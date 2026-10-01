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

## 5. Separating §4's two hypotheses: one is confirmed, and there is a third wall

Two root runs, and the first was spent on my own bug — see the end of this
section. The second answers the question, and the answer is not the one §4
picked.

The probe now reads four directories and asks the API question directly
(`tests/src/bundle-principal-class.m`, `probe_framework_root`):

```
[probe] rootscan: framework root (the directory the layout scan reads)
        -- /System/Library/Frameworks/AppKit.framework
[probe] rootscan:   0 entries returned by readdir  <- EMPTY from the guest's side
[probe] rootscan:   NO: the layout detector's scan CANNOT  see Resources, Contents or Support Files
[probe] rootscan: same root via Versions/C
[probe] rootscan:   0 entries returned by readdir  <- EMPTY from the guest's side
[probe] rootscan: Resources/Backends (the directory the backend list comes from)
[probe] rootscan:   opendir FAILED (errno 2: No such file or directory)
[probe] rootscan: Resources/Backends via Versions/C
[probe] rootscan:   d_name=.                d_fileno=8187284  d_namlen=1   d_type=4
[probe] rootscan:   d_name=..               d_fileno=8187275  d_namlen=2   d_type=4
[probe] rootscan:   d_name=Wayland.backend  d_fileno=8187285  d_namlen=15  d_type=4
[probe] rootscan:   d_name=pad-01.txt       d_fileno=8187346  d_namlen=10  d_type=8
[probe] rootscan:   4 entries returned by readdir
[probe] rootscan: pathsForResourcesOfType:@"backend" inDirectory:@"Backends" -> 0 path(s)  <- the wall
[probe] rootscan:   -> 0 path(s) on the second call
```

**§4's first hypothesis is confirmed.** The framework root answers zero
entries, through both the plain path and the `Versions/C` alias, so
`foundResources` is never set and the layout is never determined. The padding
this branch's наряд asked for is a real fix for a real wall.

**The second hypothesis was right too, and worse than §4 thought.** §4 put the
`Resources` symlink down as a *candidate* for the same wall. It is not a
candidate, it is a hard failure, and it is not in the emulation:
`opendir(AppKit.framework/Resources/Backends)` returns **ENOENT** — the path
does not exist in the guest at all — while the same directory reached through
`Versions/C` lists four entries with `Wayland.backend` in the window, exactly
as §4's fixture predicted.

The reason is the harness, and it is one line of shell
(`tests/launch-dynamic-smoke.c:76-80`):

```c
snprintf(cmd, sizeof(cmd),
         "mkdir -p '%s' && cd '%s' && find . -type f | pax -rw '%s'",
         dst, src, dst);
```

`find . -type f` emits **regular files only**. The guest's root is not the
overlay but the staged copy — `od = LOCAL_OVERLAY` at
`launch-dynamic-smoke.c:427` — and that copy therefore contains **no symlinks
at all**. Measured, not inferred:

| | symlinks |
|---|---|
| `$DARLING_OVERLAY/System/Library/Frameworks` (maxdepth 2) | 54 |
| `/tmp/darling-local-overlay` (whole tree) | 0 |

And the guest's own inodes place it in the staged tree, not the overlay:
`..` came back as `d_fileno=8187275`, and `8187275` is an inode of
`/tmp/darling-local-overlay/…` (the overlay's corresponding inode is
`5222547`). So `AppKit.framework/Resources` is not unresolvable in the guest —
**it was never copied.**

`find -type f` is not an oversight. The comment above `stage_tree` says why:
`cp -a` and `cp -RL` walk symlinks, the overlay's virtiofs returns a malformed
`FUSE_READLINK` reply with an embedded NUL, and every symlink walk then fails
with `EIO` while leaking a `fuse_msgbuf` per link — enough of them to OOM the
guest unrecoverably. Dropping symlinks was the deliberate fix. The consequence
that was not on anyone's list is that the guest's root loses 54 framework
symlinks, and `Resources` is one of them.

### Why the наряд's fix is not enough, and would have been a wasted run

Padding the framework root makes §5's first wall go away, and then CF
determines layout 0 and builds its resource path as `<base>/Resources` +
`/Backends` — the path that returns `ENOENT`. So window run №7 would have
answered "necessary but not sufficient" a second time, for a reason found on
the host before spending the root.

The three walls, in the order the guest meets them:

1. **Framework root, 5 entries < 8** → 0 records → layout undetermined.
   Fixable by the fixture. Confirmed here.
2. **`Resources` is a symlink, and the staged tree has no symlinks** →
   `ENOENT`. Not fixable by any directory layout; the link has to be copied,
   or replaced by a real directory.
3. Only then, `Resources/Backends` — already above the line and holding
   `Wayland.backend` in the window since §4.

**No window run was spent on this branch** (see below), and the framework-root
fixture mode is *not* in it either: a fixture that fixes one of three walls
would be a branch whose only demonstrated effect is a different exception, and
the наряд that asked for it was written when two walls were known rather than
three. The honest next step is wall 2, because it is the one that no amount of
directory padding reaches, and it is a harness change with a known hazard
(the FUSE bug) attached to it.

### What it cost, and my own error

The first of the two roots was wasted on a bug in the separator itself: it
called `+[NSBundle bundleWithClass:]`, which this Foundation does not
implement. That returned nil, and the next line sent a message to the nil
bundle, and the guest died with `SIGSEGV` at `0xff0a0000` after printing a
single diagnostic line. The framework root is now derived by cutting
`/AppKit.framework` off the backend path the probe already opens, and the API
half goes through `bundleWithPath:`, which the rest of the file uses and which
returns. A nil from either is printed, and the calls behind it are skipped
rather than sent to nil.

A separator that dies before it separates anything is the most expensive kind
of wrong code, and the наряд's whole point was that this question was cheap to
answer. It was cheap; I made it expensive.

## 6. The fixture, and why the pads come last

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

## 7. One thing in the log is a lie, and it was the probe's

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

## 8. What is not settled

- **The line is in bytes, and how many records come back is not a rule.**
  §9 settles the first half — `t7` and `l7` have the same seven entries and
  opposite verdicts, so the threshold counts bytes, and "eight entries" was an
  artifact of six-character names. The second half is open: the dropped tail
  is 7, 7, 6, 6, 3, 4 across six directories, and the two long-name ones
  differ by an entry and return the same 616 bytes. No model, and no run
  bought to look for one.
- **Real large directories still cannot be used to pin the count.** Their
  listing order in the guest need not be the host's, so summing record sizes
  against a returned byte count does not close — `/usr/lib` (146 entries, 392
  bytes) and `…/Frameworks` (62, 344) both land far from any prefix sum. The
  eight-entry threshold itself is unaffected; it is the residual count that is
  open.
- **`Backends/` is padded and `count = 0` persists.** §4. The count is no
  longer the blocker. §5 separates the rest: the framework-root scan is
  confirmed dead at zero records, and the `Resources` symlink is confirmed
  absent from the guest's root, with the harness line that drops it named.
  **Neither is fixed** — the root by a fixture that this branch deliberately
  does not contain, the symlink not at all.
- **§5's two walls are both closed** and §10 measured it. The order the guest
  met them in turned out not to matter: fixing the symlink is what made the
  discovery return a path, and the framework root's window is still short
  enough that `Resources` is not in it — see the symlink-entry finding, which
  is what the root scan is now actually tripping over.
- **The mechanism is still a lead.** "Returns a prefix of the listing" is now
  measured in both directions and against a byte threshold (§9), but nothing
  in reach explains how long the prefix is.
- **The window itself is still not on screen.** Four walls are down and a
  `WaylandWindow` object exists, but the shm pool fails (§10). §11 corrects
  §10's diagnosis of that failure and reopens it: the name was patched and the
  failure did not move, and the guest's `shm_open` answers `EINVAL` for every
  name including a control. Nothing here is a claim that a frame is drawn.
- **The fifth wall is diagnosed and the fix is written** (§12, §13), as a patch
  against the `darling-xnu` submodule rather than a commit, because this
  repository does not contain those files and does not build the emulation.
  **Not built and not run** — see §13 for the four reasons, all of them about
  where the code lives rather than about the budget.
- **"Symlink entries vanish from listings" (§10) was a misdiagnosis**, and §12
  retracts it: the links are real and last in the listing, because this
  branch's own transfer runs after the regular-file pass. Nothing is wrong
  with symlink entries; the order of the two staging passes is.
- **`SLOT-344.md` §1's "N−7" is wrong twice over**, and §9 is the correction:
  the threshold is a byte total, and the dropped tail is not seven. Only the
  *direction* is relied on by any fixture here, and the direction is measured.
  That is also why §4's `Backends/` prediction was a prediction and not a
  guarantee: the window there is 4 records of 10 entries, and one fewer
  dropped would have cut `Wayland.backend` out.
- Nothing in `src/` is changed by this branch. The workaround is a directory
  layout in the overlay, and it is a workaround: Chrome will hit the same wall
  on its own bundles, whose `Contents/` and `Backends/` we do not control.

## 9. The threshold is a BYTE total, and "eight entries" was a coincidence

The one open remainder of `SLOT-344.md` §5 was whether the line is eight
entries or something that lands on eight when the names are six characters.
The ten `t` fixtures cannot answer it: they vary the count and hold the name
length fixed, so a byte rule and a count rule fit them equally well. Two more
directories, same counts, names of 254 characters:

```
l7   5 files, 7 entries counting . and .., 1456 bytes total
l8   6 files, 8 entries counting . and .., 1736 bytes total
t7   5 files, 7 entries,                          216 bytes total
t8   6 files, 8 entries,                          248 bytes total
```

One root run, no seat, the same `getdirentries` sweep:

```
t7  -> -1  errno=22 (Invalid argument)      7 entries,  216 B
t8  ->  28  errno=0                        8 entries,  248 B
l7  -> 616  errno=0                        7 entries, 1456 B
l8  -> 616  errno=0                        8 entries, 1736 B
```

**It is a byte total, and not a count of entries.** `t7` and `l7` hold the
same seven entries and get opposite verdicts — one `EINVAL`, one 616 bytes of
records — and the only thing that differs between them is how many bytes those
entries occupy. A count rule cannot produce that pair.

Read against the fixtures the line sits between **216 and 248 bytes**, which
is exactly the window where "8 entries" appeared: with six-character names a
record is 32 bytes, so eight entries is 248 and seven is 216. "Eight" was never
about eight. Every fixture laid out on the count — including this document's
own, and the reason the first `Contents/` layout returned zero of what it
needed — was laid out on a coincidence of the name length.

**This does not change the fixture that works.** The rules the fixtures here
rely on are both *lower* bounds in the same direction: a directory has to be
big enough in entries to clear the byte line, and the wanted entry has to be
early in the listing. Padding is still the right move, and `Contents/` and
`Backends/` are still correctly laid out. What changes is the reason, and the
reason is what tells you how much padding is *needed*: by bytes, not by
count, so a directory of very long names clears the line with fewer entries
than a directory of short ones. `l7` clears it with seven.

### How many records come back, which is still not a rule

| directory | entries | total | returned | records | dropped |
|---|---|---|---|---|---|
| `t8` | 8 | 248 B | 28 B | 1 | 7 |
| `t14` | 14 | 440 B | 216 B | 7 | 7 |
| `Contents` | 11 | 376 B | 160 B | 5 | 6 |
| `Backends` | 10 | 348 B | 132 B | 4 | 6 |
| `l7` | 7 | 1456 B | 616 B | 4 | 3 |
| `l8` | 8 | 1736 B | 616 B | 4 | 4 |

This is the part I cannot close, and it is now visibly not one rule. The
dropped count is 7, 7, 6, 6, 3, 4 — it is neither constant nor a function of
the entry count, and the long-name pair adds the awkward case: `l7` and `l8`
differ by one entry and by 280 bytes, and return **the same 616 bytes**, which
is 4 records both times. A rule of the form "drop the last N" is out, and so is
"return the first N−k". I have one more observation and no model for it, and a
run to find the next observation is not what this наряд bought, so it is left
open rather than guessed at.

What survives as a law, and what the fixtures depend on, is unchanged and now
better supported than before: **the records come from the head of the listing**
— every one of these runs prints `d_name="."` as its first record — and a
directory must be above the line in bytes to answer at all.

## 10. Both walls closed, the window is built, and the fourth wall is the shm pool

Two root runs. The first checks the two fixes and the control; the second is
the full window run.

### The fixes

**The symlinks.** `launch-dynamic-smoke.c` now carries them across: each link is
found with `lstat`, read with a single `readlink` into a buffer we own, and
recreated with a single `symlink`. Not handed to a recursive copier — the
reason the old form was abandoned is recorded three functions above, and
calling `readlink` exactly 54 times is not that failure.
`DARLING_STAGE_SYMLINKS=0` reverts the whole thing to regular files only.

**The framework root.** Seven pads after the existing entries, nothing moved,
so `AppKit`, `Resources` and `Versions` keep their places at the front.

### Root 1 — both walls gone, control intact

```
[probe] rootscan: framework root ... d_name=. / .. / Versions / pad-01.txt /
                pad-02.txt / pad-03.txt        6 entries returned by readdir
[probe] rootscan: Resources/Backends ... d_name=. / .. / Wayland.backend /
                pad-01.txt                     4 entries returned by readdir
[probe] rootscan: pathsForResourcesOfType:@"backend" inDirectory:@"Backends"
                -> 1 path(s)
[probe] rootscan:   /System/…/AppKit.framework/Resources/Backends/Wayland.backend
[probe] before-load: infoDictionary count = 12
[probe] principalClass = WaylandDisplay
```

`ENOENT` is gone, the root answers where it answered nothing, the discovery
returns the backend it wants, and `Contents/` still works.

### Root 2 — window run №7: the exception is gone

```
[step 02] bundle path = /System/…/Backends/Wayland.backend
[step 03] NSDisplay currentDisplay (discovers + inits the backend itself)
[step 04] newWindowWithDelegate: nil -> WaylandWindow
[step 05] _acquireBackBufferForWidth:640 height:480 (reaches
         _wayland_window_create_shm_fd)
[step 06] RESULT: no buffer (record=0x0 buffer=0x0 pixels=0x0)
         -- shm allocation did NOT succeed.
```

The registry enumerates fifteen globals — `wl_shm`, `wl_compositor`,
`xdg_wm_base`, `zwp_layer_shell_v1` among them — so the display is talking to
sway, and `[step 04]` means a `WaylandWindow` exists. Four walls are behind us:
`Info.plist` not found, `Backends/` empty, the framework root unscannable, and
`Resources` unopenable. **The fourth wall is the shm pool**:

```
WaylandWindow: shm allocation failed for 640x480 buffer: Invalid argument
```

> **§11 corrects the last paragraph of this one.** The reasoning below — the
> name is invalid per POSIX, the host rejects it identically, therefore the
> `EINVAL` is ours and not the emulation's — is **wrong about the guest's**, and
> the patch it led to did not clear the wall. The name really was invalid and
> the patch is still worth keeping; the inference from "the host rejects it" to
> "the guest rejects it for the same reason" does not hold, because the guest's
> `shm_open` is not the host's. Read §11 before acting on this section.

with the name the backend builds being `./.bsdos-wlshm-%d-%d`, visible in the
binary's own strings. `EINVAL`, not `ENOENT`, and a name like that is the
classic POSIX-shm violation: the name must be one leading slash followed by
non-slash characters, and `./.bsdos-wlshm-0-1` has two.

Proved on the host, no root and no guest, in four lines of C:

```
shm_open("./.bsdos-wlshm-0-1") = -1  errno=22 (Invalid argument)
shm_open("/.bsdos-wlshm-0-1")  =  3  errno=0
shm_open("bsdos-wlshm-0-1")    = -1  errno=22 (Invalid argument)
shm_open("/bsdos-wlshm-0-1")   =  3  errno=0
```

Native FreeBSD, native libc, and the same `EINVAL` the guest produced. **This
wall would fail identically on a machine with no emulation in it at all.** Four
directories' worth of fixtures, a symlink transfer and a framework root bought
a window; the last thing between here and a frame looked like a name our own
backend built wrong.

> The bolded sentence above is where this section goes wrong, and §11 is the
> measurement that shows it. "This wall would fail identically on a machine
> with no emulation in it" is true of *the host's* `shm_open` and was
> over-generalised to *the guest's*. The guest's is a different function that
> fails for every name, so the name was never the thing being rejected. The
> second sentence of that paragraph is wrong for the same reason: what is
> between here and a frame is not the name.

**The fix is one byte, and it is not mine to make.** The string sits at offset
`0x8b3c` in `tests/vendor/wayland-backend/Wayland`; replacing the leading `.`
with `/` is a same-length edit that changes exactly one byte and moves nothing,
giving sha256 `b918e22e77875289ac6a730fba27d39cddc3e36a6234916de2d2c363897e73da`.


It is not in this branch because the decision is not a technical one. That
binary's sources **no longer exist anywhere** — the README in
`tests/vendor/wayland-backend/` records the search, and it is the only copy of
the artifact. Re-blessing its hash is re-blessing something we cannot rebuild,
and three scripts pin `a4797cdd…` as the committed value. That is the head's
call and the owner's if it comes to that, not a side commit in a наряд about
symlinks. Recorded here so the decision can be made with the offset and the new
hash in front of it.

### One more thing the run turned up, and did not need a run to notice.
**§12 retracts the reading of this: the symlinks are not missing from the
listing, they are at the end of it.** The observation below stands; the
explanation offered with it does not.

The guest's listing of the framework root came back as `.`, `..`, `Versions`,
`pad-01`, `pad-02`, `pad-03` — **`AppKit` and `Resources` are missing from it**,
and both are symlinks that are present in the staged tree (`ls -la` on the
staged root shows them). So symlink *entries* do not survive into the listing,
even though a symlinked *path* now opens. The two indistinguishable causes are
that the emulation omits them, and that it delivers them with a zero inode for
libc's `readdir` to drop (`if (dp->d_ino == 0 && skip) continue;`,
readdir.c:118) — the same two candidates `PRINCIPAL-CLASS.md` §5 named, one
level up. The raw `__getdirentries64` probe would separate them in any future
run; it is not run here because the run's own budget was spent.

Discovery worked anyway, so this is not currently blocking, and that is luck
rather than design: the layout scan saw `Versions` and no `Resources`,
determined some layout, and the fallback in `_CFBundleCopyFindResources`
("Assume no resources directory", CFBundle_Resources.c) found the path anyway.
**That fallback is load-bearing on a coincidence and should not be leaned on.**

### Is a message to nil safe here? Answered in the same run

Four messages to a nil receiver, varying the return type — `id`, `NSInteger`,
a real `NSRange` struct return, and `+alloc` through a nil `Class` — all
returned nil/0 and none faulted. **The nil check exists.** The `SIGSEGV` at
`0xff0a0000` in §5 was a bug in the separator, which sent a message to the nil
bundle `+[NSBundle bundleWithClass:]` had returned, and not a property of this
runtime. The wall-sized claim is refuted by measurement, which is the only way
it should have been refuted: had it been true, every idiomatic `[nil whatever]`
in Chrome would crash, and the finding would have been worth a very different
response.

## 11. The name was patched, the wall stayed — and the name was never the thing

The byte was blessed, applied, and every pin updated. Window run №8, preflight
8/8 with the new hash gate passing (`backend sha256 b918e22e… matches vendored,
overlay and the committed hash`). The patched binary really was the one the
guest loaded — the staged copy's sha256 is `b918e22e…` and its string reads
`/.bsdos-wlshm-%d-%d` — and the answer was the same sentence:

```
[step 04] newWindowWithDelegate: nil -> WaylandWindow
[step 05] _acquireBackBufferForWidth:640 height:480
WaylandWindow: shm allocation failed for 640x480 buffer: Invalid argument
[step 06] RESULT: no buffer (record=0x0 buffer=0x0 pixels=0x0)
```

So §10's diagnosis was wrong, and the error was in the reasoning rather than in
the measurement it rested on. The name really is invalid per POSIX and the host
really does reject it — both of those stand. What does not stand is the step
from "the host rejects this name" to "the guest's `EINVAL` is this same
rejection". **The guest's `shm_open` is not the host's function.**

Asked directly, in the guest, with names chosen to differ in every way that
could matter:

```
[probe] shmtest: shm_open("/.probe-shm-plain")         = -1 errno=22 (Invalid argument)
[probe] shmtest: shm_open("/probe-shm-no-slash")       = -1 errno=22 (Invalid argument)
[probe] shmtest: shm_open("probe-shm-relative")         = -1 errno=22 (Invalid argument)
[probe] shmtest: shm_open("/.bsdos-wlshm-0-1")          = -1 errno=22 (Invalid argument)
[probe] shmtest: shm_open("/tmp/probe-shm-in-tmp")     = -1 errno=22 (Invalid argument)
[probe] shmtest: shm_open("/no-such-dir-xyzzy/probe")  = -1 errno=22 (Invalid argument)
[probe] shmtest: control, a bare valid name            = -1 errno=22 (Invalid argument)
```

**Every name fails, including a control that is valid everywhere.** Leading
slash or not, a directory that exists or not, the patched name or the original
— identical `EINVAL`. The name is not the variable. The call is.

And the disassembly says what the call is. `shm_open` is exported by
`libsystem_kernel.dylib` (`_shm_open` at `0x44f20`), and its syscall half
(`_sys_shm_open` at `0x66940`) is:

```
_sys_shm_open:
        callq   _oflags_bsd_to_linux      ; translate the flags
        movq    _elfcalls(%rip), %rax
        movq    0x88(%rax), %rax          ; a syscall slot out of elfcalls
        callq   *%rax                     ; …and call it with (name, flags, mode)
```

It is a thin `open(2)` on the name. There is no `/dev/shm` in the shim, and
there is none in the overlay either — the earlier check for it in
`/tmp/darling-local-overlay/dev/` came back *No such file or directory*. So this
is not a POSIX shm implementation with a broken name check; it is a name handed
to the emulation's `open`, and something on that path answers `EINVAL` for
everything.

**What this costs the byte patch.** Nothing, and it should still be kept: the
name was genuinely invalid, a native build of these sources would have failed
on it, and the corrected name is right. But the patch was not what unblocked
the window, and the record must not say it was. If the sources are ever
recovered, the fix belongs in `WaylandWindow.m` — and the vendored README now
says so, and records the offset, both hashes and this outcome.

**Where the fifth wall actually is:** the emulation's `open`, or the flag
translation in front of it. `oflags_bsd_to_linux` is the narrower suspect,
because `EINVAL` is a documented result of an invalid flag set and the flag
translation is the one place the guest rewrites what it is about to pass down.
That is a lead, not a measurement — separating them needs a run that calls
`open` with and without each flag, which this наряд's budget did not buy, and
`open` is not the only possibility: the `elfcalls` slot at `0x88` may be null or
wrong for this entry, which would also surface as a bad call rather than a
rejected name.

## 12. Where the `EINVAL` is born: the flags are converted twice

`shm_open` is wrapped at
`src/external/xnu/darling/src/libsystem_kernel/emulation/src/xnu_syscall/bsd/impl/wrapped/shm_open.c:20`:

```c
ret = elfcalls()->shm_open(name, oflags_bsd_to_linux(oflag), mode);
```

and `elfcalls()->shm_open` is **the host's** `shm_open` — mldr fills that table
from the host's own symbols at
`src/startup/mldr/elfcalls/elfcalls.c:118`, into the slot declared at
`src/startup/mldr/elfcalls/elfcalls.h:51` (which is at `+0x88` in the struct,
which is the offset the disassembly in §11 was reading).

So the flags are translated BSD → Linux and then handed to a function that
expects BSD. **They are converted once too many**, and the caller's `O_CREAT`
becomes a bit the host reads as `O_ASYNC`.

### The host proves it, with the guest's own numbers

`oflags_bsd_to_linux` is a plain bit remap
(`…/conversion/fcntl/open.c:7`), so what arrives at the host is arithmetic — and
arithmetic is testable without a guest. Four rows, the value the guest passes
and the value its wrapper produces for it:

| guest passes | wrapper sends to the host | host answers | **guest answered** |
|---|---|---|---|
| `0x202` (BSD RDWR\|CREAT) | `0x042` | `EINVAL` | **`EINVAL`** |
| `0xa02` (BSD RDWR\|CREAT\|EXCL) | `0x0c2` | `EINVAL` | **`EINVAL`** |
| `0x002` (BSD RDWR) | `0x002` | `ENOENT` | **`ENOENT`** |
| `0x0c2` (already Linux) | `0x002` | `ENOENT` | **`ENOENT`** |

Four for four, on native FreeBSD with native libc and no emulation in the
picture. The guest is not adding anything of its own to this call; it is
passing its arguments to the host's function and the host is answering exactly
as the host answers.

**H1 survives and is now located at a line. H2 and H3 are refuted.** The slot is
not a stub — the source names the host's real `shm_open`, and the four-row match
is the proof that dispatch reaches it. Nothing about path semantics is
involved: the name is irrelevant (that was §11's measurement) and the
emulation's `open` is never entered, because the call never gets that far.

**My prediction for this run was backwards, and the run is more useful for
having been wrong.** I wrote that the BSD rows would succeed and the
already-translated ones would fail. What happened is that **no row can
succeed**: every value the caller can pass loses its `O_CREAT` on the way
down, so the file is never created and the call is refused or reports it
absent. There is no caller-side workaround, which is the part that matters for
what happens next.

### The same line of code is wrong in two more places

Not one call, a pattern. `sem_open` has it verbatim
(`…/impl/wrapped/sem_open.c`):

```c
ptr = elfcalls()->sem_open(name, oflags_bsd_to_linux(oflag), mode, value);
```

and the probe's control call — chosen precisely to prove the table was alive —
came back `EINVAL` too, which is what a shared defect looks like. **That was a
badly chosen control** and I should say so: `sem_open` shares the defect, so it
proves nothing about liveness. What proves liveness is the four-row match above,
which cannot happen unless the call is really being made.

And in `_open_for_libelfloader`
(`…/linux_premigration/ext/for-libelfloader.c:18-19`) the same expression is
assigned to the wrong variable altogether:

```c
linux_flags = oflags_bsd_to_linux(flags);
wd          = oflags_bsd_to_linux(flags);   /* should be get_perthread_wd() */
ret = LINUX_SYSCALL(__NR_openat, wd, path, linux_flags, mode);
```

`wd` is the `dirfd` argument of `openat(2)`, and it is being given the open
flags. The function two lines above it in the same file,
`_access_for_libelfloader`, does it correctly with `get_perthread_wd()` — so
the correct form is in the file, next to the wrong one.

**Not fixed in this branch.** The наряд says the found source is its own task,
and it is right: these are in the emulation layer, three call sites and a
shared helper, and a fix belongs where someone can run the emulation's own
tests. What this section hands over is the line, the arithmetic, and a
host-side reproduction that costs four lines of C and no root.

### The rider: "symlink entries vanish from listings" was a misdiagnosis

§10 filed this as its own finding and it became its own task. It is wrong, and
the same run that settled the flags settles this.

The guest listed the framework root as `. .. Versions pad-01 pad-02 pad-03` —
six entries. The host lists that same directory — the staged tree — as ten,
and the two missing ones are at the **end**:

```
.            1043980
..            963384
Versions     1043982
pad-01.txt   1043984   … pad-07.txt 1043990
AppKit       1044051   -> Versions/Current/AppKit
Resources    1044052   -> Versions/Current/Resources
```

Both symlinks resolve, both are on disk, and their inodes are the two highest
in the directory. **Nothing is vanishing.** The guest returned the first six
records of a ten-entry listing, in order, exactly as §9's head-window rule says,
and the links are at indices 8 and 9.

They are last because **this branch's own symlink transfer runs after the
regular-file pass** (`launch-dynamic-smoke.c`: `stage_tree` then
`stage_symlinks`, in that order), so every link is created after every
regular file and lands at the tail of the listing — outside the window. The
`framework-root` pads cannot help, for the same reason: they are created in the
overlay, and the staging re-orders anyway, putting the links last regardless of
what the overlay says.

So this is the same wall as everything else in this document, with a different
victim, and it was introduced by the fix for the previous one. The observation
is real and the explanation was not: there is nothing wrong with symlink
entries, and there is something to fix about the order of the two staging
passes. Whether to reorder them so links are created first, or to pad further,
is a decision for the task that owns the symlink transfer, not a rider.

## 13. The fix, and why it is a patch file rather than a commit

The three lines are small and the diagnosis is settled, so the work was writing
them. What came out is a patch, and the reason is worth more than the patch.

### The diff

```
wrapped/shm_open.c:20     -  ret = elfcalls()->shm_open(name, oflags_bsd_to_linux(oflag), mode);
                           +  ret = elfcalls()->shm_open(name, oflag, mode);
wrapped/sem_open.c:23     -  ptr = elfcalls()->sem_open(name, oflags_bsd_to_linux(oflag), mode, value);
                           +  ptr = elfcalls()->sem_open(name, oflag, mode, value);
for-libelfloader.c:19     -  wd = oflags_bsd_to_linux(flags);
                           +  wd = get_perthread_wd();
```

Committed as `build-freebsd/emu-shm-sem-flags.patch`, with a header that says
where it applies and what not to touch beside it. `git apply --reverse --check`
confirms it describes exactly the working tree, so the forward form applies to
the unpatched one.

### The law this section arrived at, and it is the useful part

**A silent failure path is a blind spot, and a speaking one buys the run.**

Four failures in this file's history were found by reading, not by running:
the name of the shm pool, the double flag conversion, the two symlink
"vanishing" arguments, the pax deletion theory. Every one of them cost at
least one run to disprove, because the code that hit them said nothing.

Then the pass was made to talk, and the first run with counters named a gate
no amount of reading had: `59 directories seen, 59 mkdir refusals`. The next
iteration printed the component table for the refused path, and the component
table named the line of code — the `mkdir -p` loop built every component of
the destination path *except the last one*, because it only truncates at
slashes and the final element has none after it. `Library` existed,
`Frameworks` did not, and every `mkdir` of a child answered `ENOENT`, which
is the correct answer to a parent that is not there.

One run to name the gate, one to localise it, one to confirm the fix. Three
guesses would have been the alternative and would have been wrong, as the
previous three had been.

So the rule, for the next thing in this tree: **when a step can fail, make
the failure say which step, which input, and what the filesystem thinks of
the inputs — and never let "skipped" look like "nothing there".** A count
tells you that sixty descents were skipped. A path, a per-component `lstat`
and the name in hex tell you why.

### The sweep, and what it left alone

Every `oflags_bsd_to_linux` call site in the tree is eight. Four feed an elfcalls
slot and were wrong; four feed a Linux syscall and are correct:

| site | feeds | verdict |
|---|---|---|
| `wrapped/shm_open.c:20` | elfcalls slot | **wrong, fixed** |
| `wrapped/sem_open.c:23` | elfcalls slot | **wrong, fixed** |
| `for-libelfloader.c:18-19` | elfcalls slot (as `wd`) | **wrong, fixed** |
| `impl/fcntl/openat.c:42` | `LINUX_SYSCALL` | correct, untouched |
| `impl/fcntl/fcntl.c:88` | `LINUX_SYSCALL` | correct, untouched |
| `linux_premigration/ext/file_handle.c:177` | `LINUX_SYSCALL` | correct, untouched |
| `for-libelfloader.c:18` (as `linux_flags`) | `LINUX_SYSCALL` | correct, untouched |

The rule is not "stop translating". It is "do not translate on the way to a
host function filled into the elfcalls table" — the table is filled from the
host's symbols and those functions want host flags. Removing the four correct
ones would break three working paths to fix one broken one, and the sweep is
only worth having because it drew that line.

### The prediction, written before any run

Each value the caller can pass, what the host is given now, and what it must be
given after — all four measured on this machine, so this is arithmetic:

| caller passes | reaches the host now | now | after the fix |
|---|---|---|---|
| `0x202` BSD O_RDWR\|O_CREAT | `0x042` | `EINVAL` | a real fd |
| `0xa02` BSD O_RDWR\|O_CREAT\|O_EXCL | `0x0c2` | `EINVAL` | a real fd |
| `0x002` BSD O_RDWR | `0x002` | `ENOENT` | `ENOENT`, unchanged |
| `0x0c2` a Linux value, not BSD | `0x002` | `ENOENT` | `EINVAL` |

The last row is the one to read twice. It is expected to **change**, from
`silently mistranslated into something else` to `rejected for what it actually
is`, and that is not a regression: nothing in the product passes a Linux value
to that slot, and the row exists so the change is not mistaken for one.

**Falsifiable in one run:** if after the fix the two `O_CREAT` rows still say
`EINVAL`, the translation is happening somewhere else as well and this fix is
in the wrong place. That is the outcome that would send the work back, and it
is written down here so it cannot be reinterpreted afterwards.

### The oracle, committed and runnable

`build-freebsd/shm-flags-contract.sh` asserts the contract the elfcalls slots
are filled under, by calling the host's `shm_open` with each value. No root, no
guest, no emulation. It exits non-zero if a value carrying `O_CREAT` does not
work:

```
O_RDWR|O_CREAT                       0x202 ->  3  errno=0
O_CREAT|O_EXCL                       0xa00 ->  3  errno=0
O_RDWR|O_CREAT|O_EXCL                0xa02 ->  3  errno=0
O_RDWR, no O_CREAT (want ENOENT)     0x002 -> -1  errno=2
translated 0x0c2 (want EINVAL)       0x0c2 -> -1  errno=22
```

Its first version had a row that failed, and the failure was mine: I wrote
`0x8a0` for "O_CREAT|O_EXCL" when `0x8a0` is `O_FSYNC|O_EXCL` — the access mode
is 0, so it carries no `O_CREAT` and picks up `O_FSYNC` instead, which the host
refuses. The correct value is `0xa00`, and the corrected row passes. Worth
recording because a test that fails for a reason the author introduced looks
exactly like a test that found a real defect.

### Why no root run, and what is not done

**The fix cannot be built or tested from this machine, and the наряд's slice 2
therefore does not happen here.** Not a shortage of budget:

- The three files are in the `src/external/xnu` submodule — a checkout of
  `darling-xnu` — and this repository carries only a gitlink to it. A fix to it
  cannot be a commit in `pr-arm64` without a push to that other repository.
- That submodule's on-disk checkout is at `fa29287a` while `pr-arm64` records
  `12132d9f9`. It is not at the pinned revision, so even a commit made here
  would not sit on the base.
- **No build script in this repository compiles the emulation.** The only
  reference to `external/xnu` in `build-freebsd/` is an include path for
  CarbonCore. The shim the guest actually loads,
  `usr/lib/system/libsystem_kernel.dylib`, is a prebuilt artefact dated 9 June.
- `build-mldr-only.sh` compiles `startup/mldr/*` only. mldr *fills* the elfcalls
  table; the flags translation is in the shim, so rebuilding mldr would have
  changed nothing even if it were needed.

So **window run №9 is not done, and no claim is made about it.** The patch is
the deliverable; the oracle is the check; the prediction is falsifiable. What
the owner of `darling-xnu` still has to do, in order: apply the patch, build
the shim, install it into the overlay, then run the guest flag probe and the
window probe, and compare against the table above.

**Also not done, and deliberately:** nothing in `src/` of *this* repository is
touched by the fix. `build-freebsd/` gains a patch file and a test, and that is
all.

### The same fix, applied to the prebuilt shim, and measured

**This supersedes the paragraph above.** "The fix cannot be built or tested from
this machine" was true of the *source* fix and is still true of it: the three
files live in the `xnu` submodule, no script here compiles the emulation, and
the shim is a prebuilt artefact. But the shim is a **file**, and the change
above is three instructions long. It can be written into that file directly, and
then the oracle above can be run against a guest. So slice 2 was done after
all, by patching the artefact instead of rebuilding it, and the claim about
window run №9 below is now narrower — see the end of this subsection.

The shim is a **fat** binary: `magic 0xcafebabe`, two slices, the x86_64 one at
offset **4096**. `objdump` reports slice-relative addresses, so the address in
the file is `4096 + vaddr`, and reading without that base reads the wrong bytes.
Its `__TEXT` is `vmaddr 0x0 / fileoff 0x0`, so `file_off == vaddr` and the
identity that §5.2's `libsystem_c` patch relied on holds here too. Both
facts are needed; the first is the one that bites.

The verified table. `abs` is the offset in the file:

| vaddr | abs | before | after | what |
|---|---|---|---|---|
| `0x6696d` | `0x6796d` | `e8 1e 21 fe ff` | `89 f8 0f 1f 00` | `_sys_shm_open`, flags as-is |
| `0x6674d` | `0x6774d` | `e8 3e 23 fe ff` | `89 f8 0f 1f 00` | `_sys_sem_open`, flags as-is |
| `0x4b8ee` | `0x4c8ee` | `e8 9d d1 ff ff` | `e8 bd 00 00 00` | `__open_for_libelfloader`, **wd** |
| `0x666d1`+`0x666e4` | `0x676d1` | `48 89 e5 48 83 ec 10 89 7d fc` / `48 63 7d fc` | `89 e5 48 83 ec 10 48 89 7d f0` / `48 8b 7d f0` | `_sys_sem_close`, handle width |

Four of the seven `oflags_bsd_to_linux` call sites are left alone: they are the
`LINUX_SYSCALL` path (`_sys_open_by_handle`, `_sys_fcntl_nocancel`,
`_sys_openat_nocancel`) and `__open_for_libelfloader`'s own `linux_flags`, and
that path's contract genuinely is Linux flags. The sweep boundary is
`_sys_shm_open`, `_sys_sem_open` and the `wd` call.

**The `wd` site is not a flags site and must not be given the flags treatment.**
It reads `wd = oflags_bsd_to_linux(flags)`, and the right answer there is
`wd = get_perthread_wd()`. Replacing that call with `mov %edi,%eax` leaves
`wd = flags` — the very bug being fixed — and the mechanical gate
(*`edi` is set before, `eax` is read after*) cannot see it, because it is
satisfied by a site that is wrong in a different way. So the third site is a
**call-target swap**: `rel32 = 0x4b9b0 − 0x4b8f3 = 0xbd`, five bytes, same
length, and `_get_perthread_wd` reads only immediates and TLS, never `edi`, so
the stale `flags` in `edi` is harmless.

**`_sys_sem_close` took a pointer as an `int`, and that is a second defect,
not a third site.** `sem_open` returns a 64-bit pointer; the wrapper stored it
with `movl %edi,-0x4(%rbp)` and read it back with `movslq -0x4(%rbp),%rdi`, so
`0x174F99EA9888` went in as `0x99EA9888` and came out as
`0xFFFFFFFF99EA9888`. The gate that let it be written is the same shape as every
other one here: `sem_open` always failed first, so `_sys_sem_close` was never
reached and never crashed. Fix the flags and the crash appears.

Two wrong things had to be avoided, and both are load-bearing:

- **The length must not change.** `89 7d fc` is three bytes and `48 89 7d fc`
  is four, so the obvious replacement shifts every following instruction and
  silently breaks their `rel32` fields. There is no slack to spend: the five
  bytes at `0x666db` are `callq _elfcalls`, not padding, and a near call is
  never shorter than five bytes. The one byte comes from `movq %rsp,%rbp`
  (`48 89 e5`, three) becoming `movl %esp,%ebp` (`89 e5`, two) — legal because
  `rbp` is only a frame base in this function and `popq %rbp` restores the
  caller's copy from the stack, so the zero-extended upper half never escapes.
- **The slot must not straddle the frame.** Writing eight bytes at
  `-0x4(%rbp)` covers `rbp−4 … rbp+3` and takes the low half of the saved
  `rbp`. The frame is `subq $0x10`, and `-0x8` is **not** free — `0x666ea`
  stores the slot's return value there — so the store goes to `-0x10`, which
  nothing in the function touches.

Result: the file's length is unchanged, ten bytes differ in the `sem_close`
window, every `rel32`-bearing instruction in the function keeps both its address
and its target, and `callq _elfcalls` does not move.

**Measured, against the oracle at the top of this section.** Guest and host, same
value, same call:

| value | oracle (host) | guest | |
|---|---|---|---|
| `0x202` | fd, errno 0 | fd 8, errno 0 | match |
| `0xa00` | fd, errno 0 | fd 8, errno 0 | match |
| `0xa02` | fd, errno 0 | fd 8, errno 0 | match |
| `0x002` | errno 2 | errno 2 | match |
| `0x0c2` | errno 22 | errno 22 | match |

**Five of five, and the control is what makes it mean anything.** The same probe
on the *unpatched* shim: `0x202`, `0xa00` and `0xa02` all give errno 22,
`0x0c2` gives errno 2, and `sem_open` gives `SEM_FAILED` errno 22. Three of the
five rows and the control are broken before the patch.

One thing the probe had to stop doing. Its `sem_open` control asks for
`O_CREAT|O_EXCL` on a fixed name, so a run that died before the unlink left the
object behind — and `rm` of it on the host is refused, it is root-owned. The
obvious repair, an unlink first, is **one libc call too many**: the guest stack
is at its limit at that depth and the extra call faults on the guard page
(`addr == rsp`). Measured both ways on the same patched shim, with the unlink
one `SIGSEGV`, without it none. So the probe does not clear the residue, it
names it: `EEXIST` from that control means last run crashed, not that the
elfcalls table is broken. A probe that changes the stack it is measuring is worse
than one that reports a dirty filesystem.

**Still not done: window run №9.** These measurements are the flag probe, which
is the check this section predicted would pass. The window probe has not been
run against the patched shim, so no claim is made about it. What is measured is
that `O_CREAT` now reaches the host and is honoured, and that the four walls
before the shm pool are down.

#### The patch as a script, because the shim is not in git

The shim is an artefact of the overlay and is not in this repository, so a
description of the change is not enough to reproduce it — the next person would
have to re-derive the addresses. That is the §6 trap with a new face. This
refuses to write anything whose bytes are not exactly what is expected, and
re-verifies by decoding rather than by trusting its own write:

```sh
python3 - <<'EOF'
import struct
K = "overlay/usr/lib/system/libsystem_kernel.dylib"   # the shim, in the overlay
d = bytearray(open(K, "rb").read())

# The x86_64 slice's base in the fat header, found rather than assumed.
nfat = struct.unpack_from(">I", d, 4)[0]
base = None
for i in range(nfat):
    ct, cs, off, size, align = struct.unpack_from(">iiIII", d, 8 + i * 20)
    if (ct & 0xffffffff) == 0x01000007:
        base = off
assert struct.unpack_from("<I", d, base)[0] == 0xfeedfacf, "not a 64-bit Mach-O slice"

# __TEXT must be the identity, or file_off != vaddr and every offset below is wrong.
ncmds = struct.unpack_from("<I", d, base + 16)[0]
p, segs = base + 32, []
for _ in range(ncmds):
    cmd, cmdsize = struct.unpack_from("<II", d, p)
    if cmd == 0x19:                                     # LC_SEGMENT_64
        name = d[p+8:p+24].split(b"\0")[0].decode(errors="replace")
        vmaddr, vmsize, fileoff, filesize = struct.unpack_from("<4Q", d, p+24)
        segs.append((name, vmaddr, vmsize, fileoff, filesize))
    p += cmdsize
text = [s for s in segs if s[0] == "__TEXT"][0]
assert text[1] == 0 and text[3] == 0, "__TEXT is not the identity; recompute the offsets"

MOVNOP = b"\x89\xf8\x0f\x1f\x00"                        # mov %edi,%eax ; nopl (%rax)

# (vaddr, expected-before, replacement). The before-bytes are the gate.
EDITS = [
    (0x6696d, b"\xe8\x1e\x21\xfe\xff", MOVNOP),                  # _sys_shm_open
    (0x6674d, b"\xe8\x3e\x23\xfe\xff", MOVNOP),                  # _sys_sem_open
    (0x4b8ee, b"\xe8\x9d\xd1\xff\xff",
            b"\xe8" + struct.pack("<i", 0x4b9b0 - (0x4b8ee + 5))),  # wd -> get_perthread_wd
]
# The sem_close window: ten bytes rewritten, length preserved, the call at 0x666db untouched.
EDITS.append((0x666d1, b"\x48\x89\xe5\x48\x83\xec\x10\x89\x7d\xfc",
                       b"\x89\xe5\x48\x83\xec\x10\x48\x89\x7d\xf0"))
EDITS.append((0x666e4, b"\x48\x63\x7d\xfc", b"\x48\x8b\x7d\xf0"))

for vaddr, before, after in EDITS:
    off = base + vaddr
    assert len(before) == len(after), "length changes: everything after it shifts"
    if bytes(d[off:off+len(after)]) == after:
        continue                                          # already applied
    if bytes(d[off:off+len(before)]) != before:
        raise SystemExit("refusing 0x%x: got %s, expected %s"
                         % (vaddr, bytes(d[off:off+len(before)]).hex(" "), before.hex(" ")))
    d[off:off+len(after)] = after

open(K, "wb").write(d)

# Re-verify from the bytes, not by trusting the write. Two sites must now BE the
# mov/nop, and only the third is still a call -- checking that all three are
# calls is a bug this script had before it was run.
for vaddr in (0x6696d, 0x6674d):
    assert bytes(d[base + vaddr:base + vaddr + 5]) == MOVNOP, "0x%x is not the mov/nop" % vaddr
off = base + 0x4b8ee
assert d[off] == 0xe8, "0x4b8ee is not a call any more"
got = 0x4b8ee + 5 + struct.unpack_from("<i", d, off + 1)[0]
assert got == 0x4b9b0, "0x4b8ee calls 0x%x, wanted 0x4b9b0" % got
assert bytes(d[base + 0x666db:base + 0x666e0]) == b"\xe8\x40\x75\xfe\xff", "the elfcalls call moved"
print("patched and verified")
EOF
```

The five `_oflags_bsd_to_linux` sites this leaves alone are named above on
purpose: the script patches three, and a fourth kind of change — a new call
where there was none — is a different edit with a different risk, which is why
the `wd` swap is listed separately and separately verified.

### Window run №9: the fourth wall fell, and the sixth wall is one line wide

Run against the patched shim, one root run, the seat live (capabilities=3),
all eight preflight checks passed, closure walk clean over 59 images.

**The flags defect is gone on the full window, not only in the flag probe.** The
error the backend reports moved:

| | before | after |
|---|---|---|
| `WaylandWindow: shm allocation failed` | `Invalid argument` (EINVAL) | `No such file or directory` (ENOENT) |

EINVAL was the fingerprint of the double translation. It is not what the run
reports now. A different wall is in front, and the log names it in one line:

```
[darling-mldr] unhandled Linux syscall 77 — ENOSYS
```

**77 is `ftruncate` on x86_64, and the emulator never defines it.** Its own
numbering in `src/startup/mldr/freebsd_syscall_trap.c` runs `fsync 74`,
`fdatasync 75`, and then stops — there is no `#define ... 77`, so the dispatch
falls through to the ENOSYS arm at line 2442. The Wayland backend sizes its
shm segment with `ftruncate` before mapping it, so the shm pool cannot be
created at any window size, and no frame is committed. `267` (`openat2`) is
also undefined and also appears in the log, three times, but it is not on this
path.

So the verdict on №9 is **the sixth wall, and it is a gap in a list rather than
a wrong value**: one missing `#define` and one `case`, in a file this repository
does build. Nothing here is a guess about what the code means; the log printed
the number and the number is absent from the table.

#### The fix's own assumption, which is a boundary and not a measurement

The `sem_close` edit reclaims one byte with `movl %esp,%ebp` instead of
`movq %rsp,%rbp`. That instruction **zeroes the upper half of `rbp`**, so the
edit is valid while the guest's stack lives below 4 GiB.

This is an assumption of the fix, not something the run measured. It holds for
every run so far, and it will hold until `mldr` hands out a stack above 4 GiB —
at which point the truncation is silent: the function still works, the upper
half of `rbp` is simply wrong. Anyone changing the frame size of that function
should reach for a different byte.

#### The residue the flag probe leaves, and who removes it

The `sem_open` control asks for `O_CREAT|O_EXCL` on `/.probe-sem`, and a run
that dies before the unlink leaves the object in place. It is removed by the
guest in a root run. On the host `rm` is refused — the object is root-owned and
the build user cannot delete it — so a run that ends in a crash leaves a
`EEXIST` behind that the next run must be told about rather than be confused
by. The probe names it for exactly that reason.

#### `-k` is not needed, and the comment in the source said to drop it if not

The symlink pass runs first and `pax` is then handed `find . -type f`, which
mentions no link, so `-k` ("keep destination entries the archive does not
mention") looked necessary. **Measured without it:** 106 links created, 0
failed; the guest lists `AppKit` and `Resources` as `d_type=10`; the window
lists six entries; `pathsForResourcesOfType:@"backend"` returns one path. Every
one of those is the same number as with `-k`.

The pax-deletion theory was never true — nothing was ever created, so nothing
was ever deleted — and the source comment already said to drop the flag if a
run showed the links surviving without it. That run has now happened, so the
flag goes in the next commit rather than this one.

#### Window run №9-2: the sixth wall was one line, and the path completes

`ftruncate` was the whole wall. It is now `77` in the numbering and a case in
the dispatch, and the next run gets further than any run before it:

```
[step 06] write through pixels[0] (proves the shm fd is mapped, not just created)
[step 07] buffer acquired: shm fd created, mapped and written
[step 08] flushBuffer (commit the acquired buffer to the compositor)
[step 09] RESULT: window created, shm buffer acquired + written + flushed
```

The buffer is a real buffer, not a handle: the backend stores what it was given
(`field_24=0 width=640 height=480 field_36=2560`, stride 2560 = 640 × 4 for
ARGB8888) where the previous run stored `width=-1 height=-1` and
`record=0x0`. Step 06 writes *through* the mapping, so `mmap` is proven rather
than inferred. And `unhandled Linux syscall 77` is gone from the log.

**The stale-binary trap, and how it was ruled out before spending the run.**
A binary that did not pick the change up produces the *same* ENOSYS and reads
as "the fix does not work", so the freshness was checked first, not after:

| | before | after |
|---|---|---|
| size | 683312 | 683808 (+496) |
| sha256 | `749027117052c346…` | `1bd57d6a7eab4726…` |
| mtime | 2026-09-29 21:42 | 2026-09-30 03:51:17 |

and the binary is 18 seconds newer than the edited source. The build script
installs to the path the guest runs from, so there is no second copy to fall
out of step. Had any of those three checks been skipped, the run would have
been spent proving nothing.

#### A fix that lived on disk and was lost, which is the argument for branches

`PLAN.md` §8.3 has described this fix as done since September, and §8.4 lists it
as **ГОТОВО ✅**. `git log -S LINUX_SYS_ftruncate --all` finds it in the two
commits that added `PLAN.md` — and nowhere in the C source. The fix had been
written to a working tree, never committed, and lost to a reset. Nine months of
a plan that says a thing is done, over code that does not contain it, is worse
than a plan that says nothing: the next reader skips the work, and the wall
comes back wearing the same number.

That is the argument, and it is not about tidiness. A fix in a branch survives a
reset; a fix in a tree does not; and a document that records a fix as complete
is only as good as the commit behind it. When §8.3 said the same thing last
time, it was right by accident — the code was on the disk it was describing,
and the disk was not a promise.

**Rider, 288 again on this run:** the bundle was reached through the symlink —
`dlopen_internal(/System/Library/Frameworks/AppKit.framework/Resources/Backends/
Wayland.backend/Contents/MacOS/Wayland)` — and loaded. The explicit readdir
listing is the flag probe's rider and is unchanged: six entries, `AppKit` and
`Resources` as `d_type=10`, one backend path.

**What this run does not show.** The commit is *sent*; the probe exits on step
09 without a roundtrip, so no frame callback and no `wl_display_get_error` were
waited for, and no screenshot was taken — the single authorised root run was
spent on the run itself, and the surface is gone by the time the process exits.
So the log proves the path through to the commit call and nothing claims that
the compositor painted it.

**Two unhandled numbers remain, both untouched.** `263` once and `267` three
times, neither defined. The numbering convention in this file is x86_64 Linux
and is anchored by its own neighbours: `268 = fchmodat` and `269 = faccessat`
are both defined, and both are what x86_64 says they are. So `267` is
`readlinkat` — not `openat2`, which is `437` and also undefined — and `263` is
`unlinkat`. `readlinkat` appearing three times is worth a second look rather
than a park, because this milestone is about symlinks and `dyld` resolves them;
that is noted, not acted on.

#### Window run №9-3: asking for a receipt, and what the answer was

The previous run stopped at `wl_surface_commit`, which proves the request was
marshalled and flushed and proves nothing about whether sway took it. So the
probe now asks, and the interesting part is that the answer is not the one the
question was aimed at.

**There is no frame callback to wait for.** The backend never calls
`wl_surface_frame` — it is absent from the whole call list — so it never asks
the compositor for one and there is nothing to receive. That is a measurement of
the backend, not an assumption, and it removes half the plan before it starts.
The receipt has to be `wl_display_get_error`, reached through a roundtrip.

Getting the `wl_display*` took a wrong turn first, and the harness caught it.
The two `_wl_display_*` symbols are statically linked **C functions**, not
selectors of `WaylandDisplay`, so declaring them as methods is declaring a
selector the backend does not implement — and `build-wayland-window-test.sh`
refuses to emit a binary whose probe and backend disagree. The route that works
is a real selector, `-[WaylandDisplay waylandDisplay]`, which the gate reports
as `^{wl_display=}`; the two C functions are then resolved with `dlsym`, and a
NULL from either is reported rather than treated as a receipt.

**The result: no receipt, and the reason is not the compositor.**

```
waylandDisplay = 0x11eddec35000
dlsym wl_display_roundtrip = 0x11ede0c6f2e0, wl_display_get_error = 0x11ede0c6f330
NO REPLY within 5s: the roundtrip is still blocked
```

Three facts from the same log, and only the third is a wall:

1. **A roundtrip on the main thread succeeded earlier in this very run** —
   `roundtrip1 returned 57`, with the compositor, shm, wmBase, output and seat
   all bound. The connection, the sync callback and the reply path all work.
2. **The hang is in the wait, not in the request.** The commit was sent; the
   roundtrip blocked.
3. **The roundtrip that blocked is the one on a spawned thread.** The probe
   starts a thread so that "blocks forever" is a printed result rather than a
   stuck run, and that thread is where it stops.

So the honest reading is that this run **did not establish a seventh wall**. It
established that a roundtrip issued from a spawned thread does not come back,
while the same call on the main thread does — which is a fact about waiting from
a thread, and the thread is this probe's own instrumentation. Reporting it as
"sway refused the commit" would be the wrong claim in both directions: the
commit's own receipt is still unknown.

**The unhandled syscalls are not on this path, and that is now measured rather
than assumed.** All four land *before* the flush — three before the successful
main-thread roundtrip, one just before it — and none after. `263 = unlinkat` and
`267 = readlinkat` did not block the commit, which is the negative answer to
the question that was parked, and neither is touched.

**The control that would settle it** is one line: issue the post-commit
roundtrip on the main thread. The thread exists only to bound the wait, so
without it a hang costs the run; the trade is a timeout that cannot be trusted
against a hang that is itself the finding. That is the next run, and it is not
taken here — this run's root prompt was the one that was authorised.

**No screenshot was taken, and none can be.** The seat has a real 1280x720
output, but this machine has no capture tool — no `grim`, `xwd`, `scrot`,
`import` or `maim` — and installing one is not a thing this session may do. The
log route is what there is, which is why the receipt is a printed number.

#### Window run №9-4: the commit is confirmed, and a second lane is a finding

The control from the previous subsection, one line, on the main thread:

```
main-lane:   entering wl_display_roundtrip on the MAIN thread
main-lane:   roundtrip returned 3 (errno 0), wl_display_get_error=0
thread-lane: answered=0 (still blocked)
RESULT: COMMIT CONFIRMED -- main-thread roundtrip returned 3 and get_error=0
LANE FINDING: the main thread got its reply and the spawned thread did not
```

**The commit is confirmed.** The roundtrip after the commit returned — sway
answered the sync — and `wl_display_get_error` is 0, so the compositor took the
commit and had nothing wrong to say about it. That is the receipt the previous
run could not get, and it took moving the call off the spawned thread and onto
the main one. `roundtrip1 returned 57` earlier in the same run is the same
thread behaving the same way, so the main thread was never the problem.

**The seventh wall did not fire.** Outcome (B) — blocked on the main thread —
did not happen, and it is worth being precise about why that is a result rather
than an absence of one: the main lane has no timeout at all. A deadline around
a main-thread roundtrip is either a second thread, which is the thing being
tested, or a non-blocking call that is not a roundtrip. So the check is
deliberately unbounded, and the evidence of success is a line that is *there* —
`returned 3` — rather than a line that is absent.

**Outcome (C) fired instead, and it is a real finding, recorded not fixed.** The
same display, the same call, two threads, two answers: the main thread's
roundtrip returned 3 and the spawned thread's is still blocked, indefinitely,
with no error. That is a new lane — **guest threads under mldr** — and it sits
next to the parked `get_perthread_wd` question, which reads its per-thread
working directory from `%gs:(,0xc9*8)`. Two facts about `%gs` in one run: a
thread-local read that looks like a placeholder, and a thread that never gets
its reply. They are not yet shown to be the same defect and are not merged here;
what is established is that a spawned guest thread does not complete a blocking
Wayland roundtrip while the main thread does, on the same connection.

**The unhandled syscalls are off the critical path, now twice measured.** All
four land before the flush and none after — the same answer as the previous
run, now with the flush and the main-lane entry placed by line number on either
side of them. `263 = unlinkat` and `267 = readlinkat` are not what stands
between this milestone and a confirmed commit, and neither is touched.

**Rider, 288, on this run:** the bundle is reached through the symlink and
loaded — `AppKit.framework/Resources/Backends/Wayland.backend/Contents/MacOS/
Wayland` — with 106 links staged and 0 failed.

**No screenshot, and the enumeration is now complete.** Not a shrug at the
question: this machine has no `grim`, no `wlr-screencopy`, no `swaygrab`, no
`wayshot`, no `wofi`, no `grimshot`, no X11 capture (`xwd`, `scrot`, `import`,
`maim`), no screencopy library, and no Python Wayland binding; `ffmpeg` is
present but needs an X11 display, and there is none. `swaymsg` has no
screenshot verb. The seat does have a real 1280x720 output, so the absence is
the tooling and not the compositor — and installing a tool is not a thing this
session may do. So the frame is evidenced by the compositor's own answer rather
than by a picture, which is a weaker thing than a picture and is labelled so.

#### The guest-thread lane: a probe to classify it, and a bug the probe had

The LANE FINDING above is one symptom with two very different explanations, and
they cost very different amounts of work:

- **every** blocking wait fails on a spawned guest thread → a general problem
  with guest threads under mldr: scheduling, signal delivery, TLS, stack. The
  Wayland roundtrip is then just the first thing that noticed.
- only the reply wait fails, while a timer and a lock come back → the failure is
  in the descriptor/event path, and thread resumption is fine.

So the lane needs a probe, not a fix, and the fix is not authorised here anyway.
`tests/src/guest-thread-wait.c` runs three kinds of blocking wait, each twice —
once on the main thread as a control, once on a spawned thread — chosen to
separate the *mechanisms* rather than to repeat the same test three times:

| wait | what it isolates |
|---|---|
| `nanosleep` | a timer: no descriptor, no lock, nothing to wake it. If this fails the thread is not being resumed at all. |
| `read(pipe)` | a descriptor wait released by another thread's `write` |
| `sem_wait` | a lock released by another thread's `sem_post` |

Every wait is bounded by a deadline, and the program prints a verdict and exits
rather than sitting in a syscall — a probe that hangs cannot report that it
hung. No window, no compositor, no Wayland, so no seat is involved.

**The probe had a bug, and running it natively is what found it.** Compiled for
this host rather than for the guest, the three lane lines all said "did not
return" while the summary said every wait had come back — a report contradicting
itself, in the direction of looking like a defect. The bounded wait recomputed
an absolute deadline inside its own loop, so the elapsed comparison was zero on
the first pass and it gave up after one 20 ms poll; the thread then finished a
moment later and set the very flags it had given up on. Fixed to a fixed start
time and an elapsed comparison, it is self-consistent, and natively all three
waits return — as they must on a real FreeBSD.

That run is worth exactly what it is: **the probe is sound, and it says nothing
about guest behaviour.** A native pass is not a guest result and is not offered
as one. It is also the reason the probe can be trusted once it does run, and the
reason a control whose own results are printed rather than assumed is in there
at all — an earlier draft printed a hardcoded "all passed" for the baseline,
which would have reported a baseline nobody observed.

**Not measured yet.** `launch-dynamic` refuses to run without root, and a root
prompt is not something to take unasked, so the guest leg of this is requested
rather than assumed. Until that run exists the lane is classified as *open*, and
this section deliberately does not guess which of the two it is.

**Kept adjacent to the parked question, not merged with it.** `get_perthread_wd`
reads its per-thread working directory from `%gs:(,0xc9*8)`, and this lane is
about a thread that never comes back. Two facts about threads in one run are
two facts; a shared `%gs` is a hypothesis, and nothing here tests it.

#### Two things the table settled, and a correction to the lane finding above

**The guest has no timer, and no poll.** Reading `freebsd_syscall_trap.c`'s own
numbering rather than assuming it:

| syscall | | in the table |
|---|---|---|
| `read` | 0 | defined |
| `write` | 1 | defined |
| `futex` | 202 | defined |
| `poll` | 7 | **not defined** |
| `select` | 23 | **not defined** |
| `nanosleep` | 35 | **not defined** |
| `pselect6` | 270 | **not defined** |
| `ppoll` | 271 | **not defined** |
| `clock_nanosleep` | 230 | **not defined** |

All of the right-hand column falls through to the ENOSYS arm. This is why the
probe has no timer leg: a guest `nanosleep` would fail on the **main** thread
too, so it would have measured a missing syscall rather than a thread that
cannot be resumed — and the main-thread control would have caught it, which is
what the control is for. The three legs the probe does have are the three the
guest can actually perform: `read`, a blocking `write`, and `sem_wait`.

**A correction to the LANE FINDING in §9-4, because that line is not a
measurement.** It reported `thread-lane: answered=0` and read as "the spawned
thread never got its reply". The semaphore was checked **immediately** after the
main lane returned — and the main lane returned in milliseconds, having just
confirmed the commit — so the thread had been given almost no time. It is a
race, and the log says so: `_sem_post` appears in that run, and the thread calls
`sem_post` only *after* its roundtrip returns, so the thread did come back.

So the honest state of the finding is narrower than §9-4 states it. The §9-3
run waited the full five seconds and saw no reply, and *that* is sound; it says
"not within 5s", not "never". The window probe's thread lane was too fast to
mean anything. Nothing here establishes that a spawned guest thread cannot get
a Wayland reply, and the classification below is still open.

**And the probe had three bugs, all three found by compiling it for the host and
running it there**, which is the only reason it is worth trusting now:

1. The bounded wait recomputed an absolute deadline inside its own loop, so the
   elapsed comparison was zero on the first pass; it gave up after one 20 ms
   poll and the thread then set the flags it had abandoned. Per-line verdicts
   and the summary disagreed.
2. It waited for the read flag *before* writing the byte the read was blocked
   on. A deadlock, which reported all three legs as dead on a machine where all
   three return — in the direction of blaming the guest.
3. It passed one descriptor for both directions, so the thread's write hit the
   pipe's read end (EBADF) while the main thread sat in a drain with an empty
   pipe. A hang, in a probe whose entire job is to report rather than stall.

Every one of them reported a defect that was not there, and every one of them
would have been read as a finding about the guest. A native self-test costs one
compile and is the cheapest possible guard against a probe inventing its own
result; the rule this adds is that a blocking-wait probe is not finished until
it has been shown to PASS somewhere the answer is known.

#### What the blocked roundtrip actually is, and why the probe may be measuring the wrong layer

The lane is named "guest threads under mldr", and that name deserves a challenge
before the probe is run against it. Disassembling the call the window probe
makes:

```
_wl_display_roundtrip:
  leaq  0x316b(%rip), %rdi     ## "wl_display_roundtrip"
  callq <__lazy>               ; bind the symbol BY NAME
  cmpq  $0x0, -0x18(%rbp)
  jne   0x630c
  ...  return -1
  callq *%rax                  ; and call whatever that resolved to
  movl  %eax, -0x4(%rbp)
```

So the backend does not contain the roundtrip. It binds the *name* and calls
through a pointer, and the run log shows where that pointer lands:
`dlsym_fatal('wl_display_roundtrip') => 0x83aee9fc0`. That is a high mapping,
and it **moves between runs** (`0x835255fc0` in an earlier one) while the
backend's own base does not — a separately mapped implementation, not the
backend's statically linked code.

**And the wait never reaches the syscall trap.** `poll (7)` appears in the log
**zero** times as an unhandled guest syscall, and neither does `select`,
`pselect6` or `ppoll`. A wait that went through mldr's dispatch would have
shown up there, because this file's own table leaves all of them undefined. It
did not. So the roundtrip blocks in the shim, calling the host's socket
directly, and no Linux syscall is involved at all.

That has a consequence for the probe, and it is the reason this is written down
before the run rather than after. The probe's three legs are `read`, a blocking
`write` and `sem_wait` — all of them **guest** blocking calls, all of them
through mldr's dispatch. If the lane is really about the shim's host-side
socket wait when it is invoked from a thread the guest created, then a probe
made of guest blocking calls can return "(none), everything came back" and be
**right**, and that answer would then say nothing about the roundtrip at all.

So the probe is worth running — it settles whether guest blocking waits work on
a spawned thread, which is a real question and the one the name suggests — but
its "(none)" branch must not be read as clearing the lane. Only a positive
result, (A) or (B), would bear on it. The layer is the open question, and it is
open because the shim is host code reached through a lazily bound name, not
because anyone has guessed wrong yet.

#### The authorized guest run happened, and its output was lost — plus a harness hang

The one root prompt authorised for the guest leg of `guest-thread-wait` was
spent, and it did run: `mldr` executed and exited. What is missing is the
measurement, and the reason is worth recording because it is not the guest's
fault.

**What the process table showed afterwards:**

| | |
|---|---|
| `mldr` | state **Z** (zombie) — it ran and exited |
| `darlingserver` | state **I**, wchan **select**, 4m14s elapsed |
| `guest-thread-wait` | no such process |

So the guest side finished. The probe's three waits are bounded at 3 s each, so
the guest could not have been the thing blocking; the harness was. Once `mldr`
was gone, `darlingserver` sat in `select` indefinitely instead of returning, and
`launch-dynamic` never came back — which is why the run produced no output at
all: the caller was still waiting on a process that was not going to report.

**The measurement is therefore NOT taken, and nothing here guesses at it.** The
classification stays open. Asking for a second root prompt is the only way to get
the numbers, and the fix for the harness is not mine to make here.

**What I could and could not confirm about the hang.** That `darlingserver` does
not notice its child has exited is measured; *why* is not. Grepping its source
for `waitpid`/`SIGCHLD`/`WNOHANG` returns only vendored `duct-tape/xnu` headers
and no `darlingserver` code of its own — the submodule's on-disk store is one
of the broken ones, so the question cannot be settled from this checkout. Stated
as what it is: a run whose output is not consumed promptly leaves the server
blocked, and that is a cost every future run pays for.

**The recovery, for whoever runs it next:** capture the output to a file rather
than a pipe. `launch-dynamic` writes the guest's stdout to its own stdout and
nothing else, so a pipe whose reader goes away takes the measurement with it.
Redirecting into the build directory would have turned this run from lost into
a log.

#### The guest run, and why the probe could not run: the guest has no pipe

The redirect recipe works, and that part is settled: with the guest's stdout
going to a file and the whole command under `timeout --foreground -k 60`,
`launch-dynamic` **returned** instead of leaving `darlingserver` blocked in
`select`. The hang is a function of the output going to a pipe whose reader goes
away, and a file does not do that.

The probe then failed at its first line of work, and the reason is the most
useful thing this run produced:

```
[darling-mldr] unhandled Linux syscall 22 — ENOSYS
[darling-mldr] unhandled Linux syscall 20 — ENOSYS
```

**22 is `pipe`.** The probe's control opens two pipes before anything else, so it
exited at once, having measured nothing. The same list read against the trap's
own numbering:

| syscall | | | syscall | | |
|---|---|---|---|---|---|
| `read` | 0 | defined | `pipe` | 22 | **ENOSYS** |
| `write` | 1 | defined | `select` | 23 | **ENOSYS** |
| `writev` | 20 | **ENOSYS** | `poll` | 7 | **ENOSYS** |
| `socket` | 41 | defined | `socketpair` | 53 | **ENOSYS** |
| `connect` | 42 | defined | `accept` | 43 | **ENOSYS** |
| `sendmsg` | 46 | defined | `futex` | 202 | defined |
| `recvmsg` | 47 | defined | `epoll_wait` | 232 | defined |

So this is the **second** time the syscall table has removed a mechanism from
this probe — the first was the timer, now it is the pipes. Both were chosen from
what a POSIX blocking wait normally looks like, and neither exists here. A probe
written against the C library's idea of the world rather than against the
emulator's table will keep doing this, and each time the failure looks like a
guest thread that cannot be woken.

**The classification is NOT obtained.** Nothing here says whether a spawned guest
thread returns from a blocking wait; the probe never got far enough to ask.

**But it is obtainable, and the table says with what.** The guest has `futex`
(202) for a lock, and `socket` (41) + `connect` (42) + `recvmsg` (47) for a
descriptor wait: a `recvmsg` on a connected socket with nothing to read blocks
in the kernel, which is exactly the shape the pipe was standing in for. It even
has `epoll_wait` (232), so a genuinely timed wait is available — though
`nanosleep` is not, so a deadline would have to come from `epoll_wait`'s timeout
rather than from a sleep. Rebuilding the two pipe legs onto a socket pair's
biggest available cousin is a contained change; it needs one more root prompt,
and none is taken here.

#### Lane law №2 applied, and one clause of it is unreachable

The law is right in principle — design the probe from the trap's table, not from
POSIX — and applying it produced a result the law did not predict: **one of its
own clauses cannot be carried out.**

> deadline of any leg — ONLY from the timeout of `epoll_wait` (232)

`epoll_wait` is in the trap's table, and that is true, but **no guest binary can
call it.** The three 232-family calls are in the table for guest code that
issues *raw* Linux syscalls; they are not callable functions. Checked against the
shim rather than assumed:

| symbol | `libsystem_kernel` | `libSystem.B` |
|---|---|---|
| `_epoll_create` | not exported | — |
| `_epoll_ctl` | not exported | — |
| `_epoll_wait` | not exported | not exported |

There is nothing to link and nothing to `dlsym`, and the macOS SDK has no header
for them. So the deadline is taken from **228 (`clock_gettime`)**, which is both
exported by the guest's `libsystem_c` *and* defined in the trap — as a bounded
spin, since there is no timer to sleep on. **This is a deviation from the law and
it is recorded as one, not slipped in.** A deadline that cannot be built is
worth more as a stated gap than as a silent substitute.

**The other shape the table forced: two threads, not one.** The law's lock leg is
a wait on an address nobody will wake, so a thread that takes it parks there
forever and never reaches a second leg. One thread cannot carry both. The lock
leg and the descriptor leg therefore run on separate spawned threads.

**REACHED is not PARKED, and the difference is the trap's own doing.** The leg
was originally described as returning REACHED, and this file claimed that meant
"the thread executed and parked in a kernel wait". That was a stronger claim than
the probe could support, and the reason is written down in
`freebsd_syscall_trap.c`: *"a mismatched value makes FreeBSD return
success-**without-sleeping** where Linux returns EAGAIN"*. A `sem_wait` in this
guest can therefore come straight back without ever parking, and a flag set
before the call cannot tell that from a thread that parked as intended.

So the leg reports two facts and the reader combines them:

| reported | means |
|---|---|
| never reached the wait | the thread did not execute up to the call |
| REACHED it and **parked** (never returned) | the thread is in the kernel wait — the liveness fact |
| REACHED it, and it **RETURNED** without blocking | a finding, not a pass: the leg tested nothing |

Only the middle row is a pass, and the third row leaves the lane **open** and
says so. A leg that cannot distinguish "blocked" from "came straight back" is a
leg whose success is unearned, which is the same defect as the unbounded control
and the shared connection, one level deeper: those two could hang or lie about
the peer, and this one could lie about the thread.

**And the peer.** 49 `bind`, 50 `listen` and 51 `getsockname` are all ENOSYS, so
the guest cannot create a listening endpoint and a connected socket has to come
from a peer already listening elsewhere. The probe takes it from
`$DARLING_THREAD_PEER_PORT` and, when there is none, prints that the descriptor
leg is **not exercised** — which is deliberately not the same sentence as a
failed leg, because "no peer" and "the thread is broken" are different worlds
and a probe that cannot tell them apart will eventually report the second while
meaning the first.

**The probe now reaches a verdict on every path**, which the acceptance asked
for: a refused leg is a result line, never an exit. Natively it compiles clean
under `-Wall` and produces the answer a real FreeBSD must give — both legs pass,
verdict `(none)` — with a real peer listening, and the same `(not exercised)`
verdict with none. That is the self-test: a probe that cannot pass where the
answer is known is not finished.

**Addendum — root #3, one authorization: the lock leg parks, the descriptor leg
cannot be created, and the lane's own question is still unasked.**

Artifact first: `sha256 tests/guest-thread-wait-macho` =
`be0625f2…ddbc53a`, the value in `$DARLING_BUILD_DIR/guest-thread-wait.provenance`,
and the source blob equals `HEAD:tests/src/guest-thread-wait.c`. Peer up on
loopback and answering **two concurrent connections without root** first, so the
single `sudo` line could not be spent discovering a dead peer. Then one prompt,
root #2's recipe (`f7476d791`): `timeout --foreground -k 60 120` around
`sudo … DARLING_TEST_BINARY=guest-thread-wait-macho DARLING_THREAD_PEER_PORT=$PORT
launch-dynamic > $DARLING_BUILD_DIR/thread-lane.log 2>&1`. RC=0, 156 log lines.

| leg | control (main) | lane (spawned) | log |
|---|---|---|---|
| `sem_wait` (202) | RETURNED | **REACHED it and parked (never returned)** | 107 / 149 |
| `recvmsg` (47) | not exercised | not exercised | 108 / 150 |

**The peer is not why the descriptor leg is missing.** Log:105 reads
`no connected sockets (Address family not supported by protocol family)`, and
that `errno` comes from `socket(AF_INET, SOCK_STREAM, 0)` at
`guest-thread-wait.c:193` — *before* `connect` is ever called. The peer was
never contacted; it was answering two concurrent connections seconds earlier.

**The control's RETURNED is not the counterpart of the lane's PARKED.** The
control posts before it waits (`guest-thread-wait.c:264-265`: `sem_init(0,0)`
then `sem_post`), so it takes a token and was never meant to block. It measures
"an uncontended `sem_wait` on main returns", not "a blocking wait on main comes
back", and its word is one step stronger than the flag behind it
(`control_lock = rep.lock_reached`, `guest-thread-wait.c:268`). That is sound
here only because the `printf` comes *after* the call — a control that parked
would hang the probe rather than misreport it. A **contended** main-thread leg
is still missing, and it is the only thing that would make "main returns /
thread parks" a statement about threads instead of about contention.

**Settled:** a spawned guest thread reaches futex 202 and parks in the kernel —
the liveness fact, and correct POSIX behaviour for a semaphore nobody posts.
**Not settled:** whether a spawned thread comes **back** from a wait it was
released from, which is the lane's actual question. The lock leg parks by
construction (`guest-thread-wait.c:292`, "nobody will post this one"), so it
measures parking only. The release half lives entirely on the descriptor leg —
and that is the leg `AF_INET` refuses.

**Classification: not obtained.** Not (A): the thread reached its first blocking
call. Not (B): nothing was ever released, so nothing failed to return. Not the
probe's `(none)` either — that verdict (`guest-thread-wait.c:353`) needs
`lock_reached && desc_returned`, and `desc_returned` is false because the leg
was never created. The probe's own word is `(not exercised)` (log:152-156).
**The lane stays OPEN and the shim layer stays.**

Self-test, same source, same peer, no root: `cc -Wall -pthread` clean, both legs
measured, verdict `(none)`, RC=0. So neither the probe nor the peer is the thing
that is broken, and the *only* difference between that run and the guest run is
the socket.

**Why the guest has no socket — and the next root.** `darlingserver.cpp:271`
boots the container by execl'ing mldr on
`LIBEXEC_PATH "/usr/libexec/darling/vchroot"`, and on this host that path is
**not there**: `/usr/local/libexec/darling/usr/libexec/darling/` holds `mldr`
and nothing else, while the overlay's tree has `vchroot` (13260 bytes). mldr's
`load()` fails on it and prints precisely the line in the log
(`mldr.c:357-361`):

```
Cannot open /usr/local/libexec/darling/usr/libexec/darling/vchroot: No such file or directory
```

A missing `vchroot` is a missing network stack, and `EAFNOSUPPORT` is what a
`socket()` gets when there is none. So the leading explanation for the missing
descriptor leg is a **host provisioning gap — an incomplete install of the
overlay's darling tree — not a thread defect and not a probe defect.** Stated as
a hypothesis, because that is what it is: one run showed both facts. The check
that settles it is one prompt plus a host install that is not this lane's to
make, and if `socket()` starts handing back a descriptor once that path is
populated, the descriptor leg is unblocked with **no change to the probe**.

Also new, recorded and not interpreted: 38 × `unhandled Linux syscall 99 —
ENOSYS` between the lane header and the lock leg's result (log:111-148). The
trap prints numbers only, so this is "call 99" — `times` in the x86-64 Linux
numbering — from the lane's runtime.

**Next root, when one is granted:** (a) a contended main-thread leg — post
*after* the reached flag, so the wait is real and the probe still cannot hang —
which is probe-only and needs no root; and (b) a transport the guest's `socket()`
will actually give, which is the `vchroot` path above.

**Addendum 2 — the release half, and the two legs that need no peer.** The addendum
above ends with the lane's question unasked: the lock leg parked, and whether a
thread comes **back** was only ever measured on the descriptor leg, which `AF_INET`
refuses. So the question was moved onto a mechanism the trap does implement.

**A parked wait and a released wait are different measurements, so the release had
to be part of the same leg.** `RELEASE_MS` (`:90`) is how long a wait is given to
be *really* parked: main waits for the reached flag, spins two seconds on 228
(`spin_ms`, `:135` — a spin, not a sleep, because 35 is undefined), and only then
posts. The flag `lock_parked` (`:97`, set at `:372`) is the whole point of those two
seconds: a `sem_wait` here can return success-without-sleeping, so **parked** is
claimed only where the call is still out two seconds later. Then the post, and one
fact (`:382-389`): did the thread come back.

**The contended main leg is what makes the asymmetry mean something** (`:398-415`).
The old control posts *before* it waits, so it never blocked and could never stand
in for a blocking wait. This one blocks for real: `sem_init(0,0)`, a poster thread
(`main_poster`, `:152`), then main's own `sem_wait`. The poster waits for main's
reached flag (`:156`), waits out `RELEASE_MS` so main has really parked (`:157`),
posts (`:159`) — and then posts **again** (`:161`) as a net, because a leg that can
park `main()` forever is a leg with no bound. Which of the two woke main is a
result, and the probe says so in words: `RETURNED from the release`,
`RETURNED, but only after the watchdog post`, or `RETURNED, but the poster never
fired the release` (`:412-415`). The verdict reads the same fact, as
`m_released_ok` (`:434`) — `m_returned` alone would have claimed the main thread
came back when what woke it was the net.

**Both new legs need only 202 and 228, and the binary proves it.** The rebuilt
binary's undefined symbols are exactly `_sem_init _sem_post _sem_wait _sem_destroy
_clock_gettime _socket _connect _sendmsg _recvmsg` plus libc basics — 24 in all,
and **none** of `_poll _writev _pipe _select _nanosleep _shutdown _bind _listen
_getsockname _socketpair _accept`. No new call was introduced, so nothing new can
fail as "the thread will not wake up".

**The classification moved ahead of the peer, and 2×2, because the released legs
decide it** (`:451-476`). With the park earned, the lane is classified before any
peer row is consulted:

| released lock leg | contended main leg | verdict | what it names |
|---|---|---|---|
| returned | returned | **(Б)** | generic blocking is alive → the lane narrows to the event path |
| returned | only the net woke main | **(Б)** + separate finding | as above, and main's own release failed |
| did not return | returned | **(А)** | the wake path works and a *spawned* thread is not resumed |
| did not return | only the net woke main | **(А)** | the wake path itself — not a thread problem at all |

**The parked-word edge is closed, and the word is the fix.** In the `!peers_ok`
note the word "parked" is now claimed only where `lock_returned` says it is true (`:477-491`):
reached **and** returned prints that it returned without blocking and claims
nothing about parking. A note that says "parked" where the call came straight back
is the same unearned-verdict defect one level up.

**Native self-test, all four states, no root** — the probe earns the right to
classify before it is trusted with a guest run:

| run | released lock leg | contended main leg | verdict |
|---|---|---|---|
| as written | `RETURNED 0ms after the release` | `RETURNED from the release` | **(Б)** |
| lane release removed | `DID NOT RETURN within 3s` | `RETURNED from the release` | **(А)** spawned thread |
| main's release post removed | `RETURNED 0ms after the release` | `only after the watchdog post` | **(Б)** + finding |

The sabotaged runs are copies in `/tmp` with one `sem_post` replaced by `(void)0`;
the tracked file carries none of them. As written, `-Wall` clean, RC=0, 4.0s.

**The guest run is REQUESTED, not taken.** Binary rebuilt from this source
(22064 B, mtime 11:18, sha256 `aa753fd5…b31614c`, replacing `be0625f2…`), provenance
rewritten. One authorization will do it, no peer needed at all now:

```sh
timeout --foreground -k 60 120 sudo env \
    DARLING_SRC_DIR="$DARLING_SRC_DIR" DARLING_OVERLAY="$DARLING_OVERLAY" \
    DARLING_BUILD_DIR="$DARLING_BUILD_DIR" \
    DARLING_TEST_BINARY=guest-thread-wait-macho \
    "$DARLING_BUILD_DIR/launch-dynamic" > "$DARLING_BUILD_DIR/thread-lane.log" 2>&1
```

**Addendum 3 — a correction to Addendum 1, and the probe now says which call
refused.** Addendum 1 ended with a cause for the missing descriptor leg. Two of
its claims do not survive being checked, and one of them was mine to check.

**The `vchroot` story is refuted by `vchroot.c`.** Addendum 1 said a missing
`vchroot` is a missing network stack and `EAFNOSUPPORT` is what `socket()` gets
without one, which is why the descriptor leg is absent. `src/vchroot/vchroot.c` is
45 lines: it checks its arguments, `open`s the directory, `fchdir`s into it, calls
`__darling_vchroot(dfd)` and `exec`s what it was pointed at. There is no socket,
no interface, no network code in it at all — it builds the *vchroot context*, which
is a directory, not a stack. So installing it would not hand the guest a socket,
and the next person must not spend a root prompt on that. What the missing file
does explain is the `Cannot open …/vchroot` line and the failed container
bootstrap — which is a real defect, just not this one.

**"The errno comes from `socket()`, before `connect`" was not supported by the
code.** `connect_peer()` returned `-1` from either failure and preserved whichever
`errno` it had, so `EAFNOSUPPORT` was equally consistent with a `connect()` that
was refused — a different world with a different owner. Addendum 1 asserted the
first because reading the code made it look like the only path, and that is
exactly the mistake this file keeps warning about. So the stage is now recorded
where it happens (`PEER_STAGE_*`, and the note prints it):

| stage | what the note says | what it means |
|---|---|---|
| none | no socket was attempted | no peer configured — not a failure at all |
| socket | `socket()` would not make a descriptor at all | the guest has no descriptor; the address was never reached |
| connect | `socket()` worked and `connect()` refused it | the descriptor is fine; it is the address that was refused |

All four states, natively, no root: no peer → *none*; a closed port → *connect*
(`Connection refused`); `ulimit -n 4` so the second `socket()` cannot get a
descriptor → *socket* (`Too many open files`); a live echo-and-hold peer → both
`recvmsg` legs `RETURNED` and verdict (Б). The old note said "no connected
sockets (…)" for the last two, and the two are not the same finding.

**What the guest run will now settle that the last one could not:** the
`EAFNOSUPPORT` of the 10:55 run is, by the code, from either call, and this probe
cannot tell you which. The rebuilt binary can. If it says *socket*, then the guest
cannot make a descriptor at all and the next root is in the socket path itself; if
it says *connect*, then descriptors work and the refusal is about reaching the
address — a different layer, and the descriptor leg is one `connect` away from
working rather than one `socket` away.

## 14. Reproduce

```sh
export DARLING_SRC_DIR="$PWD"                       # this checkout
export DARLING_OVERLAY=/path/to/overlay             # the overlay you build against
export DARLING_BUILD_DIR=/path/to/build             # scratch, outside the source tree

# §9's long-name fixtures, the pair that shows the line is in bytes. NAME_MAX
# here is 255, so 254 is one below it and a record is 280 bytes.
O="$DARLING_OVERLAY/usr/lib/dir-threshold"
for d in l7 l8; do
        mkdir -p "$O/$d"
        for i in $(seq 1 $([ "$d" = l7 ] && echo 5 || echo 6)); do
                n="long-$(printf '%02d' $i)-"
                printf 'x' >"$O/$d/$n$(printf 'z%.0s' $(seq 1 $((254 - ${#n}))))"
        done
done

sh build-freebsd/fill-bundle-contents.sh            # Contents/ layout + both hashes
sh build-freebsd/fill-bundle-contents.sh backends   # Backends/ layout, §4
sh build-freebsd/fill-bundle-contents.sh framework-root  # framework root, §10
sh build-freebsd/fill-bundle-contents.sh remove contents   # and back to 4 entries
sh build-freebsd/fill-bundle-contents.sh remove backends   # and back to 3
sh build-freebsd/fill-bundle-contents.sh remove framework-root  # and back to 3

# §10's fourth wall, proved on the host with no root and no guest. The first
# name is what the backend builds; the second is the same name POSIX accepts.
# §13's oracle — the contract the elfcalls slots are filled under, and the
# table the guest must be compared against after the fix lands.
sh build-freebsd/shm-flags-contract.sh

# §13's fix, as a patch against the darling-xnu submodule.
git -C src/external/xnu apply --stat ../../build-freebsd/emu-shm-sem-flags.patch

# §10's fourth wall, proved on the host with no root and no guest. The first
# name is what the backend used to build; the second is the same name POSIX
# accepts.
cat >/tmp/shmname.c <<'EOF'
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <string.h>
#include <errno.h>
int main(void){const char*n[]={"./.bsdos-wlshm-0-1","/.bsdos-wlshm-0-1"};
for(unsigned i=0;i<2;i++){int fd=shm_open(n[i],O_RDWR|O_CREAT|O_EXCL,0600);
printf("shm_open(\"%s\") = %d errno=%d (%s)\n",n[i],fd,fd<0?errno:0,fd<0?strerror(errno):"-");
if(fd>=0){close(fd);shm_unlink(n[i]);}}return 0;}
EOF
cc -o /tmp/shmname /tmp/shmname.c && /tmp/shmname

sh build-freebsd/build-bundle-principal-class-test.sh
DRY_RUN=1 sh build-freebsd/run-bundle-principal-class.sh   # RC=0, 7 checks, no root
sh build-freebsd/run-bundle-principal-class.sh             # root run
grep '^\[probe\] dirent\|^\[probe\] before-load\|^\[probe\] principalClass' \
    "$DARLING_BUILD_DIR/bundle-principal-class.log"
grep '^\[probe\] rootscan\|^\[step\] separating' \           # §5, the separator
    "$DARLING_BUILD_DIR/bundle-principal-class.log"

# §5's third wall, without a root run: how many symlinks each root has.
find /tmp/darling-local-overlay -type l | wc -l            # 0 — the staged tree
find "$DARLING_OVERLAY/System/Library/Frameworks" -maxdepth 2 -type l | wc -l  # 54

DRY_RUN=1 sh build-freebsd/run-wayland-window-probe.sh     # RC=0, 8 checks, needs sway
sh build-freebsd/run-wayland-window-probe.sh               # root run, a seat
grep 'Available backends\|no NSPrincipalClass' \
    "$DARLING_BUILD_DIR/wayland-window-probe.log"

# §13's addendum: the guest thread lane. The peer is proven FIRST, without root,
# so the one prompt below cannot be spent discovering a dead peer. Two
# connections, not one — the probe refuses to measure a leg with half a peer.
#
# 2026-09-30, step 2 of the addendum — THE PRIMITIVE IS NOW CHECKED AT THE
# BINARY LEVEL, BEFORE ANY RUN. The previous step grepped the probe's SOURCE for
# the wrappers it must not call, which is a claim about this file and not about
# what this file's waits turn into. The guest's semaphore wait is a syscall the
# source cannot see: llvm-nm of the probe shows only _sem_init/_sem_wait, and
# the number lives behind __darling_bsd_syscall inside an installed dylib.
#
# CORRECTED 23:2x, after the run: this step as first written checked the wrong
# table and would have sent the next reader to edit a file whose table entry
# changes nothing. Two greps that were the whole of it:
#
#   llvm-objdump … | grep -A2 '^_sem_wait:$'      # -> 271
#   grep -cE '^#define (LINUX|MACOS)_SYS_… +271$' freebsd_syscall_trap.c   # -> 0
#
# Both are true and the conclusion drawn from them ("the trap defines 271
# nowhere, therefore the wait is unprovided") was wrong. 271 never reaches that
# trap: __darling_bsd_syscall is a function-pointer table inside the overlay's
# own libsystem_kernel.dylib, not SIGSYS interception, and the guest's table
# ALREADY carries [271] = sys_sem_wait (bsd_syscall_table.c:377) wired to
# elfcalls()->sem_wait, which mldr fills with the host's sem_wait
# (elfcalls.c:112). A run confirms it from the other side: the log's only
# unhandled Linux syscall is 99 (brk); 271 never appears.
#
# What is actually missing is EARLIER, and this is the step that finds it:
# _sem_init is a stub that returns -1/ENOSYS without touching the object. Every
# sem_wait in the probe was therefore reading an uninitialised int, and the
# EINVAL it draws belongs to the initialisation. Check it, and check it BEFORE
# the wait's number is allowed to mean anything:
llvm-objdump --macho --disassemble \
    "$DARLING_OVERLAY/usr/lib/system/libsystem_kernel.dylib" \
  | grep -A9 '^_sem_init:$'
# _sem_init:  … callq ___error ; movl $0x4e, (%rax) ; movl $0xffffffff, %eax
# 0x4e = 78 = ENOSYS, and NO store to the semaphore: it is never initialised.
# _sem_destroy is the same shape; _sem_wait (271) and _sem_post (273) are real.
#
# 2026-09-30 23:5x — AND THE STUB IS UPSTREAM'S, DELIBERATELY. The body is in
# the tree, not just in the binary:
#   src/external/xnu/darling/src/libsystem_kernel/libsyscall/wrappers/
#       posix_sem_obsolete.c
# and it carries the reason in its own header comment — "system call stubs are
# no longer generated for these from syscalls.master. Instead, provide simple
# stubs here" — with sem_destroy, sem_getvalue and sem_init all returning
# errno = ENOSYS, -1. The SDK agrees and says why: sys/semaphore.h marks those
# three __deprecated and does not deprecate sem_open. So this is XNU retiring
# the anonymous POSIX semaphore, not a damaged build and not a darling defect,
# and "fix sem_init" would mean patching upstream code Apple deprecated.
#
# Which makes the constructor the real question, and it has an answer that needs
# no guest rebuild: sem_open is implemented. It is not in the obsolete file —
# it is sys_sem_open in .../xnu_syscall/bsd/impl/wrapped/sem_open.c, calling
# elfcalls()->sem_open (filled by mldr from the host's sem_open, elfcalls.c:111),
# and the host really has one. Measured on the build machine, where the answer
# is known, parked and released:
#   sem_open("/b358probe2", O_CREAT|O_EXCL, 0600, 0) -> 0x8248eb000
#   sem_wait returned rc=0 errno=0 after a 200ms poster  => PARKED then RELEASED
# So the wait this lane needs already works on a semaphore built the supported
# way. What is NOT measured, and is the only thing standing between that and a
# runnable lane: sem_open takes a NAME, and whether a named semaphore lands
# where the guest's vchroot can see it is untested. Do not assume it.
#
# The lesson is the one this addendum keeps re-learning: a grep that returns a
# confident 0 is evidence about the file it was pointed at, not about the
# subsystem. Point the next one at the table the call actually reaches.

nohup python3 - <<'EOF' >"$DARLING_BUILD_DIR/peer.log" 2>&1 &
import socket, threading
def serve(c):
    while True:
        d = c.recv(64)
        if not d: break
        c.send(d)
s = socket.socket(); s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
s.bind(("127.0.0.1", 0)); s.listen(16)
open("/tmp/peer.port", "w").write(str(s.getsockname()[1]))
while True:
    c, _ = s.accept()
    threading.Thread(target=serve, args=(c,), daemon=True).start()
EOF
sleep 1; PEER=$(cat /tmp/peer.port)

# ONE authorization for the whole run, output to a FILE (a pipe whose reader goes
# away is what used to leave darlingserver blocked in select), under a timeout —
# parking the legs is the expected result and the timeout is what measures it.
timeout --foreground -k 60 120 sudo env \
    DARLING_SRC_DIR="$DARLING_SRC_DIR" DARLING_OVERLAY="$DARLING_OVERLAY" \
    DARLING_BUILD_DIR="$DARLING_BUILD_DIR" \
    DARLING_TEST_BINARY=guest-thread-wait-macho DARLING_THREAD_PEER_PORT="$PEER" \
    "$DARLING_BUILD_DIR/launch-dynamic" > "$DARLING_BUILD_DIR/thread-lane.log" 2>&1

grep -aE 'sem_wait|recvmsg|^VERDICT|^  \(|Cannot open' \
    "$DARLING_BUILD_DIR/thread-lane.log" | grep -av patch_linux_raw

# The same probe natively, no root: this is where the answer is known, and it
# must be (none) with both legs measured. A probe that cannot pass where the
# answer is known is not finished. The released legs need no peer, so neither
# does this self-test.
cc -Wall -pthread -o /tmp/gtw-native tests/src/guest-thread-wait.c
timeout -k 5 90 /tmp/gtw-native                       # RC=0, verdict (B)

# ... and the negative controls, which is what makes (B) mean something: a
# verdict that cannot fail is a constant. One sem_post replaced by (void)0, in a
# COPY — the tracked file carries neither sabotage.
sed 's|sem_post(&lock);              /\* the release, and the only one \*/|(void)0;|' \
    tests/src/guest-thread-wait.c > /tmp/gtw-sabotage.c
cc -pthread -o /tmp/gtw-sabotage /tmp/gtw-sabotage.c
timeout -k 5 90 /tmp/gtw-sabotage                     # (A): the spawned thread

# The guest run, one authorization, no peer: the released legs decide the lane
# on 202 and 228 alone, and the descriptor leg prints "not exercised" honestly.
timeout --foreground -k 60 120 sudo env \
    DARLING_SRC_DIR="$DARLING_SRC_DIR" DARLING_OVERLAY="$DARLING_OVERLAY" \
    DARLING_BUILD_DIR="$DARLING_BUILD_DIR" \
    DARLING_TEST_BINARY=guest-thread-wait-macho \
    "$DARLING_BUILD_DIR/launch-dynamic" > "$DARLING_BUILD_DIR/thread-lane.log" 2>&1
grep -aE 'after post|sem_wait|^VERDICT|^  \(A|^  \(B|^  \(none|^  \(not' \
    "$DARLING_BUILD_DIR/thread-lane.log"

# Which call refused is now a reported fact, and all three stages are reachable
# without a guest. A closed port refuses connect(); the socket() stage needs the
# FIRST connection to SUCCEED and take the last descriptor — against a dead port
# both connects fail fast, each closing its own descriptor, and the limit is never
# reached, so the recipe looks right and does not reproduce. Output goes to a FILE,
# because the limit starves a pipeline before the probe can report anything.
CLOSED=$(python3 -c "import socket;s=socket.socket();s.bind(('127.0.0.1',0));p=s.getsockname()[1];s.close();print(p)")
DARLING_THREAD_PEER_PORT=$CLOSED timeout -k 5 60 /tmp/gtw-native 2>&1 | grep -a 'note:'
sh -c "ulimit -n 4; DARLING_THREAD_PEER_PORT=$PEER exec /tmp/gtw-native" >/tmp/lim.txt 2>&1
grep -a 'note:' /tmp/lim.txt

# And the syscall discipline, at the BINARY rather than in the source: a grep of
# the source cannot see what a macro or an inline pulled in.
llvm-nm -u tests/guest-thread-wait-macho | awk '{print $NF}' | sort \
  | grep -E '^_(poll|writev|pipe|select|nanosleep|shutdown|bind|listen|getsockname|socketpair|accept)$'
                                                          # empty, and that is the point

# The last three greps together are the whole correction, and they are three
# different kinds of check. The one above asks which wrappers the probe CALLS.
# The one before it asks which syscall the wrapper it calls REACHES: the source
# was clean, the binary imported only _sem_init/_sem_wait, and _sem_wait turned
# out to be 271 rather than the 202 this recipe assumed. A discipline that only
# checks the first cannot see a wait that is clean at the source and absent at
# the syscall. The third asks whether the syscall the wrapper reaches is
# actually PROVIDED — and that is the one that finds the real gap, because 271
# is provided (the guest's own table has it) while _sem_init, which runs BEFORE
# it, is not.
#
# What the peerless run reports, 2026-09-30 23:1x, and the reason the rubric's A
# and B are not assigned: every sem_wait is REFUSED with EINVAL, and now the
# probe prints WHY, which is the point of the last change. sem_init itself
# returns -1/ENOSYS, so the semaphore was never initialised and the wait read an
# uninitialised int; the EINVAL belongs to that, not to the wait. Earlier runs
# of this lane attributed it to the wait and would have sent a fix to a syscall
# table that was never the problem.
#
# The probe therefore prints every sem_init's rc/errno beside every sem_wait's,
# and the verdict has a branch that says the wait's errno is inherited. Read it
# with BOTH rows, or the number without its row above it is unreadable:
grep -aE 'sem_init|sem_wait|after post|REFUSED|^VERDICT|^  \(refused|^  \(A|^  \(B' \
    "$DARLING_BUILD_DIR/thread-lane.log"
#   main   sem_wait     REFUSED rc=-1 errno=Invalid argument over 0ms
#   main   sem_init     rc=-1 errno=Function not implemented
#
# A risk recorded and NOT measured, for whoever fixes sem_init: the guest's
# sem_t is `typedef int` (sys/semaphore.h — 4 bytes) while the host's is a
# 16-byte struct, so handing one to the other's sem_wait is a type substitution
# that may draw the same EINVAL even once initialisation works. Prove it with a
# run; do not assume the fix lands.
```

Logs stay in `$DARLING_BUILD_DIR`, outside the source tree, and are machine
artefacts full of addresses; they are quoted here, not committed.

# §13 addendum, step 3 — THE SOCKET HALF OF THE DISCRIMINATOR: NOT MEASURED,
# AND THE CHANNEL IS WHAT CLOSED. 2026-10-01. New probe:
# tests/src/guest-socket-wait.c (the lane probe untouched). Question: do
# SOCKET waits return on a SPAWNED guest thread? The merged (B) said
# resumption is alive for nanosleep, a pipe read and sem_wait; wl's
# roundtrip from a spawned thread hangs (run №9-4), and roundtrip's blocking
# work is a socket read multiplexed with a readiness wait — so this asks the
# socket half directly, main-thread control + spawned thread per leg.
#
# MEASURED, native self-test (the answer is known here, so the probe must
# pass): cc -Wall -pthread -o /tmp/gsw-native tests/src/guest-socket-wait.c
#   main   read       RETURNED (parked) rc=1 errno=0 over 2000ms
#   thread read       RETURNED (parked) rc=2 errno=0 over 2000ms
#   main/thread poll  returned on readiness, 2000ms
#   thread conn+rd    RETURNED (parked) over 2000ms
#   VERDICT (B) — socket reads return on spawned threads, natively.
# Sabotage control (release write deleted in a COPY, not in the tracked
# file): DID-NOT-RETURN printed on every spawned read leg and both poll
# legs, RC=0 — the deadline is real, so (B) above can fail and did not.
#
# MEASURED, guest peerless run (one authorization, file output):
#   timeout --foreground -k 60 180 sudo env DARLING_* \
#       DARLING_TEST_BINARY=guest-socket-wait-macho \
#       "$DARLING_BUILD_DIR/launch-dynamic" \
#       >"$DARLING_BUILD_DIR/socket-wait-peerless.log" 2>&1     # RC=0, 16868 B
# Every connected-socket source refuses BEFORE any wait can park:
#   socketpair       overlay sys_socketpair -> LINUX 134 -> trap has no
#                    134 -> ENOSYS. The guest table carries [135] but its
#                    body needs a Linux socketpair the trap never defines.
#   bind (self-port) overlay sys_bind -> LINUX 49 -> trap has no 49 ->
#                    ENOSYS: the guest cannot open a port itself.
#   AF_INET connect  EAFNOSUPPORT at the host-facing call (measured).
#   AF_UNIX connect  EAFNOSUPPORT TOO, on the same path shape the wayland
#                    socket uses — second run with the AF_UNIX echo peer:
#                    socket-wait-unix.log, RC=0, 16533 B.
#   VERDICT printed: (not exercised) — an INSTRUMENT verdict.
#
# NOT MEASURED: whether a socket wait returns on a spawned guest thread —
# there is no fd in this guest build on which to park one. The pipe half of
# the discriminator was already answered alive by the merged (B); the socket
# half is not a thread question here, it is a channel question. NOT MERGED,
# NOT MEASURED: the %gs/get_perthread_wd change — it stays unmerged until a
# run measures it, and this probe does not touch it. Correlation noted, not
# claimed: the AF_UNIX connect path runs get_perthread_wd() inside
# vchroot_expand (the %gs-backed per-thread wd), and sockaddr_fixup_from_bsd
# reads layout-correct on paper (bsd_family at offset 1, sun_path at 2) yet
# both families reach the host refused — whether the refusal rides on the wd
# is exactly what a measurement must decide, not a reading.
#
# §13 addendum, step 4 — THE CHANNEL IS OPEN: (B) INSIDE THE GUEST.
# 2026-10-01, on task/guest-socket-channel. Step 3's instrument verdict was
# "(not exercised)": every connected-socket source refused before any wait
# could park. This step opens the cheapest source and re-measures.
#
# WHAT WAS PATCHED (mldr's freebsd_syscall_trap.c, LINUX dispatch — the
# slice this task allows): plain BSD-lineage passthroughs the guest's
# overlay already forwards to but the trap never defined:
#   socketpair = 53  (NOT 134 — a table read said 134; a run said "unhandled
#                     Linux syscall 53" and the overlay's own linux-x86_64.h
#                     agrees: __NR_socketpair 53. Measurement over reading,
#                     the lesson of step 2, one level down.)
#   bind = 49, listen = 50, accept = 43 — the self-listener's calls.
#   poll = 7 — the guest's poll(395) lands in the overlay's
#             sys_pselect_nocancel which forwards here; the previous run
#             showed exactly two "unhandled Linux syscall 7" lines.
# Rebuilt by the usual recipe: sh build-freebsd/build-mldr-only.sh (RC=0,
# installed to the canonical mldr path; the script's own install-or-fatal
# step did not fire).
#
# MEASURED, guest run (one authorization, file output;
# socket-wait-final.log, RC=0):
#   socketpair: available here
#   main   read       RETURNED (parked) rc=1 errno=0 over 2000ms
#   thread read       RETURNED-NO-PARK rc=1 errno=0 over 1998ms
#   main   poll       RETURNED-NO-PARK rc=1 errno=0 over 2001ms
#   thread poll       RETURNED-NO-PARK rc=1 errno=0 over 2000ms
#   VERDICT (B) via socketpair: both the main-thread control and the
#   spawned thread came back from a released blocking SOCKET read — and
#   poll(POLLIN) returned on readiness on both thread kinds as well. So
#   inside this guest, socket READS and socket READINESS both resume on
#   spawned threads. Native self-test stayed (B) (RC=0) and the sabotage
#   control still prints DID-NOT-RETURN on every spawned leg (RC=0), so
#   the verdict can fail and did not.
#
# The (B) verdict's own sentence — "narrows to the readiness/multiplexing
# half" — is now HALF-RETIRED: readiness returned too. What remains of the
# roundtrip hang's suspects: libwayland's own machinery, the epoll side
# (LINUX 213/232/233 are already in the trap and measurable next), and the
# sysinfo spam below.
#
# MEASURED, the EAFNOSUPPORT bisect (the correlation step 3 left open):
# the trap's new bind case prints the sockaddr bytes it receives, and they
# are CLEAN — [gsw-bisect] bind fd=3 len=16 bytes=02 00 b8 ce (family 2 =
# AF_INET little-endian, port 0xb8ce = the probe's 47310) — while the host
# bind still refuses with EAFNOSUPPORT. So the break is AT OR BELOW the
# host bind call, NOT in sockaddr_fixup_from_bsd and NOT in
# vchroot_expand/get_perthread_wd for AF_INET (the fixup only touches the
# wd for PF_LOCAL). The %gs/get_perthread_wd correlation therefore stays
# UNMEASURED but UNIMPLICATED for this path; it stays unmerged. The
# connect-side byte print never fired — the self-listener dies at bind, so
# no connect was ever attempted with a valid fd. A host-side look at
# kern_bind is the next measurement; the self-listener leg (probe leg 3)
# stays blocked until it.
#
# MEASURED, noise with a name: 1 243 648 lines of "[darling-mldr]
# unhandled Linux syscall 99 — ENOSYS" span the leg window of the final
# run (65 MB log). 99 is __NR_sysinfo in the overlay's linux-x86_64.h —
# NOT brk: __NR_brk is 12 there, so step 2's note calling 99 "brk" was
# wrong and is corrected here. A guest thread loops sysinfo while the legs
# run; the results are unaffected (RC=0, verdict printed). Follow-up: a
# print of a1 in the trap's default case names the caller.
#
# §13 addendum, step 5 — THE ROUNDTRIP STAGES DO NOT PARK: ATTRIBUTION BY
# EXCLUSION, AND WHAT IS LEFT. 2026-10-01, on task/wl-roundtrip-stage.
# Step 4 opened the socket channel and measured (B) — socket reads and poll
# readiness both return on spawned guest threads — so №9-4's hang is not
# the descriptor/event path. This step asks WHERE the roundtrip parks, by
# measuring its own body.
#
# THE PROBE: tests/src/guest-wl-roundtrip-stage.c (new; pure C; libSystem
# only). It loads the vendored backend the way run №9-3 loaded it — dlopen
# by the dylib's GUEST path, dlsym per function, every resolution printed —
# and decomposes the roundtrip into its three stages, each with a marker,
# rc/errno and its own clock duration: marshal wl_display.sync +
# wl_display_flush (the write), poll(POLLIN) on wl_display_get_fd (the
# readiness wait), wl_display_dispatch + the sync callback done (the
# read/dispatch). libwayland's roundtrip_queue IS this sequence, so a park
# at stage N is an attribution. wl_display_dispatch stands in for
# dispatch_queue: this build exports dispatch, not dispatch_queue, and
# №9-4's roundtrip used the default queue anyway.
#
# THE RUN'S OWN WALL FIRST: the first attempt died at dlopen — "Library not
# loaded: /System/Library/PrivateFrameworks/Onyx2D.framework/Versions/A/
# Onyx2D" — because the harness's built-in staging list covers usr/lib and
# System/Library/Frameworks but not PrivateFrameworks, and the backend dylib
# links Onyx2D. The closure-derived list (check-guest-dylib-compat.py
# --emit-staging-trees, handed over as DARLING_STAGING_TREES) stages all
# three trees and the load proceeds. Measured, and it is exactly the miss
# run-wayland-window-probe.sh's step 3b exists to prevent.
#
# MEASURED, guest run (one sudo, file output; wl-stage2.log, RC=0):
#   [negative control] connect(gsw-dead-nope-0000) REFUSED errno=No such
#     file or directory — the instrument distinguishes refusal from hang.
#   (a) main at rest:  marshal+flush RETURNED rc=12 over 0ms; poll RETURNED
#       rc=1 over 1ms; dispatch RETURNED rc=2 over 0ms; sync callback
#       FIRED. The lane completed.
#   (b) main in bounded join: the same, all stages RETURNED, callback fired.
#   (c) main concurrent — №9-4's exact shape: "main-lane: roundtrip
#       returned 2 (errno 0), wl_display_get_error=0" AND the spawned lane
#       completed its decomposition on the same display. Both lanes got
#       their replies.
#   VERDICT: (none) — all three variants completed.
#
# ATTRIBUTION: on a FRESH connection the roundtrip's stages do not park on
# a spawned thread, with main at rest, main joined, or main concurrently
# roundtripping the same display. So the park is NOT flush, NOT poll, NOT
# dispatch, and NOT contention for the shared default queue — every
# primitive the roundtrip is made of is measured alive. What №9-4 had and
# this probe's fresh display does not: the backend's own stateful session —
# registry globals bound, shm pool, xdg surface, a committed window, and
# the backend's listeners and queues attached to that display. The park
# lives in that stateful/backend layer, and naming the exact stage inside
# it needs the next probe: drive the vendored backend to that state (the
# window-probe lineage — NSBundle, -[WaylandDisplay waylandDisplay], dlsym
# roundtrip) and decompose THERE. Recorded, not assumed: this run excludes
# the primitives; it does not yet name the stateful stage.
#
# Side measurements: sway answered the sync in 0-1ms on the headless seat,
# so the poll stage had real traffic to wait for. The sysinfo(99) spam was
# 598 lines here against 1 243 648 in the socket-wait run — the spam
# scales with the socketpair probe's path, not with wl traffic; its caller
# is still unnamed.
#
# §13 addendum, step 6 — THE PARK IS NATIVE, NOT STATEFUL: NAMED BY THE
# ONE STAGE THE DECOMPOSITION COULD NOT REACH. 2026-10-01, on
# task/wl-session-roundtrip. Step 5 excluded the primitives on a fresh
# display; this step builds the backend's stateful session (run №9-4's
# setup) and adds the variant that decides the task's own premise.
#
# THE PROBE: tests/src/guest-wl-session-roundtrip.c (new; ObjC + Foundation
# + AppKit link, backend dlopen'd by its guest path like run №9-3). Session
# stages with markers: bundle-load, display-init (the backend's registry
# roundtrip + binds — compositor/shm/wmBase all bound, printed), window-
# create, shm-pool (_acquireBackBuffer + pixel write proving the mmap),
# commit (flushBuffer), xdg-configure receipt via a main-thread roundtrip.
# Negative control after the backend loads. Staging:
#   STAGING_LIST="$(paste -sd: $DARLING_BUILD_DIR/staging-trees.txt)"
#   sudo env ... DARLING_STAGING_TREES="$STAGING_LIST" ...
# (the closure-derived list — Frameworks + PrivateFrameworks + usr/lib; the
# built-in list misses Onyx2D, step 5's lesson, and dyld refuses the load.)
#
# MEASURED, guest run (sway headless; wl-session3.log, RC=0):
#   session built: compositor=0x…9560 shm=0x…94a0 wmBase=0x…95c0; window,
#   shm buffer, pixel write, commit and the xdg receipt all succeeded.
#   negative control: connect(gsw-dead-nope-0000) returned NULL — refusal
#   observed; errno came back 0 in this run and ENOENT in step 5's run on
#   the same kind of call, so NULL is the refusal signal here and errno is
#   NOT trustworthy at this seam (recorded, not smoothed over).
#   (b) main dispatches (roundtrip), spawned lane DECOMPOSES on the
#       stateful display: marshal+flush RETURNED rc=12 over 6ms; poll
#       RETURNED rc=1 over 1ms; dispatch RETURNED rc=2; sync callback
#       FIRED. The decomposed roundtrip COMPLETES from a guest thread even
#       while main concurrently roundtrips the same display.
#   (a) main at rest, spawned runs the OPAQUE wl_display_roundtrip:
#       DID-NOT-RETURN within 8000ms — parked.
#   (d) FRESH display (no session), spawned runs the opaque roundtrip:
#       DID-NOT-RETURN within 8000ms — PARKED ON A FRESH DISPLAY TOO.
#   (c) FULL №9-4 reproduction, the window probe's own tail: thread lane
#       started first; main-lane roundtrip returned 2, get_error=0;
#       thread-lane answered=0. LANE FINDING REPRODUCED.
#
# ATTRIBUTION: the task's premise — that the park lives in the stateful
# layer — is REFUTED by variant (d): the same opaque call parks on a
# display with no registry, no shm, no xdg. What the disassembly then
# names: the vendored dylib's wl_display_roundtrip (0x62e0) is a LAZY
# TRAMPOLINE — lea "wl_display_roundtrip" → __lazy → cmp NULL → callq *%rax
# — a jump into the NATIVE libwayland-client resolved via _elfcalls, not a
# reimplementation. So the park is inside native libwayland's roundtrip
# machinery as it runs on a guest-created thread, and the measured
# difference between the lanes pins WHICH sub-stage: the decomposed lane
# always entered dispatch with the reply ALREADY BUFFERED (its own guest
# poll did the waiting, 1ms), so it exercised the buffered-processing path
# — and completed. The roundtrip's first dispatch finds the buffer EMPTY
# and descends into the blocking READ path — wl_display's prepare_read/
# poll/read sequence with the display's own mutexes — and THAT sub-stage
# parks on guest threads while the identical call returns on main. One
# sub-stage the decomposition structurally could not reach, and it is the
# one that parks.
#
# NOT CLAIMED: the mechanism INSIDE the native read path (native-pthread
# bookkeeping around a thread the host layer did not create is the leading
# candidate — libwayland's read path serialises on the display's mutexes
# and condition variables — but no run here opens native libwayland's
# internals). mldr/trap were not touched: the trap's poll/socketpair were
# measured alive in steps 4-5 and are not on this path's way to failing.
#
# §13 addendum, step 7 — THE 2x2 FILLS WITH RETURNS: THE EMPTY-BUFFER
# CONDITION AND THE MARSHAL PATH ARE BOTH REFUTED; THE PARK'S LAST HIDEOUT
# IS NAMED. 2026-10-01, on task/wl-empty-buffer-dispatch. Step 6 attributed
# the park to "native libwayland's blocking read path on a guest-created
# thread" — with one structural caveat it could not remove: the decomposed
# lane always entered dispatch with the reply already waiting (its own poll
# did the waiting), so "empty buffer at entry" and "guest thread" were
# never separated. This probe separates them, cell by cell.
#
# THE PROBE: tests/src/guest-wl-empty-buffer-dispatch.c (new; pure C;
# libSystem-only link; backend dlopen'd by guest path, C surface dlsym'd —
# the surface every probe since step 5 uses). Fresh display per cell (a
# parked thread poisons its display). Staging: DARLING_STAGING_TREES from
# the closure-derived list (Frameworks + PrivateFrameworks + usr/lib).
#
# MEASURED, guest run (sway headless; wl-emptybuf2.log, RC=0) — the 2x2
# plus two analysis cells, ALL RETURNED:
#   cell4 full    x main    dispatch RETURNED rc=2 over 0ms  (the 0f00018b control, one line)
#   cell3 pre-buf x thread  marshal rc=12, own-poll rc=1, dispatch RETURNED rc=2 over 0ms
#   cell1 empty   x thread  marshal rc=12, dispatch RETURNED rc=2 over 0-1ms
#   cell2 empty   x main    marshal rc=12, dispatch RETURNED rc=2 over 0ms (main unbounded by design)
#   cell5 empty   x thread, flags-marshal (the wl_display_sync form)
#                            marshal rc=12, dispatch RETURNED rc=2 over 0ms
#   cell6 empty   x main,  flags-marshal  dispatch RETURNED rc=2 over 1ms
#   negative control: connect(gsw-dead-nope-0000) returned NULL — refusal
#   observed; errno 0 again (step 6's nuance: NULL is the signal here).
#
# WHAT THE DURATIONS PROVE: cell1's dispatch was entered BEFORE the reply
# could arrive (marshal+flush+dispatch take microseconds; sway answers in
# milliseconds) and its 0-1ms duration is a WAIT, not a pre-buffered
# straight-through — so the empty-at-entry condition was genuinely
# exercised on a guest thread, and the call RETURNED. The native blocking
# read from a guest thread works.
#
# ATTRIBUTION, corrected by measurement: step 6's "native read path parks
# on guest threads" is REFUTED at call level. So is the task's
# empty-buffer premise (cell1 returned) and, by the analysis cells, the
# marshal-path hypothesis (the wl_proxy_marshal_flags form that
# wl_display_sync uses internally returns from guest threads too). Every
# exported primitive — marshal in both forms, flush, poll, dispatch,
# listener callbacks — returns from guest threads in every shape tested,
# and the FULL №9-4 control from 0f00018b (main answered, spawned blocked)
# still stands on the opaque call. The park reproduces ONLY inside
# wl_display_roundtrip itself.
#
# THE REMAINING DELTA, named as far as the exported surface allows: what
# the opaque roundtrip does that no cell does is wl_display_sync's own
# internal listener, wl_proxy_set_queue on the callback, and the dispatch
# LOOP until done — roundtrip's body above the primitives. The exported
# surface cannot decompose further without opening native libwayland,
# which this slice's boundaries forbid; the host-side slice takes exactly
# that step. The premise chain, each link measured rather than assumed:
# primitives alive (step 5) -> stateful layer (refuted by 0f00018b's
# variant d) -> native read path (refuted here by cell1) -> roundtrip's
# own body on a guest thread (the current, final-at-this-depth name).
#
# FOLLOWS: the host-side slice opens native libwayland's roundtrip
# (sync-listener/set_queue/loop) around a guest-created thread — the one
# code path this probe, by its boundaries, could only circle. No guest or
# trap change is implicated by anything measured so far: every trap-side
# primitive (steps 4-5) and every exported libwayland call (here) behaves
# on spawned threads.




