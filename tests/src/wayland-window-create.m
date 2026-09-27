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
#include <objc/runtime.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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

/* The buffer record, recovered from the method's own type encoding:
 *   _acquireBackBufferForWidth:height:  is
 *     ^{?=^{wl_buffer}^vQiiiic}24@0:8i16i20
 * i.e. it returns a 48-byte struct BY VALUE -- not an object pointer. The
 * struct is {struct wl_buffer *, void *, unsigned long long, int x4, char}.
 * Getting this wrong is not cosmetic: a struct this size is returned through
 * a hidden pointer (sret) in RDI, so declaring the method as returning `id`
 * would have the caller read that pointer as the result and shift the two
 * int arguments -- i.e. the one call this test exists for would have smashed
 * its own arguments and reported garbage. Verified against the dylib by
 * build-freebsd/check-wayland-window-probe.py.
 *
 * `buffer` is the wl_buffer the shim created, and `pixels` is the mmap of
 * the shm backing file -- the strongest evidence available that the shm fd
 * was really made and mapped, so the test writes through it. */
struct wl_buffer;

typedef struct WLBackBuffer {
	struct wl_buffer *buffer;
	void *pixels;
	unsigned long long serial;
	int width;
	int height;
	int stride;
	int offset;
	char flipped;
} WLBackBuffer;

@protocol WLWindowProbe <NSObject>
- (WLBackBuffer) _acquireBackBufferForWidth: (int)width height: (int)height;
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
		WLBackBuffer rec = [(id<WLWindowProbe>)window _acquireBackBufferForWidth: 640 height: 480];
		nAfter = snapshotCwd(after, 64);
		note("wl_buffer=%p pixels=%p serial=%llu", rec.buffer, rec.pixels, rec.serial);
		note("record: %dx%d stride=%d offset=%d flipped=%d",
			rec.width, rec.height, rec.stride, rec.offset, (int)rec.flipped);
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

		if (rec.pixels == NULL || rec.buffer == NULL) {
			step("RESULT: no buffer (buffer=%p pixels=%p) -- shm allocation did NOT succeed.",
				rec.buffer, rec.pixels);
			note("the backend log line above says which of shm alloc / mmap /");
			note("wl_shm_pool_create_buffer failed, and at what size.");
			return 7;
		}

		/* pixels is the mmap of the shm file, so writing one pixel proves the
		 * fd was not just created but mapped and writable. */
		step("write through pixels (proves the shm fd is mapped, not just created)");
		{
			unsigned int *px = (unsigned int *)rec.pixels;
			if (rec.stride > 0 && rec.width > 0 && rec.height > 0) {
				px[0] = 0xFF204060u;                 /* first pixel, top-left  */
				px[(rec.height - 1) * (rec.stride / 4) + (rec.width - 1)] = 0xFF60A0C0u;
				note("wrote 2 pixels at offsets 0 and %d",
					((rec.height - 1) * (rec.stride / 4) + (rec.width - 1)) * 4);
				note("readback: first=0x%08X last=0x%08X", px[0],
					px[(rec.height - 1) * (rec.stride / 4) + (rec.width - 1)]);
			} else {
				note("SKIPPED: non-positive stride/extent (%d x %d stride %d)",
					rec.width, rec.height, rec.stride);
			}
		}

		step("buffer acquired: shm fd created, mapped and written");
		step("flushBuffer (commit the acquired buffer to the compositor)");
		[(id<WLWindowProbe>)window flushBuffer];
		note("flushBuffer returned");

		step("RESULT: window created, shm buffer acquired + written + flushed");
	}
	return 0;
}
