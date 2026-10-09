# Control #144 step 23 — continue-run: without the 0x6c70 trap the run still dies on SIGTRAP

**Date:** 2026-10-09
**Branch:** task/sigexc-continue-run
**Base:** pr-arm64 = 4f324a42e (step 22 merged)
**mldr:** unchanged (already carries the step-22 trampoline) — no rebuild.

## What was tried

Same death recipe as docs/BZERO-DEATH-ARGS.md, but the trap set is the two
Chrome entries only, dropping the `libsystem_platform.dylib+0x6c70` trap that
step 22 blamed:

```
MLDR_TRAP_RESUME_AT=Google Chrome for Testing Framework+0x212a8c0,Google Chrome for Testing Framework+0x212aa00
```

Log `diag/sigexc-continue-run-1.log` (new name; `sigexc-fix-1.log` untouched).

## Result — the premise is falsified

Both Chrome traps arm (`resume=1 emulate=6`) and are hit once each; the log
then freezes and mldr dies on signal 5 (SIGTRAP) — **with no `0x6c70` trap
anywhere on the path**:

```
[darling-mldr] MLDR_TRAP_AT: armed Google Chrome for Testing Framework+0x212a8c0 at 0x2300d0f218c0 (resume=1 emulate=6 orig=5548)
[darling-mldr] MLDR_TRAP_AT: armed Google Chrome for Testing Framework+0x212aa00 at 0x2300d0f21a00 (resume=1 emulate=6 orig=5548)
...
dyld: calling initializer function 0x2300d0f214c0 in /Frameworks/Google Chrome for Testing Framework.framework/Versions/154.0.8029.0/Google Chrome for Testing Framework
[darling-mldr] hit #1 Google Chrome for Testing Framework+0x212a8c0 rdi=0x0 rsi=0x237a000 ... from Google Chrome for Testing Framework+0x212a567
  [rsp+  0] Google Chrome for Testing Framework+0x212a567
  [rsp+112] Google Chrome for Testing Framework+0x212a4c0
[darling-mldr] hit #1 Google Chrome for Testing Framework+0x212aa00 rdi=0x2300dee5d880 rsi=0x7fffffdfdad8 ... from Google Chrome for Testing Framework+0x212a9d2
  [rsp+  0] Google Chrome for Testing Framework+0x212a9d2
```

```
pid 69043 (mldr), jid 0, uid 0: exited on signal 5 (no core dump - coredumpsize limit is 0)
```

Log frozen at 1 292 548 lines / 102 452 486 B. No `fatal`/`not resumable`
message; the death is silent, at the `0x212aa00` trap.

## What this says

- The step-22 SIGTRAP was **not** caused by the `0x6c70` trap: the same
  SIGTRAP death recurs with only the two Chrome traps armed. So the step-22
  reading ("the 0x6c70 trap killed the run inside the handler's memset
  NULL-guard branch") does not hold.
- The run did **not** get further than the previous one: it died at the second
  Chrome trap (`0x212aa00`, hit during the Chrome initializer), i.e. *before*
  the step-22 run's own last hit (which was `0x6c70` called from
  `_sigexc_handler+0xa7`). So the sigexc-handler continuation the step wanted to
  observe was **not** reached in this run.
- Consequence for the next probe: the traps themselves are now the suspect, not
  just `0x6c70`. A run with **no** `MLDR_TRAP_RESUME_AT` at all is what would
  show the natural death "без ловушек на пути" (acceptance case б).

## Repro

```sh
# mldr unchanged from step 22 (trampoline already built in)
export PATH=/usr/local/bin:/usr/local/sbin:/usr/bin:/bin:/sbin:/usr/sbin
export DARLING_SRC_DIR=$DARLING_SRC_DIR DARLING_OVERLAY=$DARLING_OVERLAY DARLING_BUILD_DIR=$DARLING_BUILD_DIR
export DARLING_TEST_BINARY=cft-fwmacho-probe-macho
export DARLING_STAGING_TREES=$(cat /tmp/stub76-staging.txt); export DARLING_SMOKE_REFRESH=1
RUN="env ... ${DARLING_BUILD_DIR}/launch-dynamic"     # full form: docs/BZERO-DEATH-ARGS.md
cd diag && timeout 1700 sudo env "MLDR_TRAP_RESUME_AT=Google Chrome for Testing Framework+0x212a8c0,Google Chrome for Testing Framework+0x212aa00" ${RUN} > sigexc-continue-run-1.log 2>&1
grep -c 'hit #' sigexc-continue-run-1.log     # 2; then silence
sudo dmesg | tail                              # pid N (mldr) ... exited on signal 5
```

Log `diag/sigexc-continue-run-1.log` (102 452 486 B, frozen).
