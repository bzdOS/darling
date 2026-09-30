/* wayland-window-create.m -- drive the Wayland.backend from a real
 * WaylandDisplay to a real shm buffer, and log every step with numbers.
 *
 * WHY THIS TEST EXISTS IN THIS SHAPE
 *
 * The sources of src/external/cocotron/AppKit/Wayland.backend/ (and of the
 * wayland_shim.c / wayland_ifaces.c / wayland_tramp.s that go with them)
 * were never committed and did not survive a copy of the build machine, so
 * the backend cannot be rebuilt. What does survive is the built backend
 * dylib, installed in the overlay as
 *
 *   /System/Library/Frameworks/AppKit.framework/Versions/C/Resources/Backends/Wayland.backend
 *
 * (that path is the GUEST path -- the guest's DYLD_ROOT_PATH is the overlay,
 * so nothing host-specific appears here) and that dylib is the only copy in
 * existence. This test therefore treats the binary as a fixture and
 * exercises the seam it was built for.
 *
 * Every method this test calls was read back out of that dylib's
 * __objc_const/__objc_methname sections, not guessed; the ones that are not
 * public AppKit API are declared through local protocols below so the
 * compiler cannot silently agree with a wrong signature.
 *
 * WHAT IS BEING PROVEN
 *
 * The previous milestone stopped at display init: the registry roundtrip
 * binds compositor/shm/xdg_wm_base/output/seat and disconnects cleanly, but
 * no WaylandWindow was ever created, so _wayland_window_create_shm_fd and
 * the flushBuffer commit were never reached. This test creates the window
 * and calls the exact method that allocates the shm pool buffer --
 * -[WaylandWindow _acquireBackBufferForWidth:height:] -- which is the C
 * function _wayland_window_create_shm_fd behind it. Evidence it got there:
 * the returned buffer is non-nil, the backend's own failure strings ("shm
 * allocation failed for %dx%d", "mmap failed for %dx%d", "wl_shm_pool_
 * create_buffer failed for %dx%d") stay absent from the log, and a new
 * regular file appears in the working directory -- that file IS the shm fd,
 * since the shim backs the pool with a cwd-relative regular file rather
 * than memfd/shm_open.
 *
 * A nil buffer here is a real, reportable result, not a harness bug: with a
 * headless seat the shm format negotiation has nothing to go on and the
 * backend refuses to guess. The log records which of the two happened.
 */

#import <Foundation/Foundation.h>
#include <CoreGraphics/CGGeometry.h>
#include <dirent.h>
#include <errno.h>
#include <objc/runtime.h>
#include <dlfcn.h>
#include <pthread.h>
#include <semaphore.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

/* The AppKit headers cannot be included from this tree at all:
 * AppKit/include/AppKit/NSGraphics.h:21 pulls
 * <ApplicationServices/ApplicationServices.h>, and the flattened SDK
 * vendors only CoreFoundation and Security.framework -- there is no
 * ApplicationServices umbrella anywhere in tests/vendor/. That is the same
 * pre-existing gap that blocks the AppKit stage of build-gui.sh, and it is
 * why this test declares the three AppKit classes it touches instead of
 * importing them. Only the methods actually called are declared; the
 * selectors and encodings they must match were read out of the built
 * backend (see below), so a mismatch here fails to compile rather than
 * misbehaving at runtime. Foundation.h supplies NSBundle/NSString/NSArray. */
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

/* The backend's Info.plist names this as NSPrincipalClass. */
static const char *kBackendRelativePath =
	"/System/Library/Frameworks/AppKit.framework/Versions/C/Resources/"
	"Backends/Wayland.backend";

/* Private surface, read out of the built backend's ObjC metadata.
 * -shm/-compositor/-wmBase are @16@0:8, newWindowWithDelegate: is @24@0:8@16,
 * flushBuffer is v16@0:8, setFrame: is v48@0:8{CGRect}16. */
@protocol WLDisplayProbe <NSObject>
- (void *) shm;
- (void *) compositor;
- (void *) wmBase;
@end

/* Did the compositor answer?
 *
 * The previous run reached wl_surface_commit and stopped, which proves the
 * request was MARSHALLED and flushed and proves nothing about whether sway
 * took it. Sending a request is not receiving a reply.
 *
 * The obvious receipt is a frame callback, and it is not available: the
 * backend never calls wl_surface_frame, so it never asks for one and there is
 * nothing to wait for. That is a measurement of the backend, not an assumption
 * -- the whole call list has no wl_surface_frame in it. So the receipt is
 * wl_display_get_error, and a roundtrip is how the display gets a chance to
 * deliver it.
 *
 * Getting at those two needs the wl_display*, and the route to it is a
 * selector, not an ivar offset: -[WaylandDisplay waylandDisplay]. Guessing an
 * offset inside the object's layout would read as a crash rather than as a
 * wrong answer, and the build gate refuses a selector the backend does not
 * implement -- which is how the first attempt at this was caught, having
 * wrongly declared the statically linked _wl_display_roundtrip as if it were a
 * method of WaylandDisplay. It is a C function, not a selector.
 *
 * The two C functions are resolved with dlsym, because the wayland shim has
 * already resolved them in this process and they are not exported by the
 * backend. A NULL from dlsym is reported, not treated as success. */
@protocol WLDisplayRoundtripProbe <NSObject>
- (void *) waylandDisplay;
@end

typedef int (*wl_roundtrip_fn)(void *);
typedef int (*wl_get_error_fn)(void *);

/* The roundtrip runs on its own thread because it blocks until the compositor
 * answers, and "blocks forever" has to be a printed result rather than a stuck
 * run. At file scope, not nested in main: this compiler rejects a function
 * definition inside a block, and a nested one would also be a GNU extension
 * nothing else here relies on.
 *
 * No block literal and no NSThread either. The guest stack was measured to be
 * at its limit at this depth -- one extra libc call here faults on the guard
 * page -- so the thread starts with a two-pointer argument on a stack slot and
 * nothing else is asked for. */
struct rt_arg {
	void *wl;
	sem_t *done;
	wl_roundtrip_fn roundtrip;
};

static int rt_result;   /* -12345 until the thread reports */
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

/* The buffer record, recovered from the method's own type encoding AND
 * cross-checked against its disassembly:
 *
 *   _acquireBackBufferForWidth:height:  is
 *     ^{?=^{wl_buffer}^vQiiiic}24@0:8i16i20
 *
 * Two things about that are easy to get wrong, and both were:
 *
 * 1. The leading `^` means POINTER to the struct. The method returns
 *    `struct WLBackBuffer *`, NOT the struct by value. The prologue is the
 *    plain four-register form -- RDI=self, RSI=_cmd, RDX=width, ECX=height --
 *    with no hidden sret pointer, and the epilogue returns a pointer loaded
 *    from the stack in RAX. Declaring the struct as a by-value return would
 *    make the caller allocate a 48-byte temporary and pass ITS ADDRESS in
 *    RDI, where the callee expects `self`; everything after that is garbage.
 *
 * 2. The field order is not the obvious one. The struct is 48 bytes (0x30,
 *    the stride of the _buffers ivar array), and inside the method two
 *    comparisons settle two of the four ints:
 *        movl 0x1c(%rax), %eax ; cmpl -0x1c(%rbp), %eax   -> field@28 == width
 *        movl 0x20(%rax), %eax ; cmpl -0x20(%rbp), %eax   -> field@32 == height
 *    with 0x1c/0x20 the `width:`/`height:` arguments. So width is at 28 and
 *    height at 32, and the two fields at 24 and 36 are NOT width/height as a
 *    first guess would put them. Their meaning is not recoverable without the
 *    sources, so they are carried as raw ints and deliberately NOT used for
 *    addressing pixels.
 *
 * `buffer` is the wl_buffer the shim created (the method NULL-checks field 0
 * before reusing an entry) and `pixels` is the mmap of the shm backing file,
 * so writing through it proves the shm fd was not merely created but mapped
 * and writable. Only the first pixel is written: the row pitch lives in one
 * of the two unidentified fields, and guessing it would turn the evidence
 * into an out-of-bounds write. */
struct wl_buffer;

typedef struct WLBackBuffer {
	struct wl_buffer *buffer;
	void *pixels;
	unsigned long long serial;
	int field_24;              /* unidentified: not width, not height */
	int width;                 /* offset 28, confirmed by disassembly */
	int height;                /* offset 32, confirmed by disassembly */
	int field_36;              /* unidentified: possibly the row pitch */
	char flipped;              /* offset 40 */
} WLBackBuffer;

@protocol WLWindowProbe <NSObject>
- (WLBackBuffer *) _acquireBackBufferForWidth: (int)width height: (int)height;
- (void) flushBuffer;
@end

static int gStep = 0;

static void step(const char *fmt, ...) {
	va_list ap;
	va_start(ap, fmt);
	printf("[step %02d] ", ++gStep);
	vprintf(fmt, ap);
	printf("\n");
	fflush(stdout);
	fflush(stderr);
	va_end(ap);
}

static void note(const char *fmt, ...) {
	va_list ap;
	va_start(ap, fmt);
	printf("         ");
	vprintf(fmt, ap);
	printf("\n");
	fflush(stdout);
	va_end(ap);
}

/* Snapshot the working directory so the shm backing file can be identified
 * by what appeared, not by a hardcoded name. */
static int snapshotCwd(char names[][256], int max) {
	DIR *d = opendir(".");
	struct dirent *e;
	int n = 0;
	if (d == NULL) {
		return -1;
	}
	while ((e = readdir(d)) != NULL && n < max) {
		if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0) {
			continue;
		}
		snprintf(names[n], 256, "%s", e->d_name);
		n++;
	}
	closedir(d);
	return n;
}

int main(void) {
	char before[64][256];
	char after[64][256];
	int nBefore, nAfter, i, j;

	setvbuf(stdout, NULL, _IONBF, 0);
	setvbuf(stderr, NULL, _IONBF, 0);

	step("start: pid=%d uid=%d", (int)getpid(), (int)getuid());
	note("WAYLAND_DISPLAY=%s", getenv("WAYLAND_DISPLAY") ? getenv("WAYLAND_DISPLAY") : "(unset)");
	note("XDG_RUNTIME_DIR=%s", getenv("XDG_RUNTIME_DIR") ? getenv("XDG_RUNTIME_DIR") : "(unset)");
	note("XDG_SESSION_TYPE=%s", getenv("XDG_SESSION_TYPE") ? getenv("XDG_SESSION_TYPE") : "(unset)");

	nBefore = snapshotCwd(before, 64);
	note("cwd entries before: %d", nBefore);

	/* --- load the backend explicitly, out of the overlay ------------- */
	step("bundle path = %s", kBackendRelativePath);
	{
		NSBundle *bundle = [NSBundle bundleWithPath: [NSString stringWithUTF8String: kBackendRelativePath]];
		if (bundle == nil) {
			step("FATAL: no bundle at that path (is the overlay mounted as DYLD_ROOT_PATH?)");
			return 2;
		}
		note("bundle loaded=%d", (int)[bundle load]);
		Class principal = [bundle principalClass];
		note("principalClass=%s", principal ? class_getName(principal) : "(nil)");
		if (principal == nil) {
			step("FATAL: bundle has no NSPrincipalClass");
			return 3;
		}
	}

	/* --- the production path: NSDisplay picks the backend itself ------ */
	step("NSDisplay currentDisplay (discovers + inits the backend itself)");
	{
		NSDisplay *display = [NSDisplay currentDisplay];
		if (display == nil) {
			step("FATAL: no display (backend init failed -- see its own errors above)");
			return 4;
		}
		note("display class=%s instance=%p", class_getName([display class]), (void *)display);

		id<WLDisplayProbe> probe = (id<WLDisplayProbe>)display;
		note("compositor=%p shm=%p wmBase=%p",
			probe.compositor, probe.shm, probe.wmBase);
		if (probe.shm == NULL) {
			step("FATAL: wl_shm is NULL, no shm available for any buffer");
			return 5;
		}
		if (probe.compositor == NULL || probe.wmBase == NULL) {
			step("NOTE: registry incomplete (compositor/wmBase NULL), "
			     "window creation will not get past surface creation");
		}

		NSArray *screens = [display screens];
		note("screens=%lu", (unsigned long)[screens count]);
		if ([screens count] > 0) {
			NSScreen *s = [screens objectAtIndex: 0];
			note("screen[0] frame=%g,%g %gx%g scale=%g backingScaleFactor=%g",
				[s frame].origin.x, [s frame].origin.y,
				[s frame].size.width, [s frame].size.height,
				[s backingScaleFactor]);
		}

		/* --- create the window: the step never taken before --------- */
		step("newWindowWithDelegate: nil -> WaylandWindow");
		CGWindow *window = [display newWindowWithDelegate: nil];
		if (window == nil) {
			step("FATAL: newWindowWithDelegate: returned nil");
			return 6;
		}
		note("window class=%s instance=%p", class_getName([window class]), (void *)window);

		[window setFrame: (CGRect){{0, 0}, {640, 480}}];
		note("after setFrame: frame=%g,%g %gx%g",
			[window frame].origin.x, [window frame].origin.y,
			[window frame].size.width, [window frame].size.height);
		note("windowNumber=%d styleMask=%llu",
			(int)[window windowNumber], (unsigned long long)[window styleMask]);

		/* --- the shm fd: _wayland_window_create_shm_fd lives here --- */
		step("_acquireBackBufferForWidth:640 height:480 (reaches _wayland_window_create_shm_fd)");
		nBefore = snapshotCwd(before, 64);
		WLBackBuffer *rec = [(id<WLWindowProbe>)window _acquireBackBufferForWidth: 640 height: 480];
		nAfter = snapshotCwd(after, 64);
		note("buffer record=%p", (void *)rec);
		note("  buffer=%p pixels=%p serial=%llu", rec ? rec->buffer : NULL,
			rec ? rec->pixels : NULL, rec ? rec->serial : 0ull);
		note("  field_24=%d width=%d height=%d field_36=%d flipped=%d",
			rec ? rec->field_24 : -1, rec ? rec->width : -1, rec ? rec->height : -1,
			rec ? rec->field_36 : -1, rec ? (int)rec->flipped : -1);
		note("  asked for 640x480 -- the width/height fields above are what the backend stored");
		note("cwd entries before=%d after=%d", nBefore, nAfter);

		for (i = 0; i < nAfter; i++) {
			int fresh = 1;
			for (j = 0; j < nBefore; j++) {
				if (strcmp(after[i], before[j]) == 0) {
					fresh = 0;
					break;
				}
			}
			if (fresh) {
				note("NEW cwd entry (this is the shm backing file): %s", after[i]);
			}
		}

		if (rec == NULL || rec->pixels == NULL || rec->buffer == NULL) {
			step("RESULT: no buffer (record=%p buffer=%p pixels=%p) -- shm allocation did NOT succeed.",
				(void *)rec, rec ? rec->buffer : NULL, rec ? rec->pixels : NULL);
			note("the backend log line above says which of shm alloc / mmap /");
			note("wl_shm_pool_create_buffer failed, and at what size.");
			return 7;
		}

		/* pixels is the mmap of the shm file, so writing the first pixel
		 * proves the fd was not just created but mapped and writable. Only
		 * the first pixel: the row pitch is one of the two fields whose
		 * meaning is unconfirmed, and guessing it would make this an
		 * out-of-bounds write instead of evidence. */
		step("write through pixels[0] (proves the shm fd is mapped, not just created)");
		{
			unsigned int *px = (unsigned int *)rec->pixels;
			px[0] = 0xFF204060u;
			note("wrote 0x%08X at offset 0, readback 0x%08X", px[0], px[0]);
		}

		step("buffer acquired: shm fd created, mapped and written");
		step("flushBuffer (commit the acquired buffer to the compositor)");
		[(id<WLWindowProbe>)window flushBuffer];
		note("flushBuffer returned");

		/* Does the compositor answer the commit?
		 *
		 * Two lanes, and the order matters. The previous run established that
		 * roundtrip1 on the MAIN thread returned 57 while a roundtrip issued
		 * from a spawned thread never came back, so the main thread is the
		 * lane that decides the question and the thread is kept only as a
		 * second reading of the same display. The thread is started FIRST so
		 * that it is already waiting when the main lane runs, and it is
		 * labelled separately so a reader can tell which line is which.
		 *
		 * The main lane has no deadline and that is deliberate: a deadline
		 * around a main-thread roundtrip is either a second thread or a
		 * non-blocking call that is not a roundtrip. So if the main lane
		 * blocks, this log ends on the "entering" line with no RESULT after
		 * it -- and the absence is the answer, which is the one thing a
		 * timeout cannot distinguish from a hang that is itself the finding. */
		step("receipt: main-thread roundtrip after the commit (thread lane in parallel)");
		{
			id<WLDisplayRoundtripProbe> dpy =
				(id<WLDisplayRoundtripProbe>)[NSDisplay currentDisplay];
			void *wl = [dpy waylandDisplay];
			wl_roundtrip_fn roundtrip;
			wl_get_error_fn get_error;

			note("waylandDisplay = %p", wl);
			roundtrip = (wl_roundtrip_fn)dlsym(RTLD_DEFAULT, "wl_display_roundtrip");
			get_error = (wl_get_error_fn)dlsym(RTLD_DEFAULT, "wl_display_get_error");
			note("dlsym wl_display_roundtrip = %p, wl_display_get_error = %p",
			     (void *)roundtrip, (void *)get_error);
			if (wl == NULL || roundtrip == NULL || get_error == NULL) {
				note("cannot ask: the display or one of the two functions is"
				     " missing, so no receipt is claimed");
				step("RESULT: commit NOT acknowledged -- receipt unavailable, not"
				     " a failure of the commit");
			} else {
				sem_t done;
				struct rt_arg arg;
				pthread_t th;
				int thread_started = 0;
				int thread_answered;
				int main_rc, main_err;

				sem_init(&done, 0, 0);
				rt_result = -12345;
				rt_errno = 0;
				arg.wl = wl;
				arg.done = &done;
				arg.roundtrip = roundtrip;
				if (pthread_create(&th, NULL, roundtrip_thread, &arg) == 0) {
					thread_started = 1;
					note("thread-lane: started, a roundtrip is already waiting there");
				} else {
					note("thread-lane: could not start; only the main lane will report");
				}

				note("main-lane: entering wl_display_roundtrip on the MAIN thread");
				errno = 0;
				main_rc = roundtrip(wl);
				main_err = get_error(wl);
				note("main-lane: roundtrip returned %d (errno %d), "
				     "wl_display_get_error=%d", main_rc, errno, main_err);

				thread_answered = (thread_started && sem_trywait(&done) == 0);
				note("thread-lane: answered=%d%s", thread_answered,
				     thread_answered ? "" : " (still blocked, or never started)");

				if (main_rc >= 0 && main_err == 0)
					step("RESULT: COMMIT CONFIRMED -- main-thread roundtrip returned %d"
					     " and wl_display_get_error=0", main_rc);
				else
					step("RESULT: commit NOT confirmed on the main lane -- "
					     "roundtrip=%d get_error=%d; the compositor refused it, or"
					     " the reply did not come back", main_rc, main_err);

				if (main_rc >= 0 && main_err == 0 && !thread_answered
				    && thread_started)
					step("LANE FINDING: the main thread got its reply and the spawned"
					     " thread did not -- guest threads under mldr, recorded not"
					     " fixed");

				if (thread_started) pthread_detach(th);
				sem_destroy(&done);
			}
		}

		step("RESULT: window created, shm buffer acquired + written + flushed");
	}
	return 0;
}
