# SLOT-344 — the threshold, measured exactly, and why the slot patch is not shippable yet

Base: `pr-arm64` = `90dd011b1` (the merged `task/native-344`).
One root run, for ход A. The second half of the наряд — patching slot 344 from
mldr — is **not** in this branch, and §3 says why in terms of what is readable,
not in terms of what was hard.

**Short answer: the threshold is 8 entries, exactly, measured on ten synthetic
directories that differ in nothing but their entry count. Below 8 the call
answers `EINVAL`; at 8 and above it answers records. And the slot patch cannot
be shipped, not because the symbol is hard to find — it is a local symbol in a
symtab that is present, at a fixed vmaddr — but because nothing inside the guest
writes that table, so the moment at which a patch would survive is decided by
code nobody in reach can read.**

## 1. Ход A — the threshold, to the entry

Ten directories under `$DARLING_OVERLAY/usr/lib/dir-threshold/`, holding
`N-2` files, i.e. `N` entries counting `.` and `..`. Same parent, same mode,
same names apart from the number, all `fd=3 dev=64`:

```
t3   -> -1  errno=22 (Invalid argument)
t4   -> -1  errno=22
t5   -> -1  errno=22
t6   -> -1  errno=22
t7   -> -1  errno=22
t8   -> 28   errno=0
t9   -> 56   errno=0
t10  -> 88   errno=0
t11  -> 120  errno=0
t14  -> 216  errno=0
```

**Eight entries is the line.** Seven fails, eight works. That is a change from
what `NATIVE-344.md` §4 left open: the prediction there was "5-9 entries fail",
and it is wrong on the upper side — 8 and 9 succeed. The lower side was right,
and the boundary that the real directories only bracketed (4 failing, 10
succeeding) is now pinned at a single step.

The twelve real directories still behave exactly as before, which is the check
that the fixture did not change the phenomenon:

```
3 entries   /System, AVFAudio.framework, Foundation.framework,
            CoreFoundation.framework, AppKit.framework, …/Versions      -> EINVAL
4 entries   …/AppKit.framework/Versions/C                               -> EINVAL
10 entries  /System/Library                                             -> 136
12 entries  /                                                           -> 200
36 entries  /usr/lib/system                                             -> 364
62 entries  /System/Library/Frameworks                                  -> 344
93 entries  /usr/lib                                                    -> 392
```

No fixture directory, and no real one, breaks the rule.

### The returned bytes, which are a second finding

The successes are exactly linear in the entry count, and the increment is one
record per entry — from 28 bytes at eight entries to 216 at fourteen, +28, +32,
+32, +96. Put the other way: at 14 entries, 216 bytes is 7 records' worth of
header (`24 + namelen + 1` rounded to 4, and the fixture names are six
characters), and 14 entries would need 13 of them. **The handler returns about
half of what a directory holds**, and the same ratio shows on `/usr/lib`: 93
entries, 392 bytes, roughly eight records.

This is `EMU-344.md` §5 restated with a control. §5 observed the shortfall on
real directories and could not say whether the cause was the directories. Ten
directories that differ only in count show the shortfall scaling with the count
and starting from the very first directory that answers at all — so it is the
handler's accounting, not a property of `/usr/lib`. It is the same shape as the
`EINVAL`: a handler that mis-measures how much of a directory it got, and
confidently reports failure when its own idea of "enough" is not met.

## 2. Ход A did not move the wall, and that is consistent

The directories the window probe needs are all small, which is why this rule
hides so well: `Wayland.backend/Contents` holds 4 entries (`Info.plist`,
`MacOS`, `.`, `..`), so it is below the line and answers `EINVAL`. Same after
this run, as before it:

```
[probe] dirent: 0 entries returned by readdir
[probe] after-load: infoDictionary count = 0
[probe] after-load: NSPrincipalClass = (absent)
[probe] principalClass = (nil)  <- the wall
```

So `PRINCIPAL-CLASS.md` §7 stays open — it should close the moment a directory
of four entries can be listed, which is what the slot patch would have bought.

## 3. Why the slot patch is not in this branch

The premise of ход B was: mldr hosts the guest in-process, so it can resolve
`___bsd_syscall_table` in the guest images and overwrite slot 344 with its own
handler *after the emulation layer has filled the table*.

Three of those four steps check out:

- **The table is real and locatable.** `llvm-nm` on the overlay's
  `usr/lib/system/libsystem_kernel.dylib`: `000000000009a020 d
  ___bsd_syscall_table` — a **local** symbol, in `__data`
  (`0x95c70`+). Not exported, so `dlsym` would never find it, but present in
  `LC_SYMTAB`, so a Mach-O symtab walk over the loaded image finds it and the
  runtime address is `load_address + 0x9a020`.
- **Patching slot 344 would fix both entry points.** The table has exactly two
  references in the image, and both are *reads* of it: `__darling_bsd_syscall`
  at `+0x50039` and the generic `syscall(2)` path `_sys_syscall` at `+0x5c674`,
  which bounds the number with `cmpq $0x258` — 0x258 = 600, the table's
  declared size.
- **The guest's address space is mldr's address space**, so writing the slot is
  a plain store.

The fourth does not, and it is the one the patch depends on. **Nothing in
`libsystem_kernel.dylib` writes the table** — the only two references are the
two reads above. The fill therefore comes from outside the image: from the
emulation layer, writing into the guest's memory during startup. The patch is
correct only if it lands *after* that write, and the write's timing is decided
by the code `EMU-344.md` established nobody here can read.

mldr's own code all runs before it jumps to dyld's entry point. If the fill
happens during guest initialisation — the usual shape, since the emu registers
when an image loads — then a patch placed anywhere in mldr is overwritten later,
and a run would look like "the patch did nothing" exactly like the trap patch in
`NATIVE-344.md` did. Spending the last root run on that would buy a second
negative of the same kind.

What would make it possible, in order of cost:

1. **One grep in `darling-emu`**: who writes `__bsd_syscall_table[344]`, and
   from where in startup. If the fill is synchronous with an image load that
   mldr performs, the patch belongs at the end of mldr's load phase and this
   becomes an hour's work. If it is a lazy registration on first use, it does
   not, and no amount of care in mldr will help.
2. **If the fill cannot be timed from mldr**, the alternative is a hook the
   guest can trigger after it is up — a thread in mldr waiting on the commpage
   or on the lifetime pipe, woken by the probe before it calls `opendir`. That
   is a new mechanism, not a patch, and it changes behaviour mid-run; it wants
   its own наряд and its own review.
3. **The honest alternative remains fixing the emulation layer** — the owner
   question already on the card — and §1 hands it a constant and a rule instead
   of a shrug.

## 4. Reproduce

The fixture lives in the overlay, not in this repository, because that is the
only place the guest can see it from; recreate it with

```sh
O="$DARLING_OVERLAY/usr/lib/dir-threshold"
for t in 3 4 5 6 7 8 9 10 11 14; do
        mkdir -p "$O/t$t"
        for i in $(seq 1 $((t - 2))); do echo "fixture t$t.$i" >"$O/t$t/f$i.txt"; done
done

sh build-freebsd/build-bundle-principal-class-test.sh
DRY_RUN=1 sh build-freebsd/run-bundle-principal-class.sh   # RC=0, 7 checks
sh build-freebsd/run-bundle-principal-class.sh             # the root run
grep '^\[probe\] elsewhere:' "$DARLING_BUILD_DIR/bundle-principal-class.log"
```

Entry counts can be checked without the guest at all: `ls -A "$O/t$t" | wc -l`,
plus two for `.` and `..`.

## 5. What is not settled

- **One root run**, the budget, spent on ход A. The slot patch was not attempted
  because §3 is a reading of the binaries, not a guess about them.
- **The threshold is 8 on this tree, under this emulation build.** It is a
  count, not a byte count — the fixtures prove that, since `t8` through `t14`
  all have records of similar size and only the count changes. Whether the
  constant is 8 entries or something that happens to be 8 for names of six
  characters is not separated: a fixture with eight entries of *long* names
  would separate the two in one more run, and I did not spend it.
- **The mechanism is still a lead.** "A sanity check on its own result, with
  the bar set too high" explains `EINVAL` below 8 and the half-count returns
  above it with one constant, but it is inference from two observations.
- **`/System` still fails at 3 entries and `/usr/lib/system` still succeeds at
  36**, which is the rule, not an exception — but it means the rule has never
  been tested against a directory with 5-7 entries that is *real*, only against
  fixtures.
- Nothing in `src/` is changed by this branch: no handler, no slot patch.
