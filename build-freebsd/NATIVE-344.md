# NATIVE-344 — the experiment that could not work, and the rule it uncovered

Base: `pr-arm64` = `55c8f61b7` (the merged `task/dir-syscall-344`).
Two root runs, the whole budget. No fix is shipped, and §1 says why that is the
right outcome rather than a shortfall.

**Short answer: a handler in mldr cannot intercept syscall 344, because the
guest's own syscall wrappers never issue a syscall instruction — they dispatch
through a function-pointer table inside `libsystem_kernel.dylib`. The experiment
therefore proved nothing about the host and a great deal about the bug: the
`EINVAL` is not about the path, the depth, the framework, or the descriptor.
Across twelve directories, with identical fd number, device, mode and buffer
length, the calls that fail are exactly the directories holding 3 or 4 entries,
and every directory holding 10 or more succeeds.**

## 1. Why the handler is unreachable — the finding that ends option (б)

`___getdirentries64` in `libsystem_kernel.dylib+0x42dbc` does
`movl $0x158,%eax; callq __darling_bsd_syscall`. That helper, at `+0x50030`,
is not a syscall at all:

```
__darling_bsd_syscall:
   50039:  leaq  ___bsd_syscall_table(%rip), %r10
   50040:  movq  (%r10,%rax,8), %r10     ; table[344]
   50044:  testq %r10, %r10
   50047:  je    .no_sys                 ; NULL slot
   5005b:  callq *%r10                   ; call the handler directly
   ...
.no_sys:
   50070:  movq  %rax, %rdi
   50073:  callq ___unknown_syscall      ; NULL slot -> this
```

There is no `syscall` instruction anywhere on this path. mldr's
`freebsd_syscall_trap.c` intercepts by **SIGSYS on the `syscall` instruction**,
which is why its `MACOS_SYS_*` cases exist at all: they exist for callers that
issue raw BSD syscalls (dyld's early bootstrap, the upstream dyld overlay's raw
Linux-ABI calls — see the file's own `LINUX_SYS_*` section). libsystem's wrappers
are not such callers, so 344 never reaches the trap. The 600-entry table is the
dispatch mechanism, and its slot for 344 is filled by the emulation layer — the
same `__bsd_syscall_table` that `EMU-344.md` §3 identified as declared-but-not-
defined in our tree.

The handler was written, built, installed and exercised anyway, and the run
says what was already true from the disassembly: nothing changed. Every number
in `DIR-ENUM.md` §2 came back identical — `-1/EINVAL` at all six buffer sizes,
`readdir` returning 0 entries, `infoDictionary count = 0`, `principalClass =
(nil)`. The trap printed no `unhandled macOS BSD syscall 344` either, which is
the confirming half: had the call reached the switch, it would have been
answered there and the absence of the message would be silence, but the absence
together with the unchanged result is the signature of a path that never
arrives.

So the handler is reverted, not shipped. Unreachable code that reads like a fix
is worse than no code: the next person would find it in the tree and assume the
wall was addressed.

One thing from the attempt is worth keeping, and it is in this document rather
than in the source: the two directory layouts, and the fact that they disagree
in a way a memcpy hides — FreeBSD's ino64 `struct dirent` has `d_type` at 18 and
`d_namlen` at 20, Darwin's has `d_namlen` at 18 and `d_type` at 20, and FreeBSD
rounds `d_reclen` up to 8 where Darwin rounds up to 4 (measured: namlen 1 gives
32 here, 28 there). Whoever fixes the emulation needs both, and the attempted
handler carried `_Static_assert`s on every host offset — deliberately broken one
to confirm the assert was not vacuous, and it failed the build by name, which is
the only way to know an assert works.

## 2. What the run did prove: the selectivity is a count, not a directory

`__darling_bsd_syscall`'s handler receives only `(fd, buf, len, basep)`. In the
matrix every call used the same buffer, the same `len` (8192) and — as the run
now prints — the same fd number and device:

```
len=7  fd=3 dev=64 ino=963352    mode=0755  /System                              -> -1  errno=22
len=15 fd=3 dev=64 ino=963357    mode=0755  /System/Library                      -> 136
len=26 fd=3 dev=64 ino=963365    mode=0755  /System/Library/Frameworks           -> 344
len=45 fd=3 dev=64 ino=882947    mode=0755  /System/Library/Frameworks/AVFAudio.framework       -> -1  errno=22
len=47 fd=3 dev=64 ino=1124781   mode=0755  /System/Library/Frameworks/Foundation.framework     -> -1  errno=22
len=51 fd=3 dev=64 ino=963384    mode=0755  /System/Library/Frameworks/CoreFoundation.framework -> -1  errno=22
len=43 fd=3 dev=64 ino=1043979   mode=0755  /System/Library/Frameworks/AppKit.framework         -> -1  errno=22
len=52 fd=3 dev=64 ino=1043980   mode=0755  .../AppKit.framework/Versions                       -> -1  errno=22
len=54 fd=3 dev=64 ino=8186953   mode=0755  .../AppKit.framework/Versions/C                     -> -1  errno=22
len=8  fd=3 dev=64 ino=20713112  mode=0755  /usr/lib                             -> 392
len=15 fd=3 dev=64 ino=163634    mode=0755  /usr/lib/system                      -> 364
```

Same fd, same device, same mode, same length, same buffer. So the deciding
input is the inode, and the question becomes what property of it separates the
two groups. Counted on the host, in the same staged tree the guest was reading:

| directory | entries incl. `.` and `..` | result |
|---|---|---|
| `/System` | **3** | EINVAL |
| `/System/Library/Frameworks/AVFAudio.framework` | **3** | EINVAL |
| `…/Foundation.framework` | **3** | EINVAL |
| `…/CoreFoundation.framework` | **3** | EINVAL |
| `…/AppKit.framework` | **3** | EINVAL |
| `…/AppKit.framework/Versions` | **3** | EINVAL |
| `…/AppKit.framework/Versions/C` | **4** | EINVAL |
| `/System/Library` | 10 | ok |
| `/` | 12 | ok |
| `/usr/lib/system` | 36 | ok |
| `/System/Library/Frameworks` | 62 | ok |
| `/usr/lib` | 93 | ok |

**Twelve of twelve, with no counter-example: 3 and 4 entries fail, 10 and more
succeed.** Nothing else in the data separates the two groups — the failing
inodes (882947, 1043979, 1124781, 8186953, 963352, 963384) are interleaved with
the working ones (963357, 963365, 163634, 20713112) in no range, `/System` fails
while its own child works while its sibling-of-the-same-name three levels down
fails too, and the two runs agree exactly, so it is deterministic rather than a
cache that happened to fill the same way twice.

This also settles `EMU-344.md` §4.4 in the negative. The suspicion there was
"something about the specific directory, not about the path". The property *is*
about the directory, but it is not its identity — it is **how many entries it
holds**, which is content, not name, not location, not depth.

### What that looks like from the inside, as a lead and not a conclusion

A handler that answers `EINVAL` for a directory it can plainly read is doing a
sanity check on its own result and failing it. The shape that fits "fails on 3
and 4 entries, works on 10 and more" is an internal buffer or a loop that
requires more than one read — for instance a check of the form *did I get enough
back to be sure this was the whole directory*, or a chunk loop that needs a
second iteration to terminate and treats "one iteration" as failure. The
`EMU-344.md` §5 anomaly points the same way: `/usr/lib` holds 93 entries and
392 bytes came back, about four bytes per entry, impossible against a 24-byte
header. So the same handler also **under-returns on the directories it does
answer**. A threshold that is too high would explain both at once, and it is one
constant.

I have not read that code and am not going to guess at its line. The one grep
that would settle it, for whoever has the repository: the handler's own idea of
how many bytes or records constitute a successful read.

## 3. Reproduce

```sh
sh build-freebsd/build-bundle-principal-class-test.sh
DRY_RUN=1 sh build-freebsd/run-bundle-principal-class.sh   # RC=0, 7 checks
sh build-freebsd/run-bundle-principal-class.sh             # the root run
grep '^\[probe\] elsewhere:' "$DARLING_BUILD_DIR/bundle-principal-class.log"
```

The entry counts in §2 are countable without the guest: `ls -A <dir> | wc -l`
on the staged tree, plus two for `.` and `..`.

## 4. What is not settled

- **No fix.** Option (б) is closed by §1 — not "hard", closed: the call cannot
  reach that code. What is left is the owner question already on the card, and
  §2 hands it a rule instead of a shrug.
- **The rule is a correlation from one tree.** Twelve directories, one machine,
  one staging. It predicts that a directory with 5-9 entries fails; nothing has
  tested that, because no directory in this overlay has 5-9 entries. That is the
  cheapest possible confirmation for whoever picks it up, and it is a
  thirty-second check.
- **The mechanism is a lead.** "A threshold that is too high" explains both
  symptoms and names a constant, but it is inference from two observations, not
  a reading of the code.
- **The truncation is not fixed by anything here.** §2 restates `EMU-344.md` §5
  with a likely common cause; the guest still cannot list the directories it
  could not list before.
- Two root runs, the budget. The first was the experiment itself, the second the
  fd/device/mode/entry-count measurement that produced §2. Nothing was spent
  re-establishing what the first run had already shown.
