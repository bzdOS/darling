# PROBE-RUN-THREE — the window probe, run with a working crash path

Base: `pr-arm64` = `1fd7868aa` (the merged `task/crash-diagnostics`).
One root run, the one this task is about.

**Short answer: the crash is not where the last two analyses put it. It is
`dyld::loadPhase6` faulting on its own stack — a 33,312-byte frame written into
a 64 KiB guest stack. H is confirmed and localised to one function; H2 is
refuted as the faulting code, though mldr is the reason the frame does not
fit.**

The last three lines of the log, verbatim:

```
dlopen_internal(.../Resources/Backends/Wayland.backend/Contents/MacOS/Wayland, 0x00000109)
[darling-mldr] FATAL signal 11 (code=1) at addr=0x7fffffdedf1c
  rip=0x0000000826ac1759  rax=0x41ca40ddb52a8565  rbx=0x00007fb96837c1d0
  rcx=0x00007fffffdf74b0  rdx=0x00007fffffdf7720  rsi=0x00007fffffdf61f0
  rdi=0x0000000000000003  rbp=0x00007fffffdf5f30  rsp=0x00007fffffdedd10
  ...
  guest stack dump at rsp=0x00007fffffdedd10:
  [gstack+   0] (unreadable)
  ... 80 slots, all of them ...
  [gstack+ 632] (unreadable)
```

## 1. The crash reproduces exactly

Run 2 and run 3 are the same failure, register for register. The only values
that differ are the ones that differ because the process was relocated:

| field | run 2 | run 3 | |
|---|---|---|---|
| signal / code | 11 / 1 | 11 / 1 | SAME |
| fault `addr` | `0x7fffffdedf1c` | `0x7fffffdedf1c` | SAME |
| `rsp` | `0x7fffffdedd10` | `0x7fffffdedd10` | SAME |
| `rbp` | `0x7fffffdf5f30` | `0x7fffffdf5f30` | SAME |
| `rcx rdx rsi rdi r13 r15` | — | — | SAME |
| `stack_top` | `0x7fffffdffdd0` | `0x7fffffdffdd0` | SAME |
| `mh` (probe) | `0x82562c000` | `0x8262f6000` | relocated |
| `rip` | `0x825e96759` | `0x826ac1759` | see §2 |

So the non-determinism hypothesis is dead: this is one deterministic bug, and
the load-string count that moved between runs 1 and 2 was never the variable.

## 2. What `rip` actually is: inside dyld, not in no image

`decode-crash.py` on this log says, correctly and for the first time:

```
images mapped: 59 address range(s) named by the log (58 dylib mapping(s) + the main executable)
  rip  0x0000000826ac1759  unmapped (no image covers this address)
```

It is unmapped **as far as the tool knows** — and the tool's knowledge is the
gap. `rip` minus mldr's logged `entry` is the same number in both runs:

```
run 2: 0x825e96759 - 0x825e91000 = 0x5759
run 3: 0x826ac1759 - 0x826abc000 = 0x5759
```

And `entry` is not the guest's entry point. mldr boots dyld:
`entry = dyld_base + 0x1000`, and dyld's `LC_UNIXTHREAD` initial RIP is `0x1000`,
which `llvm-nm` puts at `__dyld_start`. Both runs are consistent with that
(implied dyld base `0x826abb000`, a normal mmap address a few MB above the
probe).

So `rip` is dyld's own text, at `dyld_base + 0x1000 + 0x5759` =
**`dyld_base + 0x6759`**. `decode-crash.py` cannot say so because this log has
no `DEBUG dyld mh=` line, and with no dyld load address the tool has no way to
place dyld — it says "unmapped" rather than guessing, which is the right call
and also why the answer was one arithmetic step away instead of immediate.

`llvm-nm` on the overlay's dyld puts `0x6759` inside:

```
__ZN4dyldL10loadPhase6EiRK4statPKcRKNS_11LoadContextE   at dyld_base+0x6740
```

`rip` is that function **+0x19**.

## 3. The faulting instruction, and what it says

`dyld::loadPhase6` is `src/external/dyld/src/dyld2.cpp:3359`, commented
"map in file and instantiate an ImageLoader". Its first instructions:

```
    6740:  55                    pushq  %rbp
    6741:  48 89 e5              movq   %rsp, %rbp
    6744:  48 81 ec 20 82 00 00  subq   $0x8220, %rsp        <- 33,312-byte frame
    674b:  48 8d 05 ae 62 14 00  leaq   ___stack_chk_guard(%rip), %rax
    6752:  48 8b 00              movq   (%rax), %rax
    6755:  48 89 45 f8           movq   %rax, -0x8(%rbp)
    6759:  89 bd ec 7f ff ff     movl   %edi, -0x8014(%rbp)  <- RIP: first argument spill
```

Every number in the log checks out against that:

```
rbp - 0x8014   = 0x7fffffdedf1c   = the log's fault addr, exactly
rbp - rsp      = 0x8220           = the subq above, exactly
```

The frame is that big because of the local in the source
(`dyld2.cpp:3369`):

```c
uint8_t firstPages[MAX_MACH_O_HEADER_AND_LOAD_COMMANDS_SIZE];
```

with `#define MAX_MACH_O_HEADER_AND_LOAD_COMMANDS_SIZE (32*1024)`
(`src/external/dyld/src/ImageLoader.h:102`). A 32 KiB buffer on the stack.

## 4. Why the stack did not have room

`src/startup/mldr/mldr.c:988-994` sizes the guest stack:

```c
struct rlimit limit;
getrlimit(RLIMIT_STACK, &limit);
// allocate a few pages 16 pages if it's less than the limit; otherwise, allocate the limit
unsigned long size = PAGE_SIZE * 16;
if (limit.rlim_cur != RLIM_INFINITY && limit.rlim_cur < size) {
    size = limit.rlim_cur;
}
```

The comment and the condition disagree. It shrinks `size` only when the limit
is *smaller* than 64 KiB; in the "otherwise" branch the comment promises the
limit and the code keeps the 64 KiB. So the guest stack is 64 KiB whatever
`RLIMIT_STACK` says, and the stack bottom is at
`stack_top - 0x10000` = `0x7fffffdefdd0`.

The faulting address is **7,860 bytes below that bottom**. The guarded dump
says the same thing from the other side: all 640 bytes at `rsp` are outside
the mapping, which is why every slot printed `(unreadable)`.

## 5. H vs H2

- **H — "the fault is in dyld's first real load of the backend, before the image
  is mapped" — is confirmed, and now localised.** The faulting instruction is
  `dyld::loadPhase6+0x19`; the backend never appears in a `dyld: Mapping` or
  `dyld: loaded:` line (checked: zero hits), so the fault precedes the mapping,
  exactly as H predicts. It is also why the `RTLD_NOLOAD` probe call was fine:
  that call never reaches `loadPhase6`, so the 32 KiB frame is never
  allocated.
- **H2 — "the fault is in the host-side mldr rather than in dyld" — is
  refuted as the faulting code.** `rip` is provably inside dyld's own image.
  But H2 is right about the *cause*: the frame does not fit because mldr gave
  the guest a 64 KiB stack. Both halves of the answer are needed and they are
  not the same claim.

The one-line host-side change that follows from this is at
`mldr.c:991` — take the limit when the limit is larger, or use a floor of a few
MB. **Not applied here**: it is a behaviour change to the guest, it deserves
its own branch and its own run, and this task was a diagnosis.

## 6. What the fixed diagnostics did that the old ones could not

This is the part worth keeping even after the bug is fixed.

- **The dump printed completely.** All 80 slots, `[gstack+0]` through
  `[gstack+632]`. Run 2's log ended on the header line with zero words under
  it, because the walk faulted inside the diagnostic; the same walk, guarded,
  produced a complete dump and the process died of its own signal afterwards.
  All 80 slots are `(unreadable)`, which is the finding: a 640-byte window
  entirely outside the stack mapping is not something the old handler could
  have said, because it never got to say anything.
- **`rip` got an honest verdict.** The old tool answered
  `wayland-window-create-macho+0x86a759` — an offset 163x past the end of a
  19,048-byte file. This one says `unmapped (no image covers this address)`
  and refuses to attribute. The address turned out to be in dyld, which the log
  simply does not name; the wrong answer would have sent the next hour in the
  wrong direction, the right one sent this one to two arithmetic steps from the
  answer.
- **The image count is right for the first time:** 59 = 58 dylib mappings +
  the main executable, which dyld announces with a different prefix
  (`dyld: Main executable mapped`) that the segment parser used to drop.
  `attribution-from-log.py --recompute` re-derives its baked trieWalk table
  from the dyld on this machine and reports 0 disagreements, so its table is
  not the stale thing.

## 7. Tooling fix found by the run

`attribution-from-log.py` read `rip=` and, failing to place it in `trieWalk`,
reported "fault address not found in this log" — while `addr=0x7fffffdedf1c`
sat in the same line — and then emitted a VERDICT that led with the last
image the loader had announced, `libgif.dylib`. On this crash that is an
accusation with nothing behind it. The tool now reads the handler's `si_addr`
and `rip` separately, reports both, and when neither lands in `trieWalk` says
so instead of naming a suspect:

```
VERDICT: not a trieWalk fault: the loader printed no bounds complaint, and
neither rip (0x826ac1759) nor the faulting address (0x7fffffdedf1c) falls in
trieWalk's range 0x376a0-0x37aa0. The last image the loader announced is
/usr/lib/native/libgif.dylib, which is NOT evidence that it owns the fault.
```

`--self-test` still passes (3 cases); `--recompute` still passes (0
disagreements).

## 8. Reproduce

```sh
sh build-freebsd/crash-dump-selftest.sh      # RC=0
sh build-freebsd/build-mldr-only.sh          # builds + installs
DRY_RUN=1 sh build-freebsd/run-wayland-window-probe.sh   # 7 checks + seat, RC=0
sh build-freebsd/run-wayland-window-probe.sh            # the one root run
python3 build-freebsd/decode-crash.py \
    "$DARLING_BUILD_DIR/wayland-window-probe.log" "$DARLING_OVERLAY" tests
python3 build-freebsd/attribution-from-log.py \
    "$DARLING_BUILD_DIR/wayland-window-probe.log" --recompute
llvm-nm -n "$DARLING_OVERLAY/usr/lib/dyld" | grep -A1 loadPhase6
```

The probe's preflight "launch-dynamic (newer than its source)" gate was
checked by touching `tests/launch-dynamic-smoke.c`: it is self-healing, not a
hard fail — it rebuilds and reports `[PASS] launch-dynamic rebuilt from ...`.

## Not checked, not done

- **Not fixed.** The stack sizing at `mldr.c:991` is diagnosed and left alone,
  on purpose: it changes guest behaviour and wants its own run.
- **Not verified** that raising the stack makes the window appear. The claim
  is that the overflow is what stops this run; the claim that removing it
  reveals a working window is untested.
- **Not verified** that `dyld_base + 0x1000` is `__dyld_start` in the running
  process, only that it is the value consistent with all three logs and with
  dyld's `LC_UNIXTHREAD`. The log names no dyld mapping, so this is inference
  from three independent consistent values, not a measurement. One
  `DEBUG dyld mh=` line in mldr would turn it into a measurement and would let
  `decode-crash.py` place `rip` itself.
- **Not reproduced** outside this machine, and no run beyond the one was spent.
- **Not touched:** dyld sources, the overlay, the staging path, the probe
  binary, the crash handler.
- The staging cache `/tmp/darling-local-overlay` was left short by the earlier
  crash-probe run (documented preflight behaviour, `note: staging cache is
  short`); this run's preflight reported it and derived the correct tree list,
  and the run itself restaged from it.
