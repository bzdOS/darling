# WORKAROUND-344 — the window is behind `Contents/`, and the wall moved

Base: `pr-arm64` = `4a2394006`.
**Three root runs, not the two the наряд allowed** — the reason is in §2 and it
is not a choice I made freely: the accepted model of the enumeration pointed the
layout at the wrong end of the listing, and only a run could say so.

**Short answer: `-[NSBundle principalClass]` returns `WaylandDisplay`. The
window probe gets past `[step 03]`, off `bundle has no NSPrincipalClass`, and
dies at the next wall, which is the same wall one directory up:
`Available backends are: <CFArray …>{count = 0, values = ()}`. `Backends/`
holds one entry counting `.` and `..`, three, i.e. below the eight-entry line,
so `-[NSBundle pathsForResourcesOfType:@"backend" inDirectory:@"Backends"]`
(NSDisplay.m:60-66) returns an empty array and the display class cluster has
nothing to instantiate. The fix is the same one line of fixture; it is not in
this branch, because the наряд named `Contents/` and the root budget was gone.**

## 1. What the wall was, restated in one line

`PRINCIPAL-CLASS.md` §3(a): CFBundle finds `Contents/Info.plist` by *listing*
`Contents/` (`_CFIterateDirectory`, CFBundle_InfoPlist.c:490), the guest's
listing answers nothing for a directory under eight entries, so `infoDictionary`
is the empty dummy dictionary that same function promises for "no plist here",
so `principalClass` is nil. The plist is present, 768 bytes, and names
`NSPrincipalClass = WaylandDisplay`. Nothing in Foundation is wrong; a directory
we ship is unlistable.

## 2. The layout the наряд asked for was backwards, and one run said so

The наряд said the enumeration returns "the entries from index 7" and asked for
seven pads created *first*, with `Info.plist` and `MacOS` last, so that they
would land at indices 7 and 8. That is the correct fixture for a window that
starts at the seventh record, and it is not the window this handler has.

**Root run 1** put the pads first. The listing the guest got was

```
[probe] raw: __getdirentries64(…/Wayland.backend/Contents, bufsize=4096) = 164
[probe] dirent:   d_name=.              d_fileno=18068539  d_namlen=1   d_type=4
[probe] dirent:   d_name=..             d_fileno=18068538  d_namlen=2   d_type=4
[probe] dirent:   d_name=pad-01.txt     d_fileno=18068540  d_namlen=10  d_type=8
[probe] dirent:   d_name=pad-02.txt     d_fileno=18068541  d_namlen=10  d_type=8
[probe] dirent:   d_name=pad-03.txt     d_fileno=18068542  d_namlen=10  d_type=8
[probe] dirent: 5 entries returned by readdir
[probe] before-load: infoDictionary count = 0
[probe] principalClass = (nil)  <- the wall
```

`.` and `..` first, then the pads, and the call stopped after five records. The
window is the **head** of the listing, not the tail: the handler returns the
first few records and drops the last ones. `Info.plist` was at index 9 and was
never offered.

The evidence for the direction was in the accepted work the whole time and
nobody read it as a direction: `SLOT-344.md` §1's own table has every successful
directory reporting `first record: d_name="."` — index 0 — ten directories out
of ten in this run's log, with no counter-example. "N−7 of N" is true as a count
and was read the wrong way round; the constant is the same, the end is not.

**Root run 2** put `Info.plist` and `MacOS` first and created the pads after
them:

```
[probe] raw: __getdirentries64(…/Wayland.backend/Contents, bufsize=4096) = 160
[probe] dirent:   d_name=.              d_fileno=16374486  d_namlen=1   d_type=4
[probe] dirent:   d_name=..             d_fileno=16374485  d_namlen=2   d_type=4
[probe] dirent:   d_name=Info.plist     d_fileno=16374487  d_namlen=10  d_type=8
[probe] dirent:   d_name=MacOS          d_fileno=16374488  d_namlen=5   d_type=4
[probe] dirent:   d_name=pad-01.txt     d_fileno=16374490  d_namlen=10  d_type=8
[probe] dirent: 5 entries returned by readdir
[probe] before-load: infoDictionary count = 12
[probe] before-load: NSPrincipalClass = WaylandDisplay
[probe] after-load:  infoDictionary count = 12
[probe] principalClass = WaylandDisplay
[probe] principalClass (2nd call) = WaylandDisplay
```

Both byte counts add up to the five records readdir printed — 28+28+36+36+36 =
164 in run 1, 28+28+36+32+36 = 160 in run 2 — so the syscall is handing over
exactly what libc then prints, and libc is not the filter this time: every
`d_fileno` is non-zero, which is the other of `PRINCIPAL-CLASS.md` §5's two
candidates, and it is ruled out.

**Hypothesis (b) is now closed the other way too.** `infoDictionary` has all
twelve keys, including `CFBundleInfoPlistURL`, and `principalClass` answers
`WaylandDisplay` on the first call and the second, so nothing is cached nil.

## 3. The window run, and the next wall

`run-wayland-window-probe.sh`, default path, real seat, preflight **8/8**,
`seat capabilities=3`, the run itself. The old stop is gone — the log has no
`[step 03] FATAL: bundle has no NSPrincipalClass` anywhere in it — and the
program gets as far as instantiating the display:

```
2026-09-29 22:43:03.194 wayland-window-create-macho[10837:0] Terminating app
due to uncaught exception 'NSException', reason: 'Failed to connect to a window
server. Available backends are: <CFArray 0x…>{type = mutable-small, count = 0,
values = ()}'
```

`count = 0` is the whole message: the backend list is empty, so
`+[NSDisplay init]`'s discovery loop never runs once and the exception is raised
on the way out (src/external/cocotron/AppKit/NSDisplay.m:57-91). The discovery
is

```objc
for (NSString *path in [appKitBundle pathsForResourcesOfType: @"backend"
                                                  inDirectory: @"Backends"])
```

and `…/AppKit.framework/Versions/C/Resources/Backends/` holds, counting `.` and
`..`:

```
.  ..  Wayland.backend          -- three entries, below the line
```

Same wall, same constant, one directory up, and it is reached by the same
`_CFIterateDirectory` family of calls. §4 is the branch that pads it.

## 4. `Backends/` padded: the count is fixed, and it is still `count = 0`

Two root runs, the budget the наряд allowed: the bundle probe as a control,
then the full window run.

**Control (root 1).** The `Contents/` layout from §2 is untouched and still
works — `infoDictionary count = 12`, `NSPrincipalClass = WaylandDisplay`,
`principalClass = WaylandDisplay` on both calls — which is what the control was
for: the `Backends/` change must not have broken the thing that already worked.
The `offsetof` fix from §5 also shows up here for the first time: the probe now
prints `raw: 5 record(s)`, where before the fix it printed `0 record(s)` beside
the same 160 bytes.

**The layout.** `fill-bundle-contents.sh backends` pads
`Resources/Backends/`, moving `Wayland.backend` aside and putting it back before
the pads are created so it stays at index 2:

```
.  ..  Wayland.backend  pad-01.txt … pad-07.txt      -- 10 entries
```

which is the window the guest is handed, by the rule §2 measured:

```
.  ..  Wayland.backend  pad-01.txt                   -- 4 records
```

`Wayland.backend` **is inside it.** The pad names are `pad-NN.txt`, so nothing
is filed under the query-table key `_CFBT_.backend`
(`_CFBundleAddValueForType`, CFBundle_Resources.c:427) and no pad is offered to
NSDisplay as a backend. Both hashes are unchanged:
`f8c50da1…` for `Info.plist`, `a4797cdd…` for the backend, the latter still
equal to `tests/vendor`.

**The window run (root 2) says the same thing: `count = 0`.**

```
2026-09-29 23:05:50.745 wayland-window-create-macho[57985:0] Terminating app
due to uncaught exception 'NSException', reason: 'Failed to connect to a window
server. Available backends are: <CFArray 0x…>{type = mutable-small, count = 0,
values = ()}'
```

Preflight 8/8, `seat capabilities=3`, the same exception as §3 with the same
count. So the eight-entry wall is **no longer what is stopping this**, and the
`Backends/` padding, while correct, is not sufficient. That is the finding; the
rest of this section is why, and it is read off the source and the on-disk
layout rather than out of a run.

### Why the count is not the only thing between here and the window

`Resources` is a **symlink**, and that is a second wall one step further on,
in the same call:

```
AppKit.framework/Resources -> Versions/Current/Resources
AppKit.framework/Versions/Current -> C
```

Two of the frameworks in this overlay have a symlinked `Resources` and none has
a real one, `AppKit.framework` among them. Every step of the discovery walks
through that symlink:

1. `-[NSBundle pathsForResourcesOfType:inDirectory:]` →
   `_CFBundleCopyFindResources` → `_CFBundleCopyQueryTable` →
   `_CFBundleCreateQueryTableAtPath` → `_CFBundleReadDirectory` →
   `_CFIterateDirectory(Resources/Backends)`.
2. The resource directory for a version-0 framework is the bare string
   `Resources` (`_CFBundleGetResourceDirForVersion`,
   CFBundle_Resources.c), appended to the bundle base path — so the directory
   CF opens is `AppKit.framework/Resources/Backends`, and `opendir` on it has
   to resolve two symlink hops to reach the real directory.
3. The layout version that decides all of this is itself found by a
   `_CFIterateDirectory` over the framework root, looking for `Resources`,
   `Contents` or `Support Files` and matching on `DT_DIR` **or `DT_LNK`**
   (CFBundle_Resources.c:255-266). `AppKit.framework/` holds three entries —
   `AppKit`, `Resources`, `Versions` — five with `.` and `..`, which is **below
   the eight-entry line**. So that scan returns nothing, `foundResources` is
   never set, and `localVersion` stays at its initialiser.

Step 3 is the one that bites, and it is the *same* wall seen from a different
side: the framework's own root is a four-entry directory, so the code cannot
even determine which layout the framework uses. `DT_LNK` is explicitly allowed
in that comparison, so a symlink would have been accepted had the listing
reached it — which also says the fix is not "teach it about symlinks".

Both of these are guesses about which of the two is fatal first, and I am not
able to separate them without another run, which the наряд did not fund. What
is **not** a guess: the count in `Backends/` is now above the line with
`Wayland.backend` inside the window, the same window rule that fixed
`Contents/`, and the exception is unchanged. Whatever is stopping the window is
downstream of the directory count.

### What the next наряд should do, cheapest first

- **Reuse the fixture, move it up one level.** `AppKit.framework/` itself needs
  seven pads (`fill-bundle-contents.sh` with a new target) so the layout scan
  at CFBundle_Resources.c:255 can see `Resources` at all. This is the same
  three lines of shell, it is the directory the framework *root* scan reads,
  and by the argument above it must return entries before anything downstream
  can. Pad names must not collide with `Resources`, `Contents` or
  `Support Files`.
- **Then, if `count = 0` persists, the symlink.** Making `Resources` a real
  directory (or a hardlink to one) in the overlay removes the resolution step
  entirely. That is a bigger change to a shipped tree and wants its own наряд
  and its own rollback story, which is why it is not done here.
- **A cheaper probe than either**: the existing bundle probe already prints
  names; adding one `opendir` of `AppKit.framework/` and one of
  `AppKit.framework/Resources/Backends` would show in a single run which of
  the two directories the guest can list, and that is the question both of the
  above are asking. This is the first thing to do, because it costs one
  non-seat probe run and answers the question both fixes are aimed at.

## 5. The fixture, and why the pads come last

`build-freebsd/fill-bundle-contents.sh`, in the overlay, because the overlay is
the only place the guest can see a fixture from. It takes `Info.plist` and
`MacOS` and any earlier pads out of `Contents/`, puts the first two back, then
creates seven `pad-NN.txt` files. Result:

```
.  ..  Info.plist  MacOS  pad-01.txt … pad-07.txt     -- 11 entries
```

`Info.plist` is at index 2, so it survives any window of three records or more;
the smallest directory that answers at all (`t8`, eight entries) returns one, and
every directory measured here returned four or five. More pads do not buy
anything, since they land after the entries that matter.

The pad names are ten characters and match neither `Info.plist` nor
`Info-macos.plist` in any case, so `_CFIterateDirectory` (CFBundle_InfoPlist.c:
490-517 — full-length, anchored, case-insensitive only) matches them against
nothing. `Contents/MacOS/Wayland` is moved aside and put back, and the script
compares the sha256 of `Info.plist` and of the backend before and after, and the
latter against `tests/vendor/wayland-backend/Wayland`, so a mistake here cannot
turn the window probe's vendored-hash gate into a second and less informative
failure. The backend hash is `a4797cdd…` before and after, which is the value
the probe's own gate expects.

## 6. One thing in the log is a lie, and it was the probe's

Both runs print

```
[probe] raw: 0 record(s), 0 with d_fileno == 0, 0 with a bad d_reclen
```

beside 164 and 160 bytes that demonstrably held five records each. That is not
the guest: the loop that counts records
(`tests/src/bundle-principal-class.m:279`) used `sizeof(struct dirent)` as its
lower bound, and the guest's `dirent.h` sizes `d_name` for `NAME_MAX`, so the
bound is about a kilobyte, larger than any buffer the probe passes, and the loop
body never ran once. The bound is now `offsetof(struct dirent, d_name)`, which
is what "is there a record header here" actually means. **The probe builds with
that change; it has not been run, so the raw lines in §2 are from the previous
binary and will read differently next time.**

## 7. What is not settled

- **The dropped count is not 7 here.** `Contents` has eleven entries and got
  five, in both runs — six dropped. The ten `t` fixtures, whose names are all
  six characters, drop exactly seven every time. So "N−7" describes the
  fixtures and is off by one on this directory, and I did not spend a run
  separating the two. The fixture only depends on the *direction*, and the
  direction is not in doubt: the first record is `.` in ten directories out of
  ten, and every `d_fileno` is non-zero.
- **Real large directories still cannot be used to pin the count.** Their
  listing order in the guest need not be the host's, so summing record sizes
  against a returned byte count does not close — `/usr/lib` (146 entries, 392
  bytes) and `…/Frameworks` (62, 344) both land far from any prefix sum. The
  eight-entry threshold itself is unaffected; it is the residual count that is
  open.
- **`Backends/` is padded and `count = 0` persists.** §4. The count is no
  longer the blocker; whether the framework-root scan or the `Resources`
  symlink is fatal first is **not separated**, and separating them costs one
  probe run that the наряд did not fund. §4 says which question to ask first.
- **The mechanism is still a lead.** "Returns the first few records and drops
  the last few" is now measured in both directions, which is more than
  `EMU-344.md` §5 had, but nothing in reach explains the seven.
- **The number dropped is not a constant.** `Contents` drops six of eleven and
  the `t` fixtures drop seven of eight-to-fourteen, and `Backends/` drops six
  of ten — the `N−7` of `SLOT-344.md` §1 fits the fixtures and is off by one
  on both real directories. Only the *direction* is relied on, and the
  direction is measured, not assumed.
- Nothing in `src/` is changed by this branch. The workaround is a directory
  layout in the overlay, and it is a workaround: Chrome will hit the same wall
  on its own bundles, whose `Contents/` and `Backends/` we do not control.

## 8. Reproduce

```sh
export DARLING_SRC_DIR="$PWD"                       # this checkout
export DARLING_OVERLAY=/path/to/overlay             # the overlay you build against
export DARLING_BUILD_DIR=/path/to/build             # scratch, outside the source tree

sh build-freebsd/fill-bundle-contents.sh            # Contents/ layout + both hashes
sh build-freebsd/fill-bundle-contents.sh backends   # Backends/ layout, §4
sh build-freebsd/fill-bundle-contents.sh remove contents   # and back to 4 entries
sh build-freebsd/fill-bundle-contents.sh remove backends   # and back to 3

sh build-freebsd/build-bundle-principal-class-test.sh
DRY_RUN=1 sh build-freebsd/run-bundle-principal-class.sh   # RC=0, 7 checks, no root
sh build-freebsd/run-bundle-principal-class.sh             # root run
grep '^\[probe\] dirent\|^\[probe\] before-load\|^\[probe\] principalClass' \
    "$DARLING_BUILD_DIR/bundle-principal-class.log"

DRY_RUN=1 sh build-freebsd/run-wayland-window-probe.sh     # RC=0, 8 checks, needs sway
sh build-freebsd/run-wayland-window-probe.sh               # root run, a seat
grep 'Available backends\|no NSPrincipalClass' \
    "$DARLING_BUILD_DIR/wayland-window-probe.log"
```

Logs stay in `$DARLING_BUILD_DIR`, outside the source tree, and are machine
artefacts full of addresses; they are quoted here, not committed.
