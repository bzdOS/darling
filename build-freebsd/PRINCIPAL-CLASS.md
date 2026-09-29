# PRINCIPAL-CLASS — the window probe's fourth wall, and it is not a bundle

Base: `pr-arm64` = `c48ba02e`.
Two root runs of one new micro-probe, no seat, no Wayland.

**Short answer: `-[NSBundle principalClass]` returns nil because
`-[NSBundle infoDictionary]` is an EMPTY dictionary — not nil, empty, which is
the shape its author intended for "no Info.plist here". The plist is present,
where CF looks for it, and readable in the guest: 768 bytes, right XML header.
What fails is the step before that, the directory scan CF uses to FIND the
plist. From inside the guest, `opendir` on that directory succeeds and
`readdir` returns **zero** entries.**

The immediate cause is the guest's directory enumeration, which is a wall on its
own: anything that discovers a file by listing its directory is dead, and CF
does exactly that. That is not a Foundation bug and not a dyld bug, so it is
the next task, not a one-line fix here — see §5 for why I stopped here.

## 1. Where the probe stops, and what it asked

`tests/src/wayland-window-create.m:207-220`:

```objc
NSBundle *bundle = [NSBundle bundleWithPath: …kBackendRelativePath];
note("bundle loaded=%d", (int)[bundle load]);
Class principal = [bundle principalClass];
note("principalClass=%s", principal ? class_getName(principal) : "(nil)");
```

and `-[NSBundle principalClass]` in this tree is three steps, not one
(`src/external/foundation/src/NSBundle.m:744`):

```objc
NSString *principalClassName = [[self infoDictionary] objectForKey:@"NSPrincipalClass"];
Class cls = NSClassFromString(principalClassName);
if (cls != Nil && class_respondsToSelector(object_getClass(cls), @selector(self)))
        _principalClass = [cls self];
```

So a nil answer names three suspects, and they are not equally likely. The
micro-probe asks all three, before and after `[bundle load]`, because "the class
was not registered yet" and "the class is not registered at all" look identical
if you ask once.

**The NSBundle and CFBundle sources are in this tree** — `src/external/foundation/src/NSBundle.m`,
`src/external/corefoundation/CFBundle.c`, `CFBundle_InfoPlist.c` — so this is
not a black box; the black box is only used for what the log cannot say.

## 2. What the guest answered

All of this is from `$DARLING_BUILD_DIR/bundle-principal-class.log`, one run,
lines 156380-157488 of the log, the `[probe]` lines in order:

```
[probe] baseline: NSClassFromString("NSBundle")   = NSBundle
[probe] baseline: NSClassFromString("NSDisplay")  = NSDisplay
[probe] baseline: NSClassFromString("WaylandDisplay") = (nil)  (before any bundle is loaded)
[step] bundleWithPath: /System/…/Backends/Wayland.backend
[probe] bundle: bundlePath     = /System/…/Backends/Wayland.backend
[probe] bundle: resourcePath   = /System/…/Wayland.backend/Contents/Resources
[probe] bundle: executablePath = /System/…/Wayland.backend/Contents/MacOS/Wayland
[probe] plist:  Contents/Info.plist      (/System/…/Wayland.backend/Contents/Info.plist) -> 768 byte(s)
[probe] plist:  first bytes: <?xml version="1.0" encoding="UTF-8"?>
[probe] dirent: opendir(/System/…/Wayland.backend/Contents) ok
[probe] dirent: 0 entries returned by readdir
[probe] dirent: the directory is EMPTY from the guest's side — this is why CFBundle never sees Info.plist
[step] before [bundle load]
[probe] before-load: infoDictionary count = 0
[probe] before-load: NSPrincipalClass = (absent)
[probe] before-load: NSClassFromString("WaylandDisplay") = (nil)
[probe] before-load: 970 class(es) total, 0 of them named Wayland*
[step] [bundle load]
[probe] [bundle load] = 1
[probe] after-load: infoDictionary count = 0
[probe] after-load: NSPrincipalClass = (absent)
[probe] after-load: NSClassFromString("WaylandDisplay") = WaylandDisplay
[probe] after-load:   objc_getClass("WaylandDisplay") = WaylandDisplay (same object: yes)
[probe] after-load:   class_respondsToSelector(object_getClass(WaylandDisplay), @selector(self)) = 1
[probe] after-load:   [cls self] = 0x2a9ae7874870
[probe] after-load: registered class: WaylandInput
[probe] after-load: registered class: WaylandWindow
[probe] after-load: registered class: WaylandDisplay
[probe] after-load: 973 class(es) total, 3 of them named Wayland*
[probe] dirent: opendir(/System/…/Wayland.backend/Contents) ok
[probe] dirent: 0 entries returned by readdir
[step] -[NSBundle principalClass]
[probe] principalClass = (nil)  <- the wall
[probe] principalClass (2nd call) = (nil)
```

and, for contrast, the same directory on the host, four entries
(`Info.plist`, `MacOS`, `.`, `..`, all with non-zero `d_fileno`):

```
opendir($DARLING_OVERLAY/System/…/Wayland.backend/Contents) = ok
   name=.              d_fileno=5222549      d_type=4
   name=..             d_fileno=5222548      d_type=4
   name=MacOS          d_fileno=5222550      d_type=4
   name=Info.plist     d_fileno=5222552      d_type=8
```

Same filesystem, same path: the host sees four entries, the guest sees none.

## 3. The three hypotheses, settled

**(a) The plist is not read — CONFIRMED, but not for the reason it first looks
like.** Not a path error, not a format error, not old-style plugin semantics:

- The layout CF expects for a bundle with a `Contents` directory is exactly
  what is on disk: `Contents/Info.plist`. `resourcePath` came back as
  `…/Contents/Resources`, which means CF classified this bundle as version 2 —
  the modern `Contents` layout (`_CFBundleGetBundleVersionForURL`,
  `CFBundle_Resources.c:221`) — and therefore that
  `_CFBundleCopyInfoDictionaryInDirectoryWithVersion` builds
  `directoryURL = "Contents/"` and `infoURLFromBase = "Contents/Info.plist"`
  (`CFBundle_InfoPlist.c:460-462`). It looks in the right place.
- The bytes are there and readable in the guest: 768, starting with the plist
  declaration. Staging and ordinary file reads are fine.
- What it uses to *find* the file is a directory listing:
  `_CFIterateDirectory(directoryPath, …)` (`CFBundle_InfoPlist.c:490`), whose
  handler only builds a URL for a name it was *shown*. With an empty listing,
  `infoPlistURL` is never set, `infoData` is never read, and the function
  returns the empty dictionary its own first comment promises: *"We only return
  NULL for a bad URL, otherwise we create a dummy dictionary"*
  (`CFBundle_InfoPlist.c:440`). `infoDictionary count = 0` is that dummy.

**(b) `WaylandDisplay` is not registered with the ObjC runtime — REFUTED.**
After `[bundle load]` returned 1: `NSClassFromString("WaylandDisplay")` is
`WaylandDisplay`, `objc_getClass` returns the same object, the class count goes
970 → 973, and all three `Wayland*` classes the backend defines are listed by
name. `llvm-nm` on the backend agrees from the outside — `_OBJC_CLASS_$_WaylandDisplay`
is a defined symbol and `__objc_classlist` is 0x18 bytes, three class pointers.
And the third condition in `principalClass` would have passed:
`class_respondsToSelector(object_getClass(WaylandDisplay), @selector(self)) = 1`,
`[cls self]` non-nil. **Had the plist been read, `principalClass` would not be
nil.** That is the whole verdict in one line.

**(c) `principalClass` looks at the wrong bundle — REFUTED.** `bundlePath` is
the backend directory, `executablePath` is
`…/Wayland.backend/Contents/MacOS/Wayland` — the same dylib the harness's
vendored-hash gate (`a4797cdd…`) checks byte for byte — and the harness gate
ran and passed in this very run's preflight. `principalClass` also reads the
same bundle's own `infoDictionary` (NSBundle.m:752), so there is no second
bundle to confuse it with.

## 4. My own error, recorded because it nearly produced a wrong finding

The first version of the probe read `resourcePath + "/Info.plist"` and printed
`0 byte(s)  <- the file is NOT readable in the guest`. That is a false
conclusion about the guest, and it is mine: a bundle's resources go in
`Contents/Resources`, its plist does not, so that path never existed. The probe
now reads both paths and labels the wrong one as wrong, and the finding in §3(a)
rests on the `Contents/Info.plist` read (768 bytes), not on the bogus one. The
first run's log is therefore not evidence of anything; the numbers quoted here
are all from the second run, whose probe asks the corrected question.

## 5. Where the wall actually is, and why I did not fix it here

The mechanism, named from the sources in this tree:

- `readdir` is FreeBSD's (`src/external/libc/gen/FreeBSD/readdir.c`), and
  `_readdir_unlocked` drops every entry whose inode field is zero:
  `if (dp->d_ino == 0 && skip) continue;` (line 118).
- In the guest's `dirent.h`, `d_fileno` **is** `d_ino`:
  `#define d_fileno d_ino` (SDK `usr/include/sys/dirent.h:122`). So one field,
  two names, and the "skip" rule is about the inode.
- `opendir` fills its buffer through `__getdirentries64` and then keeps only
  entries with a non-zero `d_fileno` in its pointer array
  (`src/external/libc/gen/FreeBSD/opendir.c:270`), zeroing the inode of
  duplicates on purpose. On FreeBSD that is self-consistent: zero inode means
  "duplicate, drop it".

So a kernel layer that hands back dirents with `d_ino == 0` — or a
`getdirentries` that returns nothing at all — makes every directory in the
guest look empty, and CF's file discovery dies silently with a dummy
dictionary. Both of those produce exactly the guest output in §2.

**I could not locate the `getdirentries`/`getdirentriesattr` implementation in
this checkout** — the syscall is declared (`syscalls.master:196` and `:222`,
`unistd.h:758`) but its body is not in `src/`, and I did not find it under
`src/external/libc/darling/` either. So the line to change is named as a
neighbourhood, not as a line, and I am not going to pretend otherwise.

The test that separates the two candidates is one call in the probe: `open`,
`getdirentries(fd, buf, sizeof buf, &basep)` on the same directory, and print
`n` plus the raw first dirent's `d_reclen`/`d_fileno`. `n <= 0` means the
syscall returns nothing; `n > 0` with zero `d_fileno` means the entries arrive
and the inode field is never filled. I stopped at the наряд's one-run budget
rather than spend a third root run on it; that call is the next task's first
move.

## 6. Reproduce

```sh
sh build-freebsd/build-bundle-principal-class-test.sh     # builds tests/bundle-principal-class-macho
DRY_RUN=1 sh build-freebsd/run-bundle-principal-class.sh  # RC=0, 7 checks, no seat needed
sh build-freebsd/run-bundle-principal-class.sh            # the root run
grep '^\[probe\]\|^\[step\]' "$DARLING_BUILD_DIR/bundle-principal-class.log"
```

`run-bundle-principal-class.sh` is `run-wayland-window-probe.sh` with
`NO_SEAT=1`, `TEST_BIN=bundle-principal-class-macho` and its own log. The
`NO_SEAT` switch is not a shortcut around the preflight: the vendored-backend
hash check, the dylib closure walk, the staging list derived from it, the
probe-shape check, the harness rebuild gate and the root run itself all still
happen — seven checks instead of eight, the missing one being `sway is alive`.

Verified that the default path is unchanged: `DRY_RUN=1 sh
build-freebsd/run-wayland-window-probe.sh` still reports `8 check(s) passed`
and `seat capabilities=3`.

## 7. What this changes about the plan

- `principalClass=(nil)` is **not** a Foundation or NSBundle defect and not a
  staging defect. Fixing it means fixing guest directory enumeration, which is
  below Foundation and probably below libc.
- The window probe is further from a window than the log line suggested: step 03
  was the first thing to touch a file-discovery path, and that path is broken.
  Expect the next wall after this one to be in the same family — a bundle
  resource or a framework discovered by listing a directory rather than by path.
- **Not verified** that fixing enumeration fixes `principalClass`. The chain
  says it should (§3b: the class and the `@selector(self)` check are both
  fine), but "should" is not "does", and no run has shown it yet.
