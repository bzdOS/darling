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
- **The mechanism is still a lead.** "Returns a prefix of the listing" is now
  measured in both directions and against a byte threshold (§9), but nothing
  in reach explains how long the prefix is.
- **Symlink entries do not survive into a guest listing** (§10). The staged
  tree has them, a symlinked path opens, and the entries themselves are absent
  from the listing. Emulation omits them, or delivers them with a zero inode
  and libc drops them — not separated, because the run's budget was spent.
- **The window itself is still not on screen.** Four walls are down and a
  `WaylandWindow` object exists, but the shm pool fails (§10) and the fix for
  that one is a byte in a binary whose sources are gone. Nothing here is a
  claim that a frame is drawn.

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

## 10. Both walls closed, the window is built, and the fourth wall is OURS

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
`Resources` unopenable. **The fourth wall is the shm pool**, and it is not the
emulation's:

```
WaylandWindow: shm allocation failed for 640x480 buffer: Invalid argument
```

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
a window; the last thing between here and a frame is a name our own backend
built wrong.

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

### One more thing the run turned up, and did not need a run to notice

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

## 11. Reproduce


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
