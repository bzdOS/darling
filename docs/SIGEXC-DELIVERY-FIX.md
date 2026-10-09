# Control #144 step 22 — signal-ABI delivery trampoline: the sigexc.c:446 crash is gone, death moved

**Date:** 2026-10-09
**Branch:** task/sigexc-ucontext-conv
**Base:** pr-arm64 = 6b7c76479 (step 21 merged)
**Input:** docs/SIGEXC-STATE-DEREF.md §c.

## What changed (file:line)

Apply the step-21 fix hypothesis at the delivery boundary.

- `src/startup/mldr/sigaction_translate.c`
  - new `sat_guest_handler_trampoline()` (~line 310) plus its inverse signal
    map `sat_freebsd_signo_to_linux()` (~line 298). In `mldr_sys_rt_sigaction()`
    (~line 418) a real (non-DFL/IGN/ERR) handler is no longer installed raw: the
    trampoline is installed instead. At delivery it converts the native
    `siginfo_t` (`mldr_siginfo_freebsd_to_linux`) and `ucontext_t`
    (`mldr_ucontext_freebsd_to_linux` — which sets `fpregs = NULL`, arming the
    existing NULL guard at guest `sigexc.c:440`), calls the guest handler with
    the Linux-ABI pointers, then writes register edits back
    (`mldr_ucontext_linux_to_freebsd`). Stack-only, no malloc.
  - the per-signal registry (`sat_registry[]`) is now written **before** the
    `sigaction(2)` so the trampoline can never observe a stale entry.
  - reserved SIGILL/SIGBUS/SIGSEGV/SIGSYS are untouched
    (`sat_is_reserved_freebsd_signo`, unchanged).
- `src/startup/mldr/linux_siginfo.h` — this header had **never been compiled**;
  two latent defects surfaced the moment it was: a `SEGV_*/ILL_*/...` inside a
  block comment closed it early, and FreeBSD's `<sys/signal.h>` defines
  `si_overrun`/`si_band` as macros that rewrote two member declarations. Both
  fixed (comment reworded; the two macros `#undef`'d).
- `build-freebsd/build-mldr-only.sh` — compile `linux_siginfo.c` and
  `linux_ucontext.c` into mldr. They were not in the target's source list, so
  the whole delivery-side converter was dead, uncompiled code.

## Rebuild

```sh
export PATH=/usr/local/bin:/usr/local/sbin:/usr/bin:/bin:/sbin:/usr/sbin
export DARLING_SRC_DIR=$DARLING_SRC_DIR DARLING_BUILD_DIR=$DARLING_BUILD_DIR
sh build-freebsd/build-mldr-only.sh      # installs $DARLING_BUILD_DIR/dserver/mldr-real/mldr
```

Result: builds clean, installs `mldr` (725K). `llvm-nm` confirms
`sat_guest_handler_trampoline`, `mldr_ucontext_freebsd_to_linux`,
`mldr_ucontext_linux_to_freebsd`, `mldr_siginfo_freebsd_to_linux` are linked.

## Run — same death recipe as step 18, new log

Same command as docs/BZERO-DEATH-ARGS.md, with
`MLDR_TRAP_RESUME_AT=Google Chrome for Testing Framework+0x212a8c0,Google Chrome for Testing Framework+0x212aa00,libsystem_platform.dylib+0x6c70`,
writing to `diag/sigexc-fix-1.log` (new name; `trap-resume-fix-3.log` untouched).

## Result — the sigexc.c:446 crash is GONE; the death moved

Grepping the new log for the old signature (`state_to_kernel` / `r15=0x10002` /
`libsystem_kernel.dylib+0x2bf12`) finds only unrelated dyld `fixup:` lines: the
`movzwl (%r15),%esi` fault at `_state_to_kernel+0x1a2` does **not** recur, and
the guest signal handler now runs.

The run walks 4 trap hits and then dies. Last lines of `diag/sigexc-fix-1.log`
(frozen at 102 454 418 B / 1 292 573 lines):

```
[darling-mldr] hit #3 libsystem_platform.dylib+0x6c70 rdi=0x3a3dc9c5eec0 rsi=0x2032 ... from Google Chrome for Testing Framework+0x212aaf1
  [rsp+  0] Google Chrome for Testing Framework+0x212aaf1
[darling-mldr] hit #4 libsystem_platform.dylib+0x6c70 rdi=0x240054 rsi=0x20c rdx=0x23fad0 ... from libsystem_kernel.dylib+0x2c347
  [rsp+  0] libsystem_kernel.dylib+0x2c347
  [rsp+ 72] Google Chrome for Testing Framework+0xe839780
  [rsp+160] Google Chrome for Testing Framework+0x95d07c1
```

`libsystem_kernel.dylib+0x2c347` = `_sigexc_handler+0xa7` (per docs/DEATH-BT-SYMBOLIZE.md
and docs/SIGEXC-STATE-DEREF.md): the last hit is now called **from inside the
guest signal handler** — the trampoline delivered the signal and the handler is
executing. Death mode from `dmesg`:

```
pid 41155 (mldr), jid 0, uid 0: exited on signal 5 (no core dump - coredumpsize limit is 0)
```

Signal 5 = SIGTRAP (the previous deaths were SIGSEGV, signal 11). So the step-20
data fault is fixed; the frontier moved one step forward — a trap hit during the
guest handler's own `__platform_bzero` call (hit #4) is now the fatal point.

## Repro

```sh
# code: see "What changed"; rebuild: sh build-freebsd/build-mldr-only.sh
export PATH=/usr/local/bin:/usr/local/sbin:/usr/bin:/bin:/sbin:/usr/sbin
export DARLING_SRC_DIR=$DARLING_SRC_DIR DARLING_OVERLAY=$DARLING_OVERLAY DARLING_BUILD_DIR=$DARLING_BUILD_DIR
export DARLING_TEST_BINARY=cft-fwmacho-probe-macho
export DARLING_STAGING_TREES=$(cat /tmp/stub76-staging.txt); export DARLING_SMOKE_REFRESH=1
RUN="env ... ${DARLING_BUILD_DIR}/launch-dynamic"     # full form: docs/BZERO-DEATH-ARGS.md
cd diag && timeout 1700 sudo env "MLDR_TRAP_RESUME_AT=...0x212a8c0,...0x212aa00,libsystem_platform.dylib+0x6c70" ${RUN} > sigexc-fix-1.log 2>&1
grep -c 'hit #' sigexc-fix-1.log     # 6 hit lines; last from libsystem_kernel.dylib+0x2c347; then silence
sudo dmesg | tail                     # pid N (mldr) ... exited on signal 5
```

Log `diag/sigexc-fix-1.log` (102 454 418 B). Note: the run wrapper (`timeout
1700`) was still counting down when the guest had already died; the log is
frozen and its `RUN_DONE` line lands when the timeout expires.
