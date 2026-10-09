# Control #142 step 13 — second-acquire trap: the death path no longer passes +0x8237

**Date:** 2026-10-09
**Branch:** task/second-acquire-trap
**Base:** pr-arm64 = 4fe9e6564a (after step 12 merge)

## What was armed

`MLDR_TRAP_AT='libsystem_platform.dylib+0x8237'` — fatal one-shot trap on the
`__os_unfair_lock_recursive_abort` ud2 (the death signature of steps 10–11).
Plus resumable traps `MLDR_TRAP_RESUME_AT='libsystem_platform.dylib+0x23b1,libsystem_platform.dylib+0x28c1'`
(lock/unlock entries) to count acquires before the abort. No file patch (Control
#76 not repeated). Run: `cft-fwmacho-probe-macho` + staging from
`/tmp/stub76-staging.txt` + `DARLING_SMOKE_REFRESH=1`, same launch harness as
step 11.

## Result — the trap did not fire: the death path changed

The trap armed correctly (log line):

```
[darling-mldr] MLDR_TRAP_AT: armed libsystem_platform.dylib+0x8237 at 0x1bb7fc886237 (resume=0 emulate=0 orig=0f0b)
```

But the run ended with **0 `MLDR_TRAP_AT hit` lines** and **0 death-signature
lines** (`genuine SIGILL`, `FATAL signal` — both 0). The guest died silently:
mldr became a zombie, the log froze at 102 494 627 B. Last log lines (resumable
unlock trap #95, inside the Chrome Framework initializer):

```
[darling-mldr] hit #95 libsystem_platform.dylib+0x28c1 rdi=0x2a64fb49d4d8 rax=0x1 tid=0x18a30 tsd=0x307 from Google Chrome for Testing Framework+0x16e41d2
  [rsp] 7fffffdfd850 2a64eccdb1d2 7fffffdfd83c 1c08a00 fd 100000000
  [rsp+  8] Google Chrome for Testing Framework+0x16e41d2
  [rsp+120] Google Chrome for Testing Framework+0x1863286
```

97 resumable hits total (lock/unlock), 0 fatal hits. The guest got further
into the initializer than the control run (below), then died without any
signal.

## Control run (no traps, same recipe)

Log `second-acquire-trap-ctrl.log`: the guest also died silently — mldr
zombie, log froze at 99 851 111 B, last line:

```
dyld: calling initializer function 0x395eff214c0 in /Frameworks/Google Chrome for Testing Framework.framework/Versions/154.0.8029.0/Google Chrome for Testing Framework
```

0 `genuine SIGILL`, 0 `FATAL signal`. So the silent death is **not** caused by
the traps: the old signature (SIGILL ud2 at libsystem_platform+0x8237, Control
#77 control of 2026-10-06) no longer reproduces in the current tree (after the
step 10–12 merges).

## Verdict

Honest "did not work": the fatal trap on +0x8237 never fired because the guest
no longer dies through `__os_unfair_lock_recursive_abort` — it dies silently
inside the Chrome Framework initializer (control: on entry to initializer
0x395eff214c0; trapped run: deeper, after unlock #95 from +0x16e41d2). The
second acquire is not caught because the abort it was supposed to cause does
not happen on this path anymore.

Exact alternative (same mechanism, no new one): arm `MLDR_TRAP_AT` on the
initializer entry `Google Chrome for Testing Framework+0x395eff214c0` (the
control run's last line) — the trap will fire on entry and print the caller
chain of the initializer, which is where the silent death now begins.

## Repro

```sh
export PATH=/usr/local/bin:/usr/local/sbin:/usr/bin:/bin:/sbin:/usr/sbin
export DARLING_SRC_DIR=/opt/darling/src
export DARLING_OVERLAY=/opt/darling/overlay
export DARLING_BUILD_DIR=/opt/darling/build
export DARLING_TEST_BINARY=cft-fwmacho-probe-macho
export DARLING_STAGING_TREES=$(cat /tmp/stub76-staging.txt)
export DARLING_SMOKE_REFRESH=1
RUN="env DARLING_SRC_DIR=${DARLING_SRC_DIR} DARLING_OVERLAY=${DARLING_OVERLAY} DARLING_BUILD_DIR=${DARLING_BUILD_DIR}"
RUN="${RUN} DARLING_TEST_BINARY=${DARLING_TEST_BINARY}"
RUN="${RUN} DARLING_STAGING_TREES=${DARLING_STAGING_TREES}"
RUN="${RUN} DARLING_SMOKE_REFRESH=1"
for v in DYLD_BIND_AT_LAUNCH DYLD_PRINT_LIBRARIES DYLD_PRINT_LIBRARIES_POST_LAUNCH DYLD_PRINT_BINDINGS DYLD_PRINT_WEAK_BINDINGS DYLD_PRINT_APIS DYLD_PRINT_INTERPOSING DYLD_PRINT_SEGMENTS DYLD_PRINT_STATISTICS DYLD_PRINT_STATISTICS_DETAILS DYLD_PRINT_RPATHS DYLD_PRINT_WARNINGS DYLD_PRINT_INITIALIZERS DYLD_PRINT_DOFS DYLD_PRINT_OPTS DYLD_PRINT_ENV DYLD_PRINT_CODE_SIGNATURES DYLD_PRINT_REBASINGS DYLD_PRINT_TO_STDERR; do RUN="${RUN} ${v}=1"; done
RUN="${RUN} ${DARLING_BUILD_DIR}/launch-dynamic"
timeout 1700 sudo env "MLDR_TRAP_AT=libsystem_platform.dylib+0x8237" "MLDR_TRAP_RESUME_AT=libsystem_platform.dylib+0x23b1,libsystem_platform.dylib+0x28c1" ${RUN} > second-acquire-trap.log 2>&1
grep -c "MLDR_TRAP_AT hit" second-acquire-trap.log   # 0
grep -c "genuine SIGILL\|FATAL signal" second-acquire-trap.log   # 0
tail -8 second-acquire-trap.log   # hit #95, then silence; mldr zombie
```

Logs: `diag/second-acquire-trap.log` (102 494 627 B, trapped run),
`diag/second-acquire-trap-ctrl.log` (99 851 111 B, control),
`diag/second-acquire-trap-attempt1.log` (102 494 625 B, first trapped run —
same silent death).
