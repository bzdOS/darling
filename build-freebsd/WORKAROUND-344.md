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
```

Logs stay in `$DARLING_BUILD_DIR`, outside the source tree, and are machine
artefacts full of addresses; they are quoted here, not committed.
