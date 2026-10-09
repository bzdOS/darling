# Control #146 step 24 — clean run (no traps): still a natural SIGTRAP at the Chrome initializer

**Date:** 2026-10-09
**Branch:** task/sigexc-clean-run
**Base:** pr-arm64 = 5360dc311 (step 23 merged)
**mldr:** unchanged (step-22 trampoline) — no rebuild.

## What was run

The step-18 death recipe (docs/BZERO-DEATH-ARGS.md) with **no
`MLDR_TRAP_RESUME_AT` at all** — the through-test of the step-22 trampoline:
does signal delivery reach a natural run with no traps on the path. Wrapper is
`timeout -k 30 1700` (per the step-23 finding that the darlingserver is
reparented to the `timeout` and keeps it alive). Log `diag/sigexc-clean-1.log`.

## Result — a natural SIGTRAP, no traps anywhere

The log freezes at 1 292 538 lines / 102 451 307 B, the last line being the
Chrome initializer entry — the same program point where the instrumented traps
used to be hit:

```
dyld: lazy bind: CoreGraphics:0x26F4826DB848 = libobjc.A.dylib:_objc_alloc, *0x26F4826DB848 = 0x26F4719C2500
dyld: calling initializer function 0x26f473d214c0 in /Frameworks/Google Chrome for Testing Framework.framework/Versions/154.0.8029.0/Google Chrome for Testing Framework
RUN_DONE rc=133
```

`dmesg`:

```
pid 39400 (mldr), jid 0, uid 0: exited on signal 5 (no core dump - coredumpsize limit is 0)
```

Signal 5 = SIGTRAP — **with no `MLDR_TRAP_RESUME_AT` armed at all**.

## What this says

- The SIGTRAP is **not** an instrumentation artifact: it recurs in a clean run
  with no traps. So neither the `0x6c70` trap (step 22's hypothesis) nor the two
  Chrome traps (step 23) caused it. The guest's own Chrome-initializer entry
  raises a SIGTRAP that mldr's `sigtrap_handler` cannot resolve — that is the
  natural frontier now.
- The clean run does **not** live past the previous death point: it stops at the
  Chrome initializer entry. (Its line count 1 292 538 is ~10 lines below the
  trapped runs' 1 292 548 because those extra lines are the trap-hit records —
  the program point is the same.) So the trampoline did not get the guest
  further here; the next target is the natural SIGTRAP, not the traps.
- Acceptance: neither branch is met literally — the log does not grow past
  ~1 292 548 lines, and the death signal **is** 5 (the step expected a natural
  death to be ≠5). The measurement stands: **the natural death is SIGTRAP, at
  the Chrome initializer entry, with no traps armed.**

## Repro

```sh
# mldr unchanged from step 22 (trampoline already built in)
export PATH=/usr/local/bin:/usr/local/sbin:/usr/bin:/bin:/sbin:/usr/sbin
export DARLING_SRC_DIR=$DARLING_SRC_DIR DARLING_OVERLAY=$DARLING_OVERLAY DARLING_BUILD_DIR=$DARLING_BUILD_DIR
export DARLING_TEST_BINARY=cft-fwmacho-probe-macho
export DARLING_STAGING_TREES=$(cat /tmp/stub76-staging.txt); export DARLING_SMOKE_REFRESH=1
RUN="env ... ${DARLING_BUILD_DIR}/launch-dynamic"   # full form: docs/BZERO-DEATH-ARGS.md; NO MLDR_TRAP_RESUME_AT
cd diag && timeout -k 30 1700 sudo ${RUN} > sigexc-clean-1.log 2>&1
sudo dmesg | tail     # pid N (mldr) ... exited on signal 5
```

Log `diag/sigexc-clean-1.log` (102 451 307 B, frozen; `RUN_DONE rc=133`).
