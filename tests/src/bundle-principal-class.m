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
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#if !defined(O_DIRECTORY)
#define O_DIRECTORY 0x100000
#endif
#include <stdarg.h>
#include <stddef.h>
#include <sys/mman.h>
#include <semaphore.h>
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

/* THE DISCRIMINATOR.
 *
 * Everything above goes through the guest's libc: opendir() pre-reads the whole
 * directory and keeps only entries with a non-zero d_fileno
 * (libc/gen/FreeBSD/opendir.c:270), and readdir() then drops anything whose
 * inode field is zero (readdir.c:118, `if (dp->d_ino == 0 && skip) continue;`).
 * Two different faults hide behind "readdir returns nothing":
 *
 *   (1) the syscall hands back NO bytes at all, or
 *   (2) it hands back records whose d_fileno is never filled in.
 *
 * The guest hides them behind the same libc behaviour, so this calls
 * getdirentries(2) directly and prints what came back, before any filter:
 *
 *   n <= 0                     -> case (1): the syscall returns nothing
 *   n > 0, d_fileno == 0       -> case (2): records arrive, inode not filled
 *   n > 0, d_fileno != 0       -> libc is at fault, not the syscall
 *
 * The buffer is printed as raw records as well, because "n > 0" alone does not
 * say whether the records are well-formed: a kernel layer that fills d_reclen
 * wrongly would make opendir's own scan bail out at
 * `if ((dp->d_reclen <= 0) || (dp->d_reclen > (ddeptr + 1 - ddptr))) break;`
 * and drop everything after the first bad one.
 *
 * Prints, not asserts: this program is a sensor, and the point of it is that
 * the answer is a number somebody else would otherwise have to guess. */
/* Darwin's getdirentries(2) is a compile-time trap when 64-bit inodes are in
 * effect — the SDK's dirent.h replaces it with a reference to
 * `_getdirentries_is_not_available_when_64_bit_inodes_are_in_effect`, so the
 * linker refuses it (that is what happened the first time this was written).
 * The guest's readdir does not call that one: the disassembly of
 * `__readdir_unlocked$INODE64` in the overlay's libsystem_c.dylib calls
 * ___getdirentries64, which libsystem_kernel.dylib exports as Darwin syscall
 * 344. So that is what this calls, and it is the same entry point libc uses. */
extern int __getdirentries64(int fd, void *buf, int bufsize, unsigned long long *basep);

static void probe_raw_getdirentries(NSBundle *bundle)
{
	NSString *bp = [bundle bundlePath];
	NSString *dir = [bp stringByAppendingPathComponent: @"Contents"];
	const char *path = [dir UTF8String];
	/* Sizes to try, smallest first. opendir uses one page (getpagesize(),
	 * which is DIRBLKSIZ-aligned), so the page size is the one size already
	 * known to be accepted by whatever answers this on this host. A size
	 * sweep is how a caller finds out whether the layer has an upper bound
	 * it does not document: every size is reported, because "EINVAL for
	 * 32 KiB, 4 KiB works" is a fact about the layer and not about this
	 * program, and the first size that returns bytes is dumped raw. */
	static const int sizes[] = { 4096, 8192, 16384, 32768, 65536, 1024 * 1024 };
	unsigned i;
	static char buf[1024 * 1024];

	for (i = 0; i < sizeof(sizes) / sizeof(sizes[0]); ++i) {
		unsigned long long basep = 0;
		int n;
		/* Two opens, on purpose. The guest's own opendir does NOT open
		 * plainly: the disassembly of __opendir2$INODE64 in the overlay's
		 * libsystem_c.dylib opens with 0x1100004 = O_RDONLY|O_NONBLOCK|
		 * O_CLOEXEC|O_DIRECTORY. So a plain open() is compared against the
		 * same call on an O_DIRECTORY fd: if only the second one works,
		 * then the syscall refuses a descriptor it did not see opened as a
		 * directory, which is a fact about the layer and about nothing
		 * else. */
		int plain = open(path, O_RDONLY);
		int dirofd = open(path, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
		int fd = (plain >= 0 && dirofd >= 0) ? dirofd : (plain >= 0 ? plain : dirofd);
		if (fd < 0) {
			note("raw: open(%s) FAILED errno=%d (%s)", path, errno, strerror(errno));
			return;
		}
		if (plain >= 0 && dirofd >= 0) {
			memset(buf, 0xAA, sizeof(buf));
			errno = 0;
			{
				int pn = __getdirentries64(plain, buf, sizes[i], &basep);
				note("raw:   plain open(O_RDONLY),                bufsize=%-7d -> %d errno=%d (%s)",
				    sizes[i], pn, errno, pn < 0 ? strerror(errno) : "-");
			}
		} else {
			note("raw:   only one open succeeded (plain=%d o_directory=%d errno=%d %s);"
			     " continuing with that one",
			    plain, dirofd, errno, strerror(errno));
		}
		basep = 0;
		memset(buf, 0xAA, sizeof(buf));
		errno = 0;
		n = __getdirentries64(fd, buf, sizes[i], &basep);
		note("raw: __getdirentries64(%s, bufsize=%-7d) = %-4d errno=%d (%s)",
		    path, sizes[i], n, errno, n < 0 ? strerror(errno) : "-");
		if (plain >= 0 && dirofd >= 0) {
			close(plain);
			if (plain != dirofd)
				;
		}

		if (n <= 0) {
			close(fd);
			continue;
		}

		{
			int records = 0, zero_ino = 0, bad_reclen = 0, off = 0;
			/* The lower bound for "is there a record header here" is
			 * offsetof(d_name), NOT sizeof(struct dirent): the guest's
			 * dirent.h sizes d_name for NAME_MAX, so sizeof is ~1KB while
			 * a record is 28-36 bytes. With sizeof as the bound this loop
			 * never runs for a directory whose listing is smaller than one
			 * struct, and the run reports "0 record(s)" beside a buffer it
			 * has just been handed 160-164 bytes of records in -- a
			 * self-contradicting line that reads like a guest fault. */
			for (off = 0; off + (int) offsetof(struct dirent, d_name) <= n; ) {
				struct dirent *dp = (struct dirent *) (buf + off);
				if (dp->d_reclen <= 0 || off + dp->d_reclen > n) {
					bad_reclen++;
					note("raw:     record %d at offset %d: d_reclen=%u is not usable"
					     " (n=%d) -- opendir's scan breaks out here and drops the rest",
					     records, off, (unsigned) dp->d_reclen, n);
					break;
				}
				note("raw:     record %d at offset %d: d_reclen=%-3u d_fileno=%-12llu"
				     " d_seekoff=%-6lld d_namlen=%-3u d_type=%d name=\"%s\"%s",
				     records, off, (unsigned) dp->d_reclen,
				     (unsigned long long) dp->d_fileno, (long long) dp->d_seekoff,
				     (unsigned) dp->d_namlen, (int) dp->d_type, dp->d_name,
				     dp->d_fileno == 0 ? "   <- inode NOT filled in" : "");
				if (dp->d_fileno == 0)
					zero_ino++;
				records++;
				off += dp->d_reclen;
				if (records >= 12) {
					note("raw:     ... stopping after 12 records");
					break;
				}
			}
			note("raw: %d record(s), %d with d_fileno == 0, %d with a bad d_reclen",
			     records, zero_ino, bad_reclen);
			if (bad_reclen)
				note("raw: CASE (3) -- malformed records; opendir's scan stops early"
				     " and every record after the bad one is invisible");
			else if (zero_ino)
				note("raw: CASE (2) -- records arrive, but d_fileno is zero for %d of"
				     " them. readdir drops exactly those"
				     " (`if (dp->d_ino == 0 && skip) continue;`, readdir.c:118),"
				     " which is how a directory with %d entries reads as empty.",
				     zero_ino, records);
			else
				note("raw: the syscall filled in every inode, so the filtering is libc's");
			close(fd);
			return;
		}
	}
	note("raw: every size failed -- the syscall answers nothing at any size tried");
}

/* Is the syscall dead everywhere, or only for that one directory?
 *
 * EINVAL on a directory that provably contains two entries is a statement about
 * the layer, not about that path -- unless the layer rejects particular paths,
 * which is not a thing any getdirentries does. So the same raw call is made on
 * three more directories, including /, and the answer decides which story is
 * true: "the syscall is refused outright, everywhere" or "it works and
 * something about THIS directory is refused". The paths are all guest-side and
 * all reachable from the overlay, so nothing here depends on the host. */
static void probe_getdirentries_elsewhere(void)
{
	/* The question this answers is "is it AppKit or is it every framework".
	 * A run where /System/Library/Frameworks works, three unrelated
	 * frameworks fail and AppKit fails is a different lead from one where
	 * only AppKit fails, and picking between them by argument is how a
	 * two-hour wrong turn starts. Length is printed so "it broke as the path
	 * got longer" stays visible if that is what it turns out to be. */
	static const char *dirs[] = {
		/* Synthetic fixtures first: same path shape, only the number of
		 * entries differs, so the threshold (if there is one) is read off
		 * directly instead of inferred from which real directory happens to
		 * be small. /usr/lib/dir-threshold/tN holds N-2 files, i.e. N
		 * entries counting . and .. -- 3,4,5,6,7,8,9,10,11,14, which spans
		 * the known failures (3 and 4) and the known successes (10+) with
		 * every count in between filled in. */
		"/usr/lib/dir-threshold/t3",
		"/usr/lib/dir-threshold/t4",
		"/usr/lib/dir-threshold/t5",
		"/usr/lib/dir-threshold/t6",
		"/usr/lib/dir-threshold/t7",
		"/usr/lib/dir-threshold/t8",
		"/usr/lib/dir-threshold/t9",
		"/usr/lib/dir-threshold/t10",
		"/usr/lib/dir-threshold/t11",
		"/usr/lib/dir-threshold/t14",
		/* Then the same two counts with long names, which is the whole
		 * question: is the line a COUNT of entries or a BYTE total that
		 * happens to sit at 248 bytes when the names are six characters?
		 *
		 * l7 holds 5 files and l8 holds 6 — 7 and 8 entries counting . and
		 * .. — with names of 254 characters, so one dirent record is 280
		 * bytes instead of 32. l8 is therefore 1736 bytes against t8's 248
		 * with the same entry count: if l8 answers, the threshold counts
		 * entries; if it answers EINVAL, then "eight" was never about eight
		 * and every fixture built on the count is built on a coincidence.
		 *
		 * A count threshold and a byte threshold disagree here, which is the
		 * only reason these two directories are worth a root run. */
		"/usr/lib/dir-threshold/l7",
		"/usr/lib/dir-threshold/l8",
		/* then the real directories that produced the original matrix */
		"/",
		"/System",
		"/System/Library",
		"/System/Library/Frameworks",
		"/System/Library/Frameworks/AVFAudio.framework",
		"/System/Library/Frameworks/Foundation.framework",
		"/System/Library/Frameworks/CoreFoundation.framework",
		"/System/Library/Frameworks/AppKit.framework",
		"/System/Library/Frameworks/AppKit.framework/Versions",
		"/System/Library/Frameworks/AppKit.framework/Versions/C",
		"/usr/lib",
		"/usr/lib/system",
	};unsigned i;
	static char buf[32768];

	for (i = 0; i < sizeof(dirs) / sizeof(dirs[0]); ++i) {
		int fd = open(dirs[i], O_RDONLY | O_DIRECTORY);
		unsigned long long basep = 0;
		int n;
		if (fd < 0) {
			note("elsewhere: open(%s) FAILED errno=%d (%s)", dirs[i], errno, strerror(errno));
			continue;
		}
		memset(buf, 0xAA, sizeof(buf));
		errno = 0;
		n = __getdirentries64(fd, buf, 8192, &basep);
		/* The handler's only inputs are (fd, buf, len, basep). Every call
		 * here uses the same len and the same buffer, so whatever decides
		 * whether a directory lists has to be a property of the DESCRIPTOR.
		 * These four numbers are what the handler's side of the boundary can
		 * see from here; printing them for a working and a failing directory
		 * is what turns "it is the fd" into "here is what about the fd". */
		{
			struct stat st;
			int have = (fstat(fd, &st) == 0);
			note("elsewhere: len=%-4lu fd=%-4d dev=%-12llu ino=%-12llu mode=%06o %-100s -> %-6d errno=%d (%s)",
			    (unsigned long) strlen(dirs[i]), fd,
			    have ? (unsigned long long) st.st_dev : 0ull,
			    have ? (unsigned long long) st.st_ino : 0ull,
			    have ? (unsigned) (st.st_mode & 07777) : 0u,
			    dirs[i], n, errno, n < 0 ? strerror(errno) : "-");
		}
		if (n > 0) {
			struct dirent *dp = (struct dirent *) buf;
			note("elsewhere:   first record: d_reclen=%u d_fileno=%llu d_name=\"%s\"",
			    (unsigned) dp->d_reclen, (unsigned long long) dp->d_fileno, dp->d_name);
		}
		close(fd);
	}
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

/* The separator for the two hypotheses WORKAROUND-344.md §4 could not tell
 * apart, in one run and without a seat.
 *
 * The hypothesis is that +[NSDisplay init] fails because CF cannot determine
 * the framework's LAYOUT: _CFBundleCreateQueryTableAtPath opens the framework
 * ROOT and iterates it looking for Resources / Contents / Support Files,
 * matching DT_DIR or DT_LNK (CFBundle_Resources.c:255-266). AppKit.framework/
 * holds three entries — AppKit, Resources, Versions — five with . and ..,
 * which is below the eight-entry line, so foundResources is never set.
 *
 * The other candidate is the symlink: Resources is a symlink to
 * Versions/Current/Resources, and the scan of Resources/Backends has to
 * resolve it.
 *
 * These are two different directories and the guest answers them differently,
 * so one readdir of each separates them:
 *
 *   root answers 0 entries, Backends/ answers 4  ->  the root scan is the
 *       wall, and padding the framework root is the fix;
 *   the root answers                              ->  the hypothesis is
 *       wrong, and neither padding nor anything downstream of the count is
 *       where this goes. Stop.
 *
 * The root is also read twice, once through the plain path and once through
 * the AppKit.framework/Versions/C alias, because they are the same directory
 * reached differently and a difference between them would be a finding in its
 * own right rather than a duplicate line.
 *
 * Names, inodes and types are printed, exactly as probe_directory_reading does
 * for Contents/: a d_fileno of zero would be a different fault again, and the
 * DT_LNK of Resources is the one entry the layout detector actually accepts. */
static void list_dir(const char *label, NSString *path)
{
	note("rootscan: %s -- %s", label, [path UTF8String]);
	DIR *dp = opendir([path UTF8String]);
	if (dp == NULL) {
		note("rootscan:   opendir FAILED (errno %d: %s)", errno, strerror(errno));
		note("rootscan:   ^ the guest cannot open this path at all");
		return;
	}
	{
		int entries = 0;
		struct dirent *ent;
		while ((ent = readdir(dp)) != NULL) {
			entries++;
			note("rootscan:   d_name=%-16s d_fileno=%-10ld d_namlen=%-3u d_type=%d%s",
			     ent->d_name, (long) ent->d_fileno, (unsigned) ent->d_namlen,
			     (int) ent->d_type,
			     ent->d_fileno == 0 ? "   <- inode NOT filled in" : "");
		}
		note("rootscan:   %d entr%s returned by readdir%s",
		     entries, entries == 1 ? "y" : "ies",
		     entries == 0 ? "  <- EMPTY from the guest's side" : "");
		/* The question the наряд asks, answered in the log rather than
		 * by whoever reads it. The layout detector only ever needs three
		 * names out of this listing; saying whether they were visible is
		 * the whole point of the run. */
		note("rootscan:   %s: the layout detector's scan%s see Resources, "
		     "Contents or Support Files",
		     entries == 0 ? "NO" : "may or may not",
		     entries == 0 ? " CANNOT " : " ");
	}
	closedir(dp);
}

static void probe_framework_root(void)
{
	step("separating the two hypotheses: framework root vs Resources/Backends");

	/* The framework root, derived from the one path this probe has already
	 * proved it can open: the backend bundle's own path, cut at
	 * "/AppKit.framework".
	 *
	 * It is derived rather than looked up because the obvious lookup is
	 * +[NSBundle bundleWithClass:], and that is not implemented in this
	 * tree: the first version of this function called it, got nil back,
	 * and then sent a message to the nil bundle and took the guest down
	 * with SIGSEGV before printing a single directory. A separator that
	 * dies before it separates anything is worse than no separator, so
	 * this uses only calls the rest of this probe already makes. */
	static NSString *kFramework = @"/System/Library/Frameworks/AppKit.framework";
	NSString *base = nil;
	{
		NSString *backend = [NSString stringWithUTF8String: kBackendRelativePath];
		NSRange cut = [backend rangeOfString:@"/AppKit.framework"];
		if (cut.location != NSNotFound)
			base = [backend substringToIndex:cut.location + cut.length];
	}
	if (base == nil || [base length] == 0) {
		base = kFramework;
		note("rootscan: could not derive the framework root from the backend"
		     " path; using the literal %s", [base UTF8String]);
	} else {
		note("rootscan: framework root, derived from the backend path: %s",
		     [base UTF8String]);
	}

	list_dir("framework root (the directory the layout scan reads)", base);
	list_dir("same root via Versions/C",
	         [base stringByAppendingPathComponent:@"Versions/C"]);
	list_dir("Resources/Backends (the directory the backend list comes from)",
	         [base stringByAppendingPathComponent:@"Resources/Backends"]);
	list_dir("Resources/Backends via Versions/C",
	         [base stringByAppendingPathComponent:@"Versions/C/Resources/Backends"]);

	/* And the question the wall actually asks, through the API rather than
	 * through libc. bundleWithPath: is the same call the rest of this probe
	 * makes and returns, so unlike bundleWithClass: it is known to work
	 * here. If it still hands back nil, that is printed and the calls that
	 * follow it are skipped rather than sent to nil. */
	NSBundle *appKit = [NSBundle bundleWithPath: base];
	if (appKit == nil) {
		note("rootscan: bundleWithPath(%s) = nil; skipping the API half,"
		     " the four readdirs above are the separator", [base UTF8String]);
		return;
	}
	NSArray *paths = [appKit pathsForResourcesOfType: @"backend"
	                                    inDirectory: @"Backends"];
	note("rootscan: pathsForResourcesOfType:@\"backend\" inDirectory:@\"Backends\""
	     " -> %lu path(s)%s",
	     (unsigned long) [paths count], [paths count] == 0 ? "  <- the wall" : "");
	for (NSString *p in paths)
		note("rootscan:   %s", [p UTF8String]);
	[paths release];

	note("rootscan: reading the same thing a second time, to see whether the"
	     " query table is cached across calls");
	paths = [appKit pathsForResourcesOfType: @"backend" inDirectory: @"Backends"];
	note("rootscan:   -> %lu path(s) on the second call",
	     (unsigned long) [paths count]);
	[paths release];

	[appKit release];
}

/* Does a message to nil survive in this guest?
 *
 * §5 of WORKAROUND-344.md cost a root run to a SIGSEGV that happened right
 * after a method returned nil, and the obvious reading — that objc_msgSend
 * here has no nil check, where in real ObjC sending to nil is a no-op that
 * returns nil or zero — is a wall-sized claim and has never been tested. It
 * is tested here, because if it is true then every idiomatic [nil whatever]
 * in Chrome is a crash and the whole GUI effort is built on a false floor.
 *
 * So: the return type is varied on purpose. objc_msgSend's return path
 * differs by type — a plain id is the same register either way, an integer
 * is returned in the same register but zero-extended differently, and a
 * struct larger than a pointer comes back in memory whose address the
 * callee has to produce. A nil check written for the first case is not
 * automatically right for the third, and only running all three says
 * whether the check exists at all.
 *
 * Each message is preceded by a note() so that a crash names the line that
 * caused it: without them the log would end at whichever call faulted, with
 * nothing to say which of the four it was.
 *
 * Everything here is a constant expression on a known-nil object, so the
 * compiler must not be allowed to fold it away. The results are printed,
 * which is what keeps the sends.
 */
static id probe_nil_id(id target, const char *label)
{
	id r = [target description];
	note("nilmsg: %s -> %s", label, r ? "an object" : "(nil)");
	return r;
}

static NSInteger probe_nil_int(id target, const char *label)
{
	NSInteger r = [target hash];
	note("nilmsg: %s -> %lld", label, (long long) r);
	return r;
}

/* A real struct return, not an invented one. NSRange is two words, so it comes
 * back differently from a pointer on every ABI that matters here, and
 * -rangeOfString: is a declared Foundation method, so the send is one Chrome
 * would actually make. A hand-rolled struct behind a made-up selector would
 * test the compiler as much as the runtime. */
static NSRange probe_nil_struct(id target, const char *label)
{
	NSRange r = [target rangeOfString: @"abc"];
	note("nilmsg: %s -> {%lu, %lu}", label,
	     (unsigned long) r.location, (unsigned long) r.length);
	return r;
}

static void probe_nil_messages(void)
{
	step("does a message to nil survive here?");

	id n = nil;
	note("nilmsg: target = nil, four sends: id, NSInteger, struct, class pointer");

	/* Through a volatile so the sends are really sent. */
	volatile id v = n;

	probe_nil_id((id) v, "id      -description");
	probe_nil_int((id) v, "NSInteger -hash");
	probe_nil_struct((id) v, "struct   -pair");

	/* A message through a class pointer rather than an instance: objc_msgSend
	 * takes the receiver differently here and this is the one Chrome's own
	 * dispatch code leans on hardest. */
	Class cls = Nil;
	note("nilmsg: class pointer = %s", cls ? "a class" : "(nil)");
	id made = [cls alloc];
	note("nilmsg:   +alloc on a nil class -> %s", made ? "an object" : "(nil)");

	note("nilmsg: all sends returned; a real ObjC answers nil/0 and does not"
	     " fault, so a clean pass here means the nil check exists");
}

/* shm_open, asked directly, because the fourth wall's error is not what the
 * name suggests.
 *
 * The name the backend builds was "./.bsdos-wlshm-%d-%d", which POSIX rejects
 * and which the host rejects too (four lines of C: -1/EINVAL for the dotted
 * name, a real fd for the same name with one leading slash). It was patched to
 * "/.bsdos-wlshm-%d-%d" and the guest still answers EINVAL, with the patched
 * binary definitely loaded — the staged copy's sha256 is the patched one.
 *
 * So the guest's shm_open is not the host's, and the POSIX rule is not what is
 * rejecting the name here. Disassembling the kernel shim says why: sys_shm_open
 * translates the flags through oflags_bsd_to_linux and then calls a syscall
 * slot straight out of elfcalls — it is an open(2) on the name, with no
 * /dev/shm and none in the overlay either.
 *
 * Which leaves the question this prints the answer to: does the guest's shm_open
 * accept ANY name? A plain name, a leading-slash name, the patched name, a
 * name in a directory that exists, and one in a directory that does not. If
 * they all fail the same way, the wall is the call and not the name, and the
 * errno tells us which.
 *
 * Every name is tried with O_RDWR|O_CREAT|O_EXCL so a success is unambiguous,
 * and unlinked afterwards so a run leaves nothing behind.
 */
static void probe_shm_open(void)
{
	static const char *names[] = {
		"/.probe-shm-plain",
		"/probe-shm-no-slash",
		"probe-shm-relative",
		"/.bsdos-wlshm-0-1",
		"/tmp/probe-shm-in-tmp",
		"/no-such-dir-xyzzy/probe",
	};
	step("asking shm_open directly, one name at a time");

	for (unsigned i = 0; i < sizeof(names) / sizeof(names[0]); i++) {
		int fd = shm_open(names[i], O_RDWR | O_CREAT | O_EXCL, 0600);
		int e = fd < 0 ? errno : 0;
		note("shmtest: shm_open(\"%s\") = %d errno=%d (%s)",
		     names[i], fd, e, e ? strerror(e) : "-");
		if (fd >= 0) {
			close(fd);
			shm_unlink(names[i]);
		}
	}

	/* The same call with a name that is certainly fine on any system, to
	 * separate "the call is broken" from "these names are refused". */
	{
		int fd = shm_open("/.probe-shm-control", O_RDWR | O_CREAT | O_EXCL, 0600);
		int e = fd < 0 ? errno : 0;
		note("shmtest: control, a bare valid name -> %d errno=%d (%s)",
		     fd, e, e ? strerror(e) : "-");
		if (fd >= 0) { close(fd); shm_unlink("/.probe-shm-control"); }
	}
	note("shmtest: the names above differ in leading slash, in a directory that"
	     " exists, and in one that does not; identical results across all of"
	     " them would mean the call, not the name");
}

/* The flags are the variable now, and the name is held constant.
 *
 * sys_shm_open (src/external/xnu/darling/.../impl/wrapped/shm_open.c:20) does
 *
 *     ret = elfcalls()->shm_open(name, oflags_bsd_to_linux(oflag), mode);
 *
 * and elfcalls()->shm_open is the HOST's shm_open — mldr fills that table with
 * the host's own symbols (src/startup/mldr/elfcalls/elfcalls.c:118). So the
 * flags are translated from BSD to Linux and then handed to a function that
 * expects BSD. They are converted once too many.
 *
 * That is checkable without a guest at all, on native FreeBSD:
 *
 *     shm_open(name, 0x0C2, 0600) = -1 errno=22   <- what the guest passes
 *     shm_open(name, 0xA02, 0600) =  3 errno=0    <- what the caller meant
 *
 * 0xA02 is BSD O_RDWR|O_CREAT|O_EXCL. oflags_bsd_to_linux turns it into
 * 0x0C2 = O_RDWR|LINUX_O_CREAT|LINUX_O_EXCL, and the host reads 0x0C2 as BSD
 * O_RDWR|O_ASYNC|O_FSYNC, which its shm_open rejects.
 *
 * So the run below has to show the same split inside the guest: the BSD values
 * work and the translated ones do not. A result where BOTH fail would mean the
 * table itself is dead, which is the other hypothesis and is not what the
 * source says.
 *
 * sem_open is the control for the table being alive at all: it is the
 * neighbouring POSIX slot, filled the same way from the same struct, so if it
 * works then elfcalls is populated and dispatching, and whatever is wrong with
 * shm_open is in its arguments rather than in the plumbing.
 */
static void probe_shm_flags(void)
{
	step("the flags are the variable now, the name held constant");

	static const char *name = "/.probe-flags";
	/* BSD values, the Linux values oflags_bsd_to_linux maps them to, and the
	 * reverse direction, so a translation in either place shows up. */
	struct { const char *label; int flags; } cases[] = {
		{"BSD  O_RDONLY                      (0x000)", 0x000},
		{"BSD  O_RDWR                        (0x002)", 0x002},
		{"BSD  O_RDWR|O_CREAT                (0x202)", 0x202},
		{"BSD  O_RDWR|O_CREAT|O_EXCL         (0xa02)", 0xa02},
		{"LINUX O_RDWR                       (0x002)", 0x002},
		{"LINUX O_RDWR|O_CREAT|O_EXCL        (0x0c2)", 0x0c2},
		{"LINUX O_CREAT|O_EXCL               (0x0c0)", 0x0c0},
	};

	for (unsigned i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
		int fd = shm_open(name, cases[i].flags, 0600);
		int e = fd < 0 ? errno : 0;
		note("shmflags: %s -> %2d errno=%2d (%s)", cases[i].label, fd, e,
		     e ? strerror(e) : "-");
		if (fd >= 0) { close(fd); shm_unlink(name); }
	}
	note("shmflags: the prediction is that the BSD rows succeed and the rows"
	     " holding oflags_bsd_to_linux's output do not");

	/* Is the elfcalls table itself alive? sem_open is the neighbouring POSIX
	 * slot, filled from the same struct in the same file. */
	{
		sem_t *sem = sem_open("/.probe-sem", O_CREAT | O_EXCL, 0600, 0);
		int e = sem == SEM_FAILED ? errno : 0;
		note("shmflags: control, the neighbouring elfcalls slot: sem_open -> %s"
		     " errno=%2d (%s)", sem == SEM_FAILED ? "SEM_FAILED" : "a handle",
		     e, e ? strerror(e) : "-");
		if (sem != SEM_FAILED) sem_close(sem), sem_unlink("/.probe-sem");
	}
}

/* Ridden along: are symlink entries in a listing at all?
 *
 * The framework root came back from the guest as `. .. Versions pad-01 …` with
 * AppKit and Resources missing, and both of those are symlinks that are
 * present in the staged tree. So either the emulation omits symlink entries or
 * it delivers them with a zero inode for libc's readdir to drop
 * (readdir.c:118, `if (dp->d_ino == 0 && skip) continue;`).
 *
 * The fixture directory holds one symlink, one plain file, and one subdirectory
 * among enough ordinary files to be above the byte line, so the answer cannot
 * be "the directory was too small". The host's own listing of the same path is
 * printed here for comparison, name for name, because a difference in which
 * entries appear is the whole question.
 */
static void probe_symlink_entries(void)
{
	step("a symlink entry and a plain file in one listing");

	NSBundle *b = [NSBundle bundleWithPath: @"/System/Library/Frameworks/AppKit.framework"];
	NSString *root = b ? [b bundlePath] : @"/System/Library/Frameworks/AppKit.framework";
	[b release];

	/* The three real entries of the framework root, and then the staged
	 * tree's own listing, which is where the symlinks are. */
	const char *dirs[] = {
		"/System/Library/Frameworks/AppKit.framework",
		"/System/Library/Frameworks/AppKit.framework/Versions/C",
	};
	for (unsigned k = 0; k < 2; k++) {
		DIR *dp = opendir(dirs[k]);
		int entries = 0;
		note("symtest: %s", dirs[k]);
		if (dp == NULL) {
			note("symtest:   opendir FAILED errno=%d (%s)", errno, strerror(errno));
			continue;
		}
		struct dirent *ent;
		while ((ent = readdir(dp)) != NULL) {
			entries++;
			note("symtest:   %-18s d_fileno=%-10ld d_namlen=%-3u d_type=%d%s",
			     ent->d_name, (long) ent->d_fileno, (unsigned) ent->d_namlen,
			     (int) ent->d_type, ent->d_fileno == 0 ? "   <- zero inode" : "");
		}
		note("symtest:   %d entr%s", entries, entries == 1 ? "y" : "ies");
		closedir(dp);
	}
	note("symtest: d_type 4=DIR 8=REG 10=LNK on this ABI; a symlink that the"
	     " host lists as 10 and the guest does not list at all is the"
	     " question, and one that arrives as 4 would be a different fault");
	(void) root;
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
		probe_raw_getdirentries(bundle);
		probe_getdirentries_elsewhere();
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

	/* Last, and after principalClass, so a run that dies earlier still keeps
	 * the answer it managed to reach: the separator for WORKAROUND-344.md
	 * §4's two hypotheses. */
	probe_framework_root();

	/* And the question §5's crash was read as implying. Ahead of the
	 * framework scan, not after it: if a message to nil does fault here,
	 * this process dies and the scan below never prints anything, which
	 * would look like the scan failing rather than like this dying. */
	probe_nil_messages();

	/* And the wall the window is actually stopped at. Same reasoning as the
	 * nil messages: it runs here, before anything that depends on it, and
	 * its own failure is the thing it is measuring. */
	probe_shm_open();
	probe_shm_flags();
	probe_symlink_entries();

	step("done");
	return 0;
}
