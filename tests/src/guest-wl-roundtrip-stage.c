/* guest-wl-roundtrip-stage.c — WHERE does a wl roundtrip park on a spawned
 * guest thread while the main thread is alive?
 *
 * The completed discriminator (§13 steps 3-4) says thread resumption under
 * mldr is alive for every wait class measured — nanosleep, pipe read,
 * sem_wait, socket read and poll readiness all return on spawned threads —
 * so window run №9-4's hang is NOT the descriptor/event path: the same
 * display, the same wl_display_roundtrip, main returns and the spawned
 * thread does not. This probe names the STAGE at which it parks, by
 * measuring the roundtrip's own body as three stages, each with a marker,
 * its own rc/errno and its own clock duration:
 *
 *   stage 1  marshal a wl_display.sync + wl_display_flush   (the write)
 *   stage 2  poll(POLLIN) on wl_display_get_fd             (the readiness wait)
 *   stage 3  wl_display_dispatch + the sync callback done  (the read/dispatch)
 *
 * libwayland's roundtrip_queue IS this sequence (sync, flush, then
 * dispatch-queue until the callback fires), so a park at stage N is an
 * attribution, not a proxy for one. wl_display_dispatch stands in for
 * wl_display_dispatch_queue here: this build of the backend exports
 * dispatch (default queue) but not dispatch_queue, and №9-4's roundtrip
 * used the default queue anyway.
 *
 * The backend dylib is loaded the way run №9-3 loaded it — dlopen by its
 * GUEST path, dlsym per function, every resolution printed — because that
 * is the proven in-guest route and it keeps this probe's link surface at
 * libSystem alone. The dylib is the only copy of the backend in existence
 * (tests/vendor/wayland-backend/README.md).
 *
 * THREE VARIANTS, one fresh wl_display each:
 *   (a) main at rest   — the spawned lane alone touches the display; a park
 *                        here needs no concurrency to explain it.
 *   (b) main in join   — main waits on the spawned lane's result from the
 *                        first instant and cannot touch the queue. Real
 *                        pthread_join is deliberately NOT used: a spawned
 *                        thread parked forever would hang the probe itself,
 *                        which is the failure the bounds exist to prevent
 *                        (pthread_timedjoin_np is absent from the flattened
 *                        SDK headers, so the bounded flag-wait is the join).
 *   (c) main concurrent — №9-4's exact shape: main runs wl_display_roundtrip
 *                        on the SAME display while the spawned lane
 *                        decomposes. If (a)/(b) complete and (c) parks at
 *                        stage N, N is where contention for the shared
 *                        default queue bites; the main lane is deliberately
 *                        unbounded, №9-4's own methodology — the harness
 *                        timeout bounds the run and the last unbuffered
 *                        marker names the stage.
 *
 * NEGATIVE CONTROL, run first: wl_display_connect to a name that cannot
 * exist must refuse with errno — a probe that cannot tell a refusal from a
 * hang is a constant.
 *
 * Every spawn uses a file-scope context (the socket-wait probe's lesson: an
 * abandoned thread writes its result when it finally wakes, and that write
 * must not land in a returned stack frame). Unbuffered output; the program
 * always exits.
 */

#include <dlfcn.h>
#include <errno.h>
#include <poll.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define BOUND_MS 6000   /* poll's own timeout, and the observer's deadline */
#define STAGE_FLUSH    1
#define STAGE_POLL     2
#define STAGE_DISPATCH 3
#define STAGE_DONE     4

/* The backend's guest path — the install_name the dylib carries, resolved
 * through DYLD_ROOT_PATH at run time. */
static const char *kBackendPath =
	"/System/Library/Frameworks/AppKit.framework/Versions/C/Resources/"
	"Backends/Wayland.backend/Contents/MacOS/Wayland";

/* wl_display.sync is request opcode 0 in wayland.xml's wl_display. */
#define WL_DISPLAY_SYNC 0

struct wl_display;
struct wl_proxy;
struct wl_interface;

/* ---- resolved per run, printed as evidence (№9-3's pattern) ---- */
static struct wl_display *(*p_connect)(const char *);
static void (*p_disconnect)(struct wl_display *);
static int (*p_get_fd)(struct wl_display *);
static int (*p_flush)(struct wl_display *);
static int (*p_dispatch)(struct wl_display *);
static int (*p_get_error)(struct wl_display *);
static int (*p_roundtrip)(struct wl_display *);
static struct wl_proxy *(*p_marshal_constructor)(struct wl_proxy *,
                                                 uint32_t,
                                                 const struct wl_interface *,
                                                 ...);
static int (*p_add_listener)(struct wl_proxy *, void (**)(void), void *);
static const struct wl_interface *p_cb_iface;

static int load_wayland(void)
{
	void *h;
	char *err;

	h = dlopen(kBackendPath, RTLD_LAZY);
	if (h == NULL) {
		printf("  dlopen FAILED: %s\n", dlerror());
		return -1;
	}
#define RESOLVE(var, name)                                                    \
	do {                                                                 \
		var = (void *)dlsym(h, name);                                 \
		printf("  dlsym %-28s = %p\n", name, (void *)var);            \
		if ((var) == NULL) return -1;                                 \
	} while (0)
	RESOLVE(p_connect, "wl_display_connect");
	RESOLVE(p_disconnect, "wl_display_disconnect");
	RESOLVE(p_get_fd, "wl_display_get_fd");
	RESOLVE(p_flush, "wl_display_flush");
	RESOLVE(p_dispatch, "wl_display_dispatch");
	RESOLVE(p_get_error, "wl_display_get_error");
	RESOLVE(p_roundtrip, "wl_display_roundtrip");
	RESOLVE(p_marshal_constructor, "wl_proxy_marshal_constructor");
	RESOLVE(p_add_listener, "wl_proxy_add_listener");
	p_cb_iface = (const struct wl_interface *)
		dlsym(h, "wl_callback_interface");
	printf("  dlsym %-28s = %p\n", "wl_callback_interface",
	       (const void *)p_cb_iface);
	if (p_cb_iface == NULL) return -1;
#undef RESOLVE
	return 0;
}

static long now_ms(void)
{
	struct timespec ts;

	if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) return 0;
	return (long)ts.tv_sec * 1000L + ts.tv_nsec / 1000000L;
}

static int wait_flag(volatile int *flag)
{
	long start = now_ms();

	if (start == 0) return *flag ? 1 : 0;
	while (now_ms() - start < BOUND_MS) {
		if (*flag) return 1;
	}
	return *flag ? 1 : 0;
}

/* ---- one spawned lane's whole state, file-scope for the abandoned case ---- */
struct lane {
	struct wl_display *d;
	int fd;
	volatile int stage;        /* the last stage the lane ENTERED */
	volatile int returned;
	volatile int done;         /* the sync callback fired */
	struct {
		int rc;
		int err;
		long ms;
	} s[STAGE_DONE + 1];
};

static struct lane g_lane;

static void cb_done(void *data, void *proxy, uint32_t callback_data)
{
	(void)proxy;
	(void)callback_data;
	*(volatile int *)data = 1;
}

static const char *stage_name(int s)
{
	switch (s) {
	case STAGE_FLUSH:    return "marshal+flush";
	case STAGE_POLL:     return "poll";
	case STAGE_DISPATCH: return "dispatch";
	case STAGE_DONE:     return "callback-done";
	default:             return "?";
	}
}

static void print_lane(const char *who, struct lane *L, int completed)
{
	int i;

	for (i = STAGE_FLUSH; i <= STAGE_DISPATCH; i++) {
		printf("  %-6s %-14s %s rc=%d errno=%s over %ldms\n", who,
		       stage_name(i),
		       L->s[i].rc < 0 ? "REFUSED" :
		       (i == STAGE_POLL && L->s[i].rc == 0) ? "DID-NOT-RETURN" :
		       "RETURNED",
		       L->s[i].rc,
		       L->s[i].err ? strerror(L->s[i].err) : "0",
		       L->s[i].ms);
	}
	if (completed)
		printf("  %-6s %-14s RETURNED (sync callback fired)\n", who,
		       stage_name(STAGE_DONE));
	else
		printf("  %-6s %-14s DID-NOT-RETURN — the lane parked at %s\n",
		       who, stage_name(STAGE_DONE), stage_name(L->stage));
}

/* THE decomposition, run inside the lane's thread. */
static void *lane_body(void *p)
{
	struct lane *L = (struct lane *)p;
	struct pollfd pfd;
	void *impl[1];
	long t0;

	/* stage 1: marshal a sync on the default queue, flush it out */
	L->stage = STAGE_FLUSH;
	t0 = now_ms();
	errno = 0;
	{
		struct wl_proxy *cb =
			p_marshal_constructor((struct wl_proxy *)L->d,
			                      WL_DISPLAY_SYNC, p_cb_iface, NULL);
		if (cb == NULL) {
			L->s[STAGE_FLUSH].rc = -1;
			L->s[STAGE_FLUSH].err = errno;
			L->returned = 1;
			return NULL;
		}
		impl[0] = (void (*)(void))cb_done;
		p_add_listener(cb, impl, (void *)&L->done);
		errno = 0;
		L->s[STAGE_FLUSH].rc = p_flush(L->d);
		L->s[STAGE_FLUSH].err = errno;
		L->s[STAGE_FLUSH].ms = now_ms() - t0;
	}

	/* stage 2: wait for the compositor's answer to become readable */
	L->stage = STAGE_POLL;
	t0 = now_ms();
	pfd.fd = L->fd;
	pfd.events = POLLIN;
	pfd.revents = 0;
	errno = 0;
	L->s[STAGE_POLL].rc = poll(&pfd, 1, BOUND_MS);
	L->s[STAGE_POLL].err = errno;
	L->s[STAGE_POLL].ms = now_ms() - t0;

	/* stage 3: dispatch whatever is pending — the reply path */
	L->stage = STAGE_DISPATCH;
	t0 = now_ms();
	errno = 0;
	L->s[STAGE_DISPATCH].rc = p_dispatch(L->d);
	L->s[STAGE_DISPATCH].err = errno;
	L->s[STAGE_DISPATCH].ms = now_ms() - t0;

	L->returned = 1;
	return NULL;
}

struct obs_ctx {
	struct lane *L;
	volatile int join_over;
};

static void *observer(void *p)
{
	struct obs_ctx *O = (struct obs_ctx *)p;

	wait_flag(&O->L->returned);
	O->join_over = 1;
	return NULL;
}

/* Returns 1 completed, 0 parked, -1 not measurable. */
static int run_variant(const char *name, int main_concurrent)
{
	struct lane *L = &g_lane;
	struct obs_ctx O;
	pthread_t to, tl;
	int completed;

	memset(L, 0, sizeof(*L));
	printf("[%s]\n", name);

	/* a fresh connection per variant: a lane parked inside dispatch leaves
	 * display state no other variant should inherit */
	L->d = p_connect(NULL);
	if (L->d == NULL) {
		printf("  connect  REFUSED errno=%s — nothing to measure\n",
		       strerror(errno));
		return -1;
	}
	L->fd = p_get_fd(L->d);
	printf("  display=%p fd=%d\n", (void *)L->d, L->fd);

	O.L = L;
	O.join_over = 0;
	if (pthread_create(&to, NULL, observer, &O) != 0) {
		printf("  FATAL: observer thread not created\n");
		p_disconnect(L->d);
		return -1;
	}
	pthread_detach(to);
	if (pthread_create(&tl, NULL, lane_body, L) != 0) {
		printf("  FATAL: lane thread not created\n");
		p_disconnect(L->d);
		return -1;
	}
	pthread_detach(tl);

	if (main_concurrent) {
		/* №9-4's exact main lane: unbounded roundtrip on the same display.
		 * The harness timeout bounds the run; the unbuffered markers name
		 * the stage if this line is the last one in the log. */
		int rc;
		printf("  main-lane: entering wl_display_roundtrip on the MAIN"
		       " thread\n");
		errno = 0;
		rc = p_roundtrip(L->d);
		printf("  main-lane: roundtrip returned %d (errno %s),"
		       " wl_display_get_error=%d\n", rc,
		       rc < 0 ? strerror(errno) : "0", p_get_error(L->d));
	} else {
		/* main at rest / main in join: bounded waits, both leave the
		 * queue entirely to the lane. The distinction between the two is
		 * recorded, not assumed: at-rest main could in principle be woken
		 * to touch the queue, joined main cannot. */
		if (!wait_flag(&O.join_over))
			printf("  main: bounded join timed out — the lane never"
			       " returned\n");
		else
			printf("  main: bounded join over\n");
	}

	completed = wait_flag(&L->returned) && L->done;
	print_lane("lane", L, completed);
	/* the display of a parked lane is deliberately NOT disconnected: the
	 * parked thread may still be inside a libwayland call on it, and the
	 * process exits immediately after the verdict anyway */
	if (completed)
		p_disconnect(L->d);
	return completed ? 1 : 0;
}

int main(void)
{
	int neg, a, b, c;

	setvbuf(stdout, NULL, _IONBF, 0);
	puts("wl roundtrip stages on a spawned guest thread: where does it park?");
	printf("  WAYLAND_DISPLAY=%s XDG_RUNTIME_DIR=%s\n",
	       getenv("WAYLAND_DISPLAY") ? getenv("WAYLAND_DISPLAY") : "<unset>",
	       getenv("XDG_RUNTIME_DIR") ? getenv("XDG_RUNTIME_DIR") : "<unset>");

	if (load_wayland() != 0) {
		puts("VERDICT");
		puts("  (instrument) the backend dylib could not be loaded or a");
		puts("  symbol was missing — nothing was measured.");
		return 0;
	}

	/* NEGATIVE CONTROL: a name that cannot exist must refuse with errno. */
	{
		struct wl_display *dead = p_connect("gsw-dead-nope-0000");
		if (dead == NULL) {
			neg = 1;
			printf("[negative control]\n  connect(gsw-dead-nope-0000)"
			       " REFUSED errno=%s — the instrument distinguishes"
			       " refusal from hang\n", strerror(errno));
		} else {
			neg = 0;
			printf("[negative control]\n  connect SUCCEEDED to a name"
			       " that cannot exist?! display=%p — the negative"
			       " control failed\n", (void *)dead);
			p_disconnect(dead);
		}
	}

	a = run_variant("(a) main at rest", 0);
	b = run_variant("(b) main in bounded join", 0);
	c = run_variant("(c) main concurrent roundtrip (№9-4 shape)", 1);

	puts("VERDICT");
	if (!neg) {
		puts("  (instrument) the negative control did not refuse, so");
		puts("  nothing below can be trusted: a probe that cannot tell a");
		puts("  refusal from a hang is a constant.");
	} else if (a > 0 && b > 0 && c > 0) {
		puts("  (none) all three variants completed: the roundtrip came");
		puts("  back from a spawned thread with main at rest, joined and");
		puts("  concurrent. The №9-4 park did NOT reproduce on this");
		puts("  connection — whatever parked it is not present here.");
	} else if (a > 0 && b > 0 && c <= 0) {
		printf("  (contention) the lane completed with main at rest and"
		       " joined,\n  and parked at %s with main concurrent:\n",
		       stage_name(g_lane.stage));
		puts("  the shared default queue is what the spawned lane loses");
		puts("  to — the park is contention for the queue, and the stage");
		puts("  named above is where it bites.");
	} else if (a <= 0 || b <= 0) {
		printf("  (lane) the spawned lane parked at %s even with main"
		       " away\n  from the queue (a=%d b=%d c=%d):\n",
		       stage_name(g_lane.stage), a, b, c);
		puts("  the stage named above parks from a spawned thread by");
		puts("  itself — not contention, the call path.");
	} else {
		printf("  (mixed) a=%d b=%d c=%d — see the stage lines above;\n",
		       a, b, c);
		puts("  the variants disagree and the measurement, not a");
		puts("  summary, is the answer.");
	}
	return 0;
}
