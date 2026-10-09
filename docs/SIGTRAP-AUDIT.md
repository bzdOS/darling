# Control #148 step 26 — SIGTRAP audit: delivery DOES reach the guest handler; the guest itself resets the disposition to SIG_DFL and dies

**Date:** 2026-10-09
**Branch:** task/sigtrap-audit
**Base:** pr-arm64 = 0ccf527af (step 25 merged)
**mldr:** rebuilt with three `SIGTRAP-AUDIT` markers (below).

## 1. What was instrumented

Three unbuffered `SIGTRAP-AUDIT` markers in mldr, all rebuilt with
`sh build-freebsd/build-mldr-only.sh`:

1. `src/startup/mldr/freebsd_syscall_trap.c`,
   `dispatch_linux_syscall()` / `LINUX_SYS_rt_sigaction` case: every guest
   `rt_sigaction(5, …)` prints
   `SIGTRAP-AUDIT: rt_sigaction(5) from <caller> old=<old> new=<new>`
   where old/new are the guest-side handlers from the new registry accessor
   `mldr_sat_sigtrap_disposition()` (`sigaction_translate.c`).
2. `src/startup/mldr/sigaction_translate.c`,
   `sat_guest_handler_trampoline()`: a native SIGTRAP that lands in mldr's
   own delivery trampoline prints
   `SIGTRAP-AUDIT-DELIVER: native SIGTRAP -> guest handler=<h> (linux_signo=5)`.
   This is the delivery point: it fires only if the native disposition at
   delivery time was mldr's trampoline wrapping a registered guest handler.
3. `src/startup/mldr/freebsd_syscall_trap.c`, `sigtrap_handler()`: prints
   `SIGTRAP-AUDIT-DELIVER: mldr sigtrap_handler reached, …` if mldr's own
   SIGTRAP handler runs (it did not, as step 25 already predicted).

The `from` field is `__builtin_return_address(0)` passed through
`mldr_describe_addr`; it resolves guest images only, so for this caller it
prints `0x223148 <unknown>`. Offline (`nm -n` on the mldr binary, base
0x200000) that is `sigill_handler+0x488` — the Linux-syscall dispatch entry,
i.e. every Linux syscall from the guest lands there; it is not the guest's
own caller (that would need `mc->mc_rip` at the trap).

## 2. Run — pristine, no instrumentation traps

Recipe docs/BZERO-DEATH-ARGS.md, **no** `MLDR_TRAP_RESUME_AT`, wrapper
`timeout -k 30 1700`, log `diag/sigexc-audit-1.log`.

## Result — the full chain, log frozen at its last audit line

`grep -c 'SIGTRAP-AUDIT:' diag/sigexc-audit-1.log` → **3**;
`grep -c 'SIGTRAP-AUDIT-DELIVER' …` → **1**;
`grep -c 'mldr sigtrap_handler reached' …` → **0**;
`grep -c NOT-OUR-SITE …` → **0**.
Log frozen at **1 292 542 lines** / 102 451 704 B; `dmesg`: `pid 12182 (mldr),
jid 0, uid 0: exited on signal 5`. Acceptance (≥1 SIGTRAP-AUDIT line naming
the last owner of the disposition) is **met**.

| # | line | marker | content | resolved owner |
|---|------|--------|---------|----------------|
| 1 | 302 | SIGTRAP-AUDIT | `rt_sigaction(5) from 0x223148 old=0x0 new=0x8279c7940` | `dyld` (mh=0x8278de000) **`_sigexc_handler`** at dyld+0xe9940 |
| 2 | 26470 | SIGTRAP-AUDIT | `rt_sigaction(5) from 0x223148 old=0x8279c7940 new=0x38c845b6f2a0` | `libsystem_kernel.dylib` **`_sigexc_handler`** at +0x2c2a0 (slide 0x38C845B43000, from the log's own rebase line) |
| 3 | 1292541 | SIGTRAP-AUDIT-DELIVER | `native SIGTRAP -> guest handler=0x38c845b6f2a0 (linux_signo=5)` | the same libsystem_kernel `_sigexc_handler` — **delivery reached the guest** |
| 4 | 1292542 (**last line of the log**) | SIGTRAP-AUDIT | `rt_sigaction(5) from 0x223148 old=0x38c845b6f2a0 new=0x0` | the guest **reset the disposition to SIG_DFL** |

Handlers were symbolized offline: `dyld+0xe9940` and
`libsystem_kernel.dylib+0x2c2a0` both land on `_sigexc_handler` (`llvm-nm -n`
on `$DARLING_OVERLAY/usr/lib/dyld` and `…/usr/lib/system/libsystem_kernel.dylib`;
the kernel-dylib slide is the log's own
`dyld: rebase: libsystem_kernel.dylib:*0x38C845BA2010 += 0x38C845B43000`).

## Verdict — both of step 25's remaining explanations are now settled

- **(iii) "delivery bypasses the guest handler" is disproven**: line 3 is the
  delivery trampoline firing with the guest handler address, during the
  Chrome initializer (the same point where the death has stood since step 17).
- **(ii) "a foreign guest-sigaction replaced the disposition before death" is
  confirmed, in its exact form**: the *last owner* of the SIGTRAP disposition
  at death is the guest's own `_sigexc_handler` (libsystem_kernel). The log's
  **last line** is that handler, entered on a delivered SIGTRAP, immediately
  calling `rt_sigaction(5)` with `new=0x0` (SIG_DFL) — the standard
  reset-to-default-and-reraise crash protocol. The re-raised SIGTRAP then
  hits SIG_DFL and the process dies with signal 5: mldr's `sigtrap_handler`
  can never see it, which is exactly the step-25 observation (fallback
  `NOT-OUR-SITE` grep=0, `dmesg` signal 5).

So the fatal SIGTRAP is not a mldr delivery defect: the guest receives it in
its own handler, declines it, restores SIG_DFL and dies by its own protocol.

## Repro

```sh
# code: three SIGTRAP-AUDIT markers (this commit)
# rebuild: sh build-freebsd/build-mldr-only.sh
export PATH=/usr/local/bin:/usr/local/sbin:/usr/bin:/bin:/sbin:/usr/sbin
export DARLING_SRC_DIR=$DARLING_SRC_DIR DARLING_OVERLAY=$DARLING_OVERLAY DARLING_BUILD_DIR=$DARLING_BUILD_DIR
export DARLING_TEST_BINARY=cft-fwmacho-probe-macho
export DARLING_STAGING_TREES=$(cat /tmp/stub76-staging.txt); export DARLING_SMOKE_REFRESH=1
RUN="env ... ${DARLING_BUILD_DIR}/launch-dynamic"   # full form: docs/BZERO-DEATH-ARGS.md; NO MLDR_TRAP_RESUME_AT
cd diag && timeout -k 30 1700 sudo ${RUN} > sigexc-audit-1.log 2>&1
grep -c 'SIGTRAP-AUDIT:' sigexc-audit-1.log          # 3
grep -c 'SIGTRAP-AUDIT-DELIVER' sigexc-audit-1.log   # 1
tail -2 sigexc-audit-1.log                           # DELIVER … guest handler=0x38c845b6f2a0; rt_sigaction(5) … new=0x0
sudo dmesg | tail                                    # pid N (mldr) … exited on signal 5
```

Log `diag/sigexc-audit-1.log` (102 451 704 B frozen; no `RUN_DONE` — the run
died on signal 5 as before).
