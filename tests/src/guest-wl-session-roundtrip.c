/* guest-wl-session-roundtrip.c — name the park INSIDE the backend's stateful
 * session.
 *
 * Step 5's staging (tests/src/guest-wl-roundtrip-stage.c, §13) measured the
 * roundtrip's primitives alive on a FRESH connection — marshal+flush, poll
 * and dispatch all return from a spawned guest thread, with main at rest,
 * joined, and concurrently roundtripping. Window run №9-4's park therefore
 * is not a primitive and not queue contention on a virgin display: it needs
 * the backend's stateful session — registry globals bound, shm pool, xdg
 * surface, a committed window, and the backend's own listeners attached.
 * This probe builds that session the way the window probe built it (NSBundle
 * load, NSDisplay currentDisplay, window, shm buffer, commit) and then asks
 * where the spawned-thread roundtrip parks in it.
 *
 * SESSION STAGES, each with a marker and its own duration:
 *   bundle-load / display-init (the backend's registry roundtrip + binds,
 *   one call from out here — named as both, honestly not separable),
 *   window-create (xdg surface+toplevel ride inside this call), shm-pool
 *   (_acquireBackBuffer + a pixel write proving the mmap), commit
 *   (flushBuffer), and an xdg-configure receipt via a main-thread roundtrip
 *   (the receipt pattern run №9-3 established).
 *
 * THREE VARIANTS on that session, one spawned thread each:
 *   (b) main dispatches — the spawned lane runs the DECOMPOSED roundtrip
 *       (marshal sync + flush → poll(POLLIN) → dispatch + done callback)
 *       while main runs wl_display_roundtrip on the same display. Which
 *       stage's deadline fires is the attribution.
 *   (a) main at rest — the spawned lane runs the OPAQUE wl_display_roundtrip
 *       (№9-4's exact call) while main does nothing with the display.
 *   (c) FULL №9-4 reproduction — the window probe's own tail, verbatim in
 *       shape: thread lane started FIRST, main lane unbounded after it,
 *       sem_trywait after main returns. The control that MUST show the
 *       lane finding: main answered, spawned still blocked. If the spawned
 *       lane answers instead, that is a finding too and the setup
 *       differences are printed, not papered over.
 *
 * NEGATIVE CONTROL, kept from step 5 and run before the backend loads:
 * wl_display_connect to a name that cannot exist must refuse with errno.
 *
 * The guest is one process and NSDisplay currentDisplay is a singleton, so
 * the session is built ONCE and the variants run in order of destructiveness:
 * (b) fully bounded first, then (a), then (c) last. Abandoned lanes are
 * file-scope-context threads (the socket-wait lesson) and are never joined
 * past the deadline. Unbuffered output; the process always exits.
 *
 * The vendored backend is the only one in existence
 * (tests/vendor/wayland-backend/README.md); its private interfaces are
 * declared here exactly as tests/src/wayland-window-create.m declares them —
 * that file passes the build gate, and a wrong signature here would fail the
 * same way. mldr/trap are NOT touched by this probe.
 */

#import <Foundation/Foundation.h>
#include <CoreGraphics/CGGeometry.h>
#include <dlfcn.h>
#include <errno.h>
#include <poll.h>
#include <pthread.h>
#include <semaphore.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>
#include <objc/runtime.h>

/* ---- declarations copied verbatim from wayland-window-create.m ---- */
@interface NSScreen : NSObject
- (CGRect) frame;
- (double) backingScaleFactor;
@end

@interface CGWindow : NSObject
- (CGRect) frame;
- (void) setFrame: (CGRect)frame;
- (int) windowNumber;
- (unsigned long long) styleMask;
@end

@interface NSDisplay : NSObject
+ (NSDisplay *) currentDisplay;
- (NSArray *) screens;
- (CGWindow *) newWindowWithDelegate: (id)delegate;
@end

static const char *kBackendRelativePath =
	"/System/Library/Frameworks/AppKit.framework/Versions/C/Resources/"
	"Backends/Wayland.backend";

@protocol WLDisplayProbe <NSObject>
- (void *) shm;
- (void *) compositor;
- (void *) wmBase;
@end

@protocol WLDisplayRoundtripProbe <NSObject>
- (void *) waylandDisplay;
@end

struct wl_buffer;

typedef struct WLBackBuffer {
	struct wl_buffer *buffer;
	void *pixels;
	unsigned long long serial;
	int field_24;
	int width;
	int height;
	int field_36;
	char flipped;
} WLBackBuffer;

@protocol WLWindowProbe <NSObject>
- (WLBackBuffer *) _acquireBackBufferForWidth: (int)width height: (int)height;
- (void) flushBuffer;
@end

/* ---- markers ---- */
static int gStep;

static void step(const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	printf("[step %02d] ", ++gStep);
	vprintf(fmt, ap);
	printf("\n");
	va_end(ap);
}

static void note(const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	printf("         ");
	vprintf(fmt, ap);
	printf("\n");
	va_end(ap);
}

static long now_ms(void)
{
	struct timespec ts;

	if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) return 0;
	return (long)ts.tv_sec * 1000L + ts.tv_nsec / 1000000L;
}

static int wait_flag(volatile int *flag, long bound_ms)
{
	long start = now_ms();

	if (start == 0) return *flag ? 1 : 0;
	while (now_ms() - start < bound_ms) {
		if (*flag) return 1;
	}
	return *flag ? 1 : 0;
}

/* ---- the C surface, resolved with dlsym AFTER the backend loads: the shim
 * has already resolved them in this process and the backend does not export
 * them (run №9-3's finding). NULL is reported, never treated as success. ---- */
typedef int (*wl_roundtrip_fn)(void *);
typedef int (*wl_get_error_fn)(void *);
typedef int (*wl_flush_fn)(void *);
typedef int (*wl_dispatch_fn)(void *);
typedef int (*wl_get_fd_fn)(void *);
typedef void *(*wl_connect_fn)(const char *);
typedef void *(*wl_marshal_fn)(void *, unsigned int, const void *, ...);
typedef int (*wl_add_listener_fn)(void *, void (**)(void), void *);

static wl_roundtrip_fn p_roundtrip;
static wl_get_error_fn p_get_error;
static wl_flush_fn p_flush;
static wl_dispatch_fn p_dispatch;
static wl_get_fd_fn p_get_fd;
static wl_connect_fn p_connect;
static wl_marshal_fn p_marshal;
static wl_add_listener_fn p_add_listener;
static const void *p_cb_iface;

static int resolve_c_surface(void)
{
	p_roundtrip = (wl_roundtrip_fn)dlsym(RTLD_DEFAULT, "wl_display_roundtrip");
	p_get_error = (wl_get_error_fn)dlsym(RTLD_DEFAULT, "wl_display_get_error");
	p_flush = (wl_flush_fn)dlsym(RTLD_DEFAULT, "wl_display_flush");
	p_dispatch = (wl_dispatch_fn)dlsym(RTLD_DEFAULT, "wl_display_dispatch");
	p_get_fd = (wl_get_fd_fn)dlsym(RTLD_DEFAULT, "wl_display_get_fd");
	p_connect = (wl_connect_fn)dlsym(RTLD_DEFAULT, "wl_display_connect");
	p_marshal = (wl_marshal_fn)dlsym(RTLD_DEFAULT,
	                                  "wl_proxy_marshal_constructor");
	p_add_listener = (wl_add_listener_fn)dlsym(RTLD_DEFAULT,
	                                           "wl_proxy_add_listener");
	p_cb_iface = dlsym(RTLD_DEFAULT, "wl_callback_interface");
	note("dlsym roundtrip=%p get_error=%p flush=%p dispatch=%p get_fd=%p",
	     (void *)p_roundtrip, (void *)p_get_error, (void *)p_flush,
	     (void *)p_dispatch, (void *)p_get_fd);
	note("dlsym connect=%p marshal=%p add_listener=%p cb_iface=%p",
	     (void *)p_connect, (void *)p_marshal, (void *)p_add_listener,
	     p_cb_iface);
	if (p_roundtrip == NULL || p_get_error == NULL || p_flush == NULL ||
	    p_dispatch == NULL || p_get_fd == NULL || p_connect == NULL)
		return -1;
	return 0;
}

/* ---- bind-lock word reader: at any probe moment, print the state of the
 * rtld bind lock that host dlsym takes (do_dlsym -> rlock_acquire on
 * rtld_bind_lock). The lock pointer variable sits in ld-elf's BSS at link
 * offset 0x1fe20 on this host build; the lock word is the uint32 at the
 * object it points to (def_lock_acquire: write-lock = cmpxchg to 1,
 * read-lock = add 2). Reading is plain loads in the shared address space;
 * /proc/self/maps is how the base is found. ---- */
static void dump_bind_lock(const char *tag)
{
	FILE *f = fopen("/proc/self/maps", "r");
	char line[512];
	void *base = NULL;
	int realpid = 0;
	FILE *pf;

	if (f == NULL) {
		/* /proc/self is not reachable from the guest view; the watcher
		 * resolves the real mldr pid at run start and leaves it in
		 * /tmp/park-realpid — read that, then /proc/<pid>/maps. */
		pf = fopen("/tmp/park-realpid", "r");
		if (pf != NULL) {
			if (fscanf(pf, "%d", &realpid) == 1 && realpid > 0) {
				char p[64];
				snprintf(p, sizeof(p), "/proc/%d/maps", realpid);
				f = fopen(p, "r");
			}
			fclose(pf);
		}
		if (f == NULL) {
			note("[bindlock] %s maps-unreadable errno=%d", tag, errno);
			return;
		}
	}
	while (fgets(line, sizeof(line), f) != NULL) {
		unsigned long start, end, off;
		if (sscanf(line, "%lx-%lx %*4s %lx %*s %*s",
			   &start, &end, &off) != 3)
			continue;
		if (strstr(line, "ld-elf.so.1") != NULL && off == 0) {
			base = (void *)start;
			break;
		}
	}
	fclose(f);
	if (base == NULL) {
		note("[bindlock] %s ld-elf-base-not-found", tag);
		return;
	}
	{
		void **slot = (void **)((char *)base + 0x1fe20);
		void *lockobj = *slot;
		unsigned int word = lockobj ? *(unsigned int *)lockobj : 0;
		note("[bindlock] %s base=%p slot=%p lockobj=%p word=0x%x",
		     tag, base, (void *)slot, lockobj, word);
	}
}


/* ---- the decomposed lane (variant b), file-scope state ---- */
#define ST_FLUSH    1
#define ST_POLL     2
#define ST_DISPATCH 3
#define ST_DONE     4
#define LANE_BOUND_MS 8000
/* roundtrip-return lane: runtime override — WL_LANE_BOUND_MS=120000
 * makes the (a)/(d) waits bound-free-ish, to separate a real park from
 * the 8s bound burning under trace load. */
static long lane_bound_ms = LANE_BOUND_MS;

struct decomp_lane {
	void *wl;
	int fd;
	volatile int stage;
	volatile int returned;
	volatile int done;
	struct { int rc; int err; long ms; } s[ST_DONE + 1];
};

static struct decomp_lane g_dc;

static void dc_cb_done(void *data, void *proxy, uint32_t callback_data)
{
	(void)proxy;
	(void)callback_data;
	*(volatile int *)data = 1;
}

static void *decomp_body(void *p)
{
	struct decomp_lane *L = (struct decomp_lane *)p;
	struct pollfd pfd;
	void *impl[1];
	long t0;

	/* stage: marshal a sync on the default queue + flush (the write) */
	L->stage = ST_FLUSH;
	t0 = now_ms();
	errno = 0;
	{
		void *cb = p_marshal(L->wl, 0 /* WL_DISPLAY_SYNC */,
		                     p_cb_iface, NULL);
		if (cb == NULL) {
			L->s[ST_FLUSH].rc = -1;
			L->s[ST_FLUSH].err = errno;
			L->returned = 1;
			return NULL;
		}
		impl[0] = (void (*)(void))dc_cb_done;
		if (p_add_listener != NULL)
			p_add_listener(cb, impl, (void *)&L->done);
		L->s[ST_FLUSH].rc = p_flush(L->wl);
		L->s[ST_FLUSH].err = errno;
		L->s[ST_FLUSH].ms = now_ms() - t0;
	}

	/* stage: poll(POLLIN) on the display fd (the readiness wait) */
	L->stage = ST_POLL;
	t0 = now_ms();
	pfd.fd = L->fd;
	pfd.events = POLLIN;
	pfd.revents = 0;
	L->s[ST_POLL].rc = poll(&pfd, 1, lane_bound_ms);
	L->s[ST_POLL].err = errno;
	L->s[ST_POLL].ms = now_ms() - t0;

	/* stage: dispatch (the read/dispatch half) */
	L->stage = ST_DISPATCH;
	t0 = now_ms();
	L->s[ST_DISPATCH].rc = p_dispatch(L->wl);
	L->s[ST_DISPATCH].err = errno;
	L->s[ST_DISPATCH].ms = now_ms() - t0;

	L->returned = 1;
	return NULL;
}

static void print_decomp(const char *who, struct decomp_lane *L, int completed)
{
	int i;

	for (i = ST_FLUSH; i <= ST_DISPATCH; i++) {
		printf("  %-6s %-12s %s rc=%d errno=%s over %ldms\n", who,
		       (i == ST_FLUSH) ? "marshal+flush" :
		       (i == ST_POLL) ? "poll" : "dispatch",
		       L->s[i].rc < 0 ? "REFUSED" :
		       (i == ST_POLL && L->s[i].rc == 0) ? "DID-NOT-RETURN" :
		       "RETURNED",
		       L->s[i].rc,
		       L->s[i].err ? strerror(L->s[i].err) : "0",
		       L->s[i].ms);
	}
	if (completed)
		printf("  %-6s %-12s RETURNED (sync callback fired)\n", who,
		       "done-callback");
	else
		printf("  %-6s %-12s DID-NOT-RETURN — parked at stage %d\n", who,
		       "done-callback", L->stage);
}

/* the opaque lane (variants a and c), window-probe shape */
struct rt_arg {
	void *wl;
	sem_t *done;
	wl_roundtrip_fn roundtrip;
};

static int rt_result = -12345;
static int rt_errno;

static void *roundtrip_thread(void *p)
{
	struct rt_arg *a = p;
	int r;

	errno = 0;
	r = a->roundtrip(a->wl);
	rt_errno = errno;
	rt_result = r;
	sem_post(a->done);
	return NULL;
}

int main(void)
{
	void *wl = NULL;
	int decomp_main_rc = 0, decomp_main_err = 0;
	int a_spawn_answered, c_thread_answered, d_fresh_answered = -1;
	long t0;

	setvbuf(stdout, NULL, _IONBF, 0);
	setvbuf(stderr, NULL, _IONBF, 0);

	/* roundtrip-return lane: bound override from the environment */
	{
		const char *b = getenv("WL_LANE_BOUND_MS");
		if (b != NULL && b[0] != '\0')
			lane_bound_ms = atol(b);
	}

	step("start: pid=%d uid=%d", (int)getpid(), (int)getuid());
	note("WAYLAND_DISPLAY=%s", getenv("WAYLAND_DISPLAY") ? getenv("WAYLAND_DISPLAY") : "(unset)");
	note("XDG_RUNTIME_DIR=%s", getenv("XDG_RUNTIME_DIR") ? getenv("XDG_RUNTIME_DIR") : "(unset)");

	/* NEGATIVE CONTROL runs AFTER the backend loads (see below, right
	 * after the C surface resolves): a pre-load check resolves nothing —
	 * the shim is not in the process yet — which is how one draft of this
	 * probe printed "REFUSED errno=0" for a missing FUNCTION rather than
	 * a dead socket, and the next one FATALed on an empty dlsym. */

	/* ---- session build (the stateful layer) ---- */
	t0 = now_ms();
	step("bundle load: %s", kBackendRelativePath);
	{
		NSBundle *bundle = [NSBundle bundleWithPath:
			[NSString stringWithUTF8String: kBackendRelativePath]];
		if (bundle == nil) {
			step("FATAL: no bundle at that path");
			return 2;
		}
		note("bundle loaded=%d over %ldms", (int)[bundle load],
		     now_ms() - t0);
		if ([bundle principalClass] == nil) {
			step("FATAL: bundle has no NSPrincipalClass");
			return 3;
		}
	}

	t0 = now_ms();
	step("display-init: NSDisplay currentDisplay (registry roundtrip + binds inside)");
	{
		NSDisplay *display = [NSDisplay currentDisplay];
		id<WLDisplayProbe> probe;

		if (display == nil) {
			step("FATAL: no display (backend init failed)");
			return 4;
		}
		note("display-init over %ldms class=%s", now_ms() - t0,
		     class_getName([display class]));
		probe = (id<WLDisplayProbe>)display;
		note("registry: compositor=%p shm=%p wmBase=%p",
		     probe.compositor, probe.shm, probe.wmBase);
		if (probe.shm == NULL) {
			step("FATAL: wl_shm NULL — no shm pool possible");
			return 5;
		}
		if (resolve_c_surface() != 0) {
			step("FATAL: the C surface did not resolve — no lane can run");
			return 6;
		}
		wl = [(id<WLDisplayRoundtripProbe>)display waylandDisplay];
		note("waylandDisplay=%p fd=%d", wl, wl ? p_get_fd(wl) : -1);
		if (wl == NULL) {
			step("FATAL: waylandDisplay NULL");
			return 7;
		}
		if (resolve_c_surface() != 0) {
			step("FATAL: the C surface did not resolve — no lane can run");
			return 6;
		}
		/* NEGATIVE CONTROL, now that the C surface is real: a name that
		 * cannot exist must refuse with errno. */
		{
			void *dead;
			step("negative control: wl_display_connect(\"gsw-dead-nope-0000\")");
			errno = 0;
			dead = p_connect("gsw-dead-nope-0000");
			if (dead == NULL)
				note("REFUSED errno=%s — the instrument distinguishes"
				     " refusal from hang", strerror(errno));
			else
				note("connect SUCCEEDED to a dead name?! %p — negative"
				     " control FAILED", dead);
		}

		t0 = now_ms();
		step("window-create: newWindowWithDelegate: nil (xdg surface+toplevel inside)");
		{
			CGWindow *window = [display newWindowWithDelegate: nil];
			WLBackBuffer *rec;

			if (window == nil) {
				step("FATAL: newWindowWithDelegate: nil");
				return 8;
			}
			note("window-create over %ldms class=%s", now_ms() - t0,
			     class_getName([window class]));
			[window setFrame: (CGRect){{0, 0}, {640, 480}}];

			t0 = now_ms();
			step("shm-pool: _acquireBackBufferForWidth:640 height:480");
			rec = [(id<WLWindowProbe>)window
			       _acquireBackBufferForWidth: 640 height: 480];
			note("shm-pool over %ldms record=%p buffer=%p pixels=%p",
			     now_ms() - t0, (void *)rec,
			     rec ? (void *)rec->buffer : NULL,
			     rec ? rec->pixels : NULL);
			if (rec == NULL || rec->pixels == NULL ||
			    rec->buffer == NULL) {
				step("RESULT: no shm buffer — session cannot go"
				     " stateful; the backend's own line above says"
				     " which of shm alloc / mmap / create_buffer failed");
				return 9;
			}
			{
				unsigned int *px = (unsigned int *)rec->pixels;
				px[0] = 0xFF204060u;
				note("pixel write ok (readback 0x%08X) — the pool"
				     " is mapped, not just created", px[0]);
			}

			t0 = now_ms();
			step("commit: flushBuffer");
			[(id<WLWindowProbe>)window flushBuffer];
			note("commit over %ldms", now_ms() - t0);

			/* xdg-configure receipt: a main-thread roundtrip delivers
			 * the configure event the toplevel is waiting for. */
			t0 = now_ms();
			step("xdg-configure receipt: main-thread roundtrip");
			errno = 0;
			decomp_main_rc = p_roundtrip(wl);
			decomp_main_err = p_get_error(wl);
			note("xdg receipt over %ldms: roundtrip rc=%d get_error=%d",
			     now_ms() - t0, decomp_main_rc, decomp_main_err);
		}
	}

	/* ---- variant (b): main dispatches, spawned lane decomposes ----
	 * Skipped entirely when WL_SKIP_B is set: run order A then makes
	 * variant (a) the FIRST spawned lane of the session (the order
	 * experiment for the bind-lock holder). */
	if (getenv("WL_SKIP_B") == NULL) {
	step("variant (b): main dispatches (roundtrip), spawned lane decomposes");
	{
		pthread_t th, tm;
		int completed;
		struct rt_arg arg;
		sem_t done;

		memset(&g_dc, 0, sizeof(g_dc));
		g_dc.wl = wl;
		g_dc.fd = p_get_fd(wl);
		if (pthread_create(&th, NULL, decomp_body, &g_dc) != 0) {
			step("FATAL: decomp lane not created");
			return 10;
		}
		pthread_detach(th);

		sem_init(&done, 0, 0);
		arg.wl = wl;
		arg.done = &done;
		arg.roundtrip = p_roundtrip;
		(void)tm;
		t0 = now_ms();
		errno = 0;
		decomp_main_rc = p_roundtrip(wl);   /* main DISPATCHES here */
		decomp_main_err = p_get_error(wl);
		note("main dispatch-roundtrip rc=%d errno=%s get_error=%d over"
		     "%ldms", decomp_main_rc,
		     decomp_main_rc < 0 ? strerror(errno) : "0",
		     decomp_main_err, now_ms() - t0);

		completed = wait_flag(&g_dc.returned, lane_bound_ms) && g_dc.done;
		print_decomp("lane", &g_dc, completed);
		sem_destroy(&done);
		dump_bind_lock("post-b");
		}
	}

	/* ---- variant (a): main at rest, spawned lane runs the opaque
	 * roundtrip — №9-4's call shape without main touching the queue ---- */
	step("variant (a): main at rest, spawned lane runs wl_display_roundtrip");
	/* kernel-esrch gate — measured semantics (KERNEL-ESRCH.md,
	 * Method notes): the Linux revoke(2) trap translation returns
	 * WITHOUT issuing the host call, so this revoke cannot arm the
	 * dtrace window. The revoke() call is the window's POINT MARKER:
	 * it marks the variant (a) site; the host-side observer raises
	 * the dtrace window off the step marker written just above. */
	revoke("/tmp/kernel-esrch-gate");
	dump_bind_lock("pre-a");
	{
		pthread_t th;
		struct rt_arg arg;
		sem_t done;
		int answered;

		sem_init(&done, 0, 0);
		rt_result = -12345;
		rt_errno = 0;
		arg.wl = wl;
		arg.done = &done;
		arg.roundtrip = p_roundtrip;
		if (pthread_create(&th, NULL, roundtrip_thread, &arg) != 0) {
			step("FATAL: opaque lane not created");
			return 11;
		}
		pthread_detach(th);
		note("lane started; main is NOT touching the display");
		/* main sleeps out the bound — at rest, not dispatching */
		answered = wait_flag((volatile int *)&rt_result, lane_bound_ms) &&
		           rt_result != -12345;
		a_spawn_answered = answered;
		if (answered)
			note("lane: roundtrip returned %d (errno %s) over the"
			     " bound", rt_result,
			     rt_errno ? strerror(rt_errno) : "0");
		else {
			note("lane: DID-NOT-RETURN within %dms — parked inside"
			     " wl_display_roundtrip", lane_bound_ms);
			dump_bind_lock("a-parked");
		}
		sem_destroy(&done);
	}
	dump_bind_lock("post-a");

	/* ---- variant (d): FRESH display + spawned opaque roundtrip — is the
	 * park stateful-specific, or does the native roundtrip park on a
	 * guest thread regardless of state? This decides between the task's
	 * premise (stateful layer) and the disassembly's implication (the
	 * dylib's roundtrip is a lazy trampoline into the NATIVE libwayland
	 * resolved via _elfcalls). ---- */
	step("variant (d): FRESH display, spawned opaque wl_display_roundtrip");
	{
		void *fresh;
		pthread_t th;
		struct rt_arg arg;
		sem_t done;

		fresh = p_connect(NULL);
		if (fresh == NULL) {
			note("fresh connect REFUSED errno=%s — variant not taken",
			     strerror(errno));
			d_fresh_answered = -1;
		} else {
			note("fresh display=%p fd=%d", fresh, p_get_fd(fresh));
			sem_init(&done, 0, 0);
			rt_result = -12345;
			rt_errno = 0;
			arg.wl = fresh;
			arg.done = &done;
			arg.roundtrip = p_roundtrip;
			if (pthread_create(&th, NULL, roundtrip_thread, &arg) != 0) {
				note("FATAL: lane not created on the fresh display");
				d_fresh_answered = -1;
			} else {
				pthread_detach(th);
				note("lane started on the FRESH display; main at rest");
				d_fresh_answered =
					(wait_flag((volatile int *)&rt_result, lane_bound_ms) &&
					 rt_result != -12345);
				if (d_fresh_answered == 1)
					note("lane: roundtrip returned %d (errno %s)",
					     rt_result,
					     rt_errno ? strerror(rt_errno) : "0");
				else
					note("lane: DID-NOT-RETURN within %dms — parked on a"
					     " FRESH display too", lane_bound_ms);
			}
			/* a parked lane may still be inside libwayland on it:
			 * deliberately not disconnected; the process exits anyway */
			sem_destroy(&done);
		}
	}

	/* ---- variant (c): FULL №9-4 reproduction — the window probe's tail,
	 * verbatim in shape. The control that must show the lane finding. ---- */
	step("variant (c): FULL №9-4 — thread lane first, main lane unbounded");
	{
		pthread_t th;
		struct rt_arg arg;
		sem_t done;
		int main_rc, main_err, thread_answered;

		sem_init(&done, 0, 0);
		rt_result = -12345;
		rt_errno = 0;
		arg.wl = wl;
		arg.done = &done;
		arg.roundtrip = p_roundtrip;
		if (pthread_create(&th, NULL, roundtrip_thread, &arg) == 0)
			note("thread-lane: started, a roundtrip is already"
			     " waiting there");
		else
			note("thread-lane: could NOT start — the control cannot"
			     " be taken");
		note("main-lane: entering wl_display_roundtrip on the MAIN"
		     " thread");
		errno = 0;
		main_rc = p_roundtrip(wl);
		main_err = p_get_error(wl);
		note("main-lane: roundtrip returned %d (errno %d),"
		     " wl_display_get_error=%d", main_rc, errno, main_err);
		thread_answered = (sem_trywait(&done) == 0);
		c_thread_answered = thread_answered;
		note("thread-lane: answered=%d%s", thread_answered,
		     thread_answered ? "" : " (still blocked — the №9-4 park)");
		if (main_rc >= 0 && main_err == 0 && !thread_answered)
			step("LANE FINDING reproduced: main got its reply, the"
			     " spawned thread did not — on a stateful session");
		else if (main_rc >= 0 && main_err == 0 && thread_answered)
			step("NOT REPRODUCED: the spawned lane answered too"
			     " (rt=%d) — setup differences are printed below,"
			     " not papered over", rt_result);
		else
			step("CONTROL BROKEN: main-lane did not complete"
			     " (rc=%d get_error=%d) — the session itself is not"
			     " in the №9-4 state", main_rc, main_err);
		pthread_detach(th);
		sem_destroy(&done);
	}

	puts("VERDICT");
	if (d_fresh_answered == 0 && !a_spawn_answered && !c_thread_answered) {
		puts("  (native, not stateful) the spawned opaque roundtrip parks");
		puts("  on a FRESH display too — the stateful session is NOT the");
		puts("  trigger. Combined with the disassembly (the dylib's");
		puts("  wl_display_roundtrip is a lazy trampoline that jumps into");
		puts("  the NATIVE libwayland resolved via _elfcalls), the park");
		puts("  is inside native libwayland's roundtrip machinery as it");
		puts("  runs on a guest-created thread — deeper than flush/poll/");
		puts("  dispatch, which all returned here and in step 5.");
	} else if (d_fresh_answered == 1 && !a_spawn_answered &&
	           !c_thread_answered) {
		puts("  (stateful) the spawned opaque roundtrip completes on a");
		puts("  FRESH display and parks inside the backend's stateful");
		printf("  session — while the decomposed lane completed there"
		       " too (reached stage %d):\n", g_dc.stage);
		puts("  the park sits in the opaque call's stateful machinery —");
		puts("  the queue/callback plumbing the decomposition does not");
		puts("  exercise — not in the primitives, not in contention");
		puts("  with main.");
	} else if (g_dc.done && a_spawn_answered && c_thread_answered) {
		puts("  (not-reproduced) every variant completed on this");
		puts("  session: the №9-4 park did NOT recur. Differences from");
		puts("  the №9-4 setup are the finding — read the step lines");
		puts("  above against run №9-4's record in §13 and the window");
		puts("  probe's own log (this probe does not guess which");
		puts("  difference decides).");
	} else {
		printf("  (mixed) d_fresh=%d decomp_done=%d a_answered=%d"
		       " c_answered=%d — the step\n", d_fresh_answered,
		       g_dc.done, a_spawn_answered, c_thread_answered);
		puts("  markers and stage lines are the measurement; no summary");
		puts("  can stand in for them.");
	}
	return 0;
}
