/* bundle-principal-class.m — why is -[NSBundle principalClass] nil for the
 * window probe's backend?
 *
 * The window probe stops at
 *
 *     [step 03] FATAL: bundle has no NSPrincipalClass
 *
 * after `bundle loaded=1` and a successful dlopen of the backend executable.
 * The plist names NSPrincipalClass=WaylandDisplay, so the nil is produced
 * somewhere inside -[NSBundle principalClass], which is three steps in this
 * tree (src/external/foundation/src/NSBundle.m:744):
 *
 *     1. [[self infoDictionary] objectForKey:@"NSPrincipalClass"]
 *     2. NSClassFromString(that)
 *     3. class_respondsToSelector(object_getClass(cls), @selector(self))
 *
 * and this program asks each of the three, in order, and prints what it got.
 * It is a black box deliberately: no step is inferred from reading the code,
 * each answer is a value the guest actually produced.
 *
 * Every question is asked in BOTH orders where the order matters — before
 * [bundle load] and after — because "the class was not registered yet" and
 * "the class is not registered at all" look identical if you only ask once.
 *
 * Nothing here touches Wayland: no WAYLAND_DISPLAY, no seat, no compositor.
 * The bundle/principalClass path never gets that far, so a probe that
 * reaches for it can only fail for reasons of its own.
 */

#import <Foundation/Foundation.h>
#import <objc/runtime.h>

#include <dirent.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static const char *kBackendRelativePath =
	"/System/Library/Frameworks/AppKit.framework/Versions/C/Resources/"
	"Backends/Wayland.backend";
static const char *kPrincipalClassName = "WaylandDisplay";

static void note(const char *fmt, ...) {
	va_list ap;
	va_start(ap, fmt);
	fprintf(stderr, "[probe] ");
	vfprintf(stderr, fmt, ap);
	fprintf(stderr, "\n");
	va_end(ap);
	fflush(stderr);
}

static void step(const char *fmt, ...) {
	va_list ap;
	va_start(ap, fmt);
	fprintf(stderr, "[step] ");
	vfprintf(stderr, fmt, ap);
	fprintf(stderr, "\n");
	va_end(ap);
	fflush(stderr);
}

/* What is there to look up, and did the lookup work at all? A nil for
 * WaylandDisplay means nothing unless NSBundle itself resolves. */
static void probe_lookup_baseline(void)
{
	Class bundleCls = NSClassFromString(@"NSBundle");
	Class displayCls = NSClassFromString(@"NSDisplay");
	Class backendCls = NSClassFromString(@(kPrincipalClassName));
	note("baseline: NSClassFromString(\"NSBundle\")   = %s", bundleCls ? class_getName(bundleCls) : "(nil)");
	note("baseline: NSClassFromString(\"NSDisplay\")  = %s", displayCls ? class_getName(displayCls) : "(nil)");
	note("baseline: NSClassFromString(\"%s\") = %s  (before any bundle is loaded)",
	     kPrincipalClassName, backendCls ? class_getName(backendCls) : "(nil)");
}

/* Question 1: does the plist come back at all, and does it carry the key?
 * `count` distinguishes "no dictionary" from "a dictionary without the key",
 * which are different bugs with the same symptom. */
static void probe_info_dictionary(NSBundle *bundle, const char *when)
{
	NSDictionary *info = [bundle infoDictionary];
	if (info == nil) {
		note("%s: infoDictionary = (nil)  <- HYPOTHESIS (a): the plist is not being read",
		     when);
		return;
	}
	note("%s: infoDictionary count = %lu", when, (unsigned long) [info count]);
	{
		/* enumerate so the answer is not just the one key that matters:
		 * an empty-ish dictionary that happens to lack the key is a
		 * different failure from a fully parsed plist */
		NSArray *keys = [info allKeys];
		unsigned long i;
		for (i = 0; i < (unsigned long) [keys count] && i < 32; ++i) {
			note("%s:   key[%lu] = %s", when, i, [[keys objectAtIndex: i] UTF8String]);
		}
	}
	{
		id principal = [info objectForKey: @"NSPrincipalClass"];
		if (principal == nil) {
			note("%s: NSPrincipalClass = (absent)  <- HYPOTHESIS (a): plist read, key missing",
			     when);
		} else {
			note("%s: NSPrincipalClass = %s  <- HYPOTHESIS (a) is refuted for %s",
			     when, [[principal description] UTF8String], when);
		}
	}
}

/* Questions 2 and 3: the class lookup and the metaclass `self` check.
 * `respondsToSelf` is the exact predicate NSBundle.m:755 uses, printed so the
 * answer is about that predicate and not about a paraphrase of it. */
static void probe_class_lookup(NSBundle *bundle, const char *when)
{
	(void) bundle;
	{
		Class cls = NSClassFromString(@(kPrincipalClassName));
		if (cls == Nil) {
			note("%s: NSClassFromString(\"%s\") = (nil)  <- HYPOTHESIS (b): the class is not in the runtime",
			     when, kPrincipalClassName);
			return;
		}
		note("%s: NSClassFromString(\"%s\") = %s  <- HYPOTHESIS (b) is refuted for %s",
		     when, kPrincipalClassName, class_getName(cls), when);
		note("%s:   objc_getClass(\"%s\") = %s (same object: %s)", when, kPrincipalClassName,
		     class_getName(objc_getClass(kPrincipalClassName)),
		     objc_getClass(kPrincipalClassName) == cls ? "yes" : "NO");
		{
			/* the third condition in principalClass, verbatim */
			BOOL responds = class_respondsToSelector(object_getClass(cls), @selector(self));
			note("%s:   class_respondsToSelector(object_getClass(%s), @selector(self)) = %d%s",
			     when, class_getName(cls), (int) responds,
			     responds ? "" : "  <- this is what makes principalClass return nil");
		}
		{
			id asObject = [cls self];
			note("%s:   [cls self] = %p", when, (void *) asObject);
		}
	}
}

/* Are the backend's classes registered with the runtime at all? If the image
 * loaded but its classes did not register, [NSClassFromString] fails for every
 * class it defines, and looking at only one of them would not show that. */
static void probe_backend_classes(const char *when)
{
	int n = objc_getClassList(NULL, 0);
	Class *all = NULL;
	int i, hits = 0;
	if (n <= 0) {
		note("%s: objc_getClassList reports %d classes", when, n);
		return;
	}
	all = (Class *) malloc(sizeof(Class) * (size_t) n);
	if (all == NULL)
		return;
	n = objc_getClassList(all, n);
	for (i = 0; i < n; ++i) {
		const char *name = class_getName(all[i]);
		if (name != NULL && strncmp(name, "Wayland", 7) == 0) {
			note("%s: registered class: %s", when, name);
			hits++;
		}
	}
	note("%s: %d class(es) total, %d of them named Wayland*", when, n, hits);
	free(all);
}

/* Hypothesis (c): is this the bundle the plist belongs to? The executable the
 * bundle points at is what has to contain the class. */
static void probe_bundle_identity(NSBundle *bundle)
{
	NSString *bp = [bundle bundlePath];
	NSString *rp = [bundle resourcePath];
	NSString *ep = [bundle executablePath];
	note("bundle: bundlePath     = %s", bp ? [bp UTF8String] : "(nil)");
	note("bundle: resourcePath   = %s", rp ? [rp UTF8String] : "(nil)");
	note("bundle: executablePath = %s", ep ? [ep UTF8String] : "(nil)");
	note("bundle: isLoaded       = %d", (int) [bundle isLoaded]);
	note("bundle: allBundles count = %lu", (unsigned long) [[NSBundle allBundles] count]);
}

/* Where the plist actually is, read without going through NSBundle.
 *
 * A bundle with a Contents directory puts Info.plist in Contents, NOT in
 * Contents/Resources — resourcePath is where RESOURCES go, and appending
 * Info.plist to it finds a file that was never there. Both are printed so the
 * distinction cannot be lost again, and both are read with the plain file API:
 * if the bytes are readable here and -[NSBundle infoDictionary] still comes
 * back empty, the fault is in the bundle layer, not in staging. */
static void probe_plist_on_disk(NSBundle *bundle)
{
	NSString *bp = [bundle bundlePath];
	NSString *rp = [bundle resourcePath];
	NSString *real = [bp stringByAppendingPathComponent: @"Contents/Info.plist"];
	NSString *wrong = [rp stringByAppendingPathComponent: @"Info.plist"];
	NSData *data = [NSData dataWithContentsOfFile: real];
	note("plist:  Contents/Info.plist      (%s) -> %lu byte(s)%s",
	     [real UTF8String], (unsigned long) [data length],
	     data ? "" : "  <- NOT readable in the guest");
	if (data) {
		note("plist:  first bytes: %.60s", (const char *) [data bytes]);
	}
	data = [NSData dataWithContentsOfFile: wrong];
	note("plist:  Resources/Info.plist    (%s) -> %lu byte(s)  [wrong path, no such file]",
	     [wrong UTF8String], (unsigned long) [data length]);
}

/* The one thing CFBundle does to find the plist before it reads it: it opens
 * the directory and iterates it (_CFIterateDirectory, CFFileUtilities.c:1034),
 * skipping any entry with d_fileno == 0 — and in the guest's dirent.h
 * d_fileno IS d_ino (sdk usr/include/sys/dirent.h:122), which libc's readdir
 * already treats as "skip me". So if the guest's directory reading yields
 * nothing, Info.plist is never even a candidate and infoDictionary comes back
 * as the empty dummy dictionary its author intended for "no plist here".
 *
 * Printed raw, therefore: the names, the inode-ish field, and the type. */
static void probe_directory_reading(NSBundle *bundle)
{
	NSString *bp = [bundle bundlePath];
	NSString *dir = [bp stringByAppendingPathComponent: @"Contents"];
	DIR *dp = opendir([dir UTF8String]);
	if (dp == NULL) {
		note("dirent: opendir(%s) FAILED", [dir UTF8String]);
		return;
	}
	note("dirent: opendir(%s) ok", [dir UTF8String]);
	{
		int entries = 0;
		struct dirent *ent;
		while ((ent = readdir(dp)) != NULL) {
			entries++;
			note("dirent:   d_name=%-14s d_fileno=%-10ld d_namlen=%-3u d_type=%d%s",
			     ent->d_name, (long) ent->d_fileno, (unsigned) ent->d_namlen,
			     (int) ent->d_type,
			     ent->d_fileno == 0 ? "   <- SKIPPED by CFBundle's 0 == d_fileno test" : "");
		}
		note("dirent: %d entr%s returned by readdir",
		     entries, entries == 1 ? "y" : "ies");
		if (entries == 0)
			note("dirent: the directory is EMPTY from the guest's side — this is why"
			     " CFBundle never sees Info.plist");
	}
	closedir(dp);
}

int main(void)
{
	setvbuf(stdout, NULL, _IONBF, 0);
	setvbuf(stderr, NULL, _IONBF, 0);

	step("start: pid=%d", (int) getpid());
	probe_lookup_baseline();

	step("bundleWithPath: %s", kBackendRelativePath);
	{
		NSBundle *bundle =
			[NSBundle bundleWithPath: [NSString stringWithUTF8String: kBackendRelativePath]];
		if (bundle == nil) {
			step("FATAL: no bundle at that path at all");
			return 2;
		}
		note("bundleWithPath: %p", (void *) bundle);
		probe_bundle_identity(bundle);
		probe_plist_on_disk(bundle);
		probe_directory_reading(bundle);

		step("before [bundle load]");
		probe_info_dictionary(bundle, "before-load");
		probe_class_lookup(bundle, "before-load");
		probe_backend_classes("before-load");

		step("[bundle load]");
		note("[bundle load] = %d", (int) [bundle load]);

		step("after [bundle load]");
		note("[bundle isLoaded] = %d", (int) [bundle isLoaded]);
		probe_info_dictionary(bundle, "after-load");
		probe_class_lookup(bundle, "after-load");
		probe_backend_classes("after-load");
		probe_bundle_identity(bundle);
		probe_plist_on_disk(bundle);
		probe_directory_reading(bundle);

		step("-[NSBundle principalClass]");
		{
			Class principal = [bundle principalClass];
			note("principalClass = %s",
			     principal ? class_getName(principal) : "(nil)  <- the wall");
			/* what the production path would then do */
			note("production path would ask: +[NSBundle bundleForClass:] style lookup, skipped");
		}

		step("the same thing again, to see whether it is cached nil");
		{
			Class principal2 = [bundle principalClass];
			note("principalClass (2nd call) = %s", principal2 ? class_getName(principal2) : "(nil)");
		}
	}

	step("done");
	return 0;
}
