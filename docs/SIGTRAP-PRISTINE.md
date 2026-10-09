# Control #147 step 25 — pristine run: the SIGTRAP is natural and does NOT reach sigtrap_handler

**Date:** 2026-10-09
**Branch:** task/sigtrap-pristine
**Base:** pr-arm64 = 4fcde2ca7 (step 24 merged)
**mldr:** rebuilt with a new fallback log line (below).

## 1. Byte-check — no planted 0xCC anywhere

The overlay Chrome framework (`$DARLING_OVERLAY`) and the staged copy
(`/tmp/darling-local-overlay`) are **pristine** at every site named in the step
and in docs/BZERO-DEATH-ARGS.md — the first byte is the real instruction, never
0xCC (`llvm-objdump`):

| site | bytes | note |
|---|---|---|
| Chrome+0x64d020 | `55 48 89 e5` (pushq %rbp; movq %rsp,%rbp) | Control #76's stub-entry site — **not** patched |
| Chrome+0x212a8c0 | `55 48 89 e5 53` | first Chrome-init entry |
| Chrome+0x212aa00 | `55 48 89 e5 41 57` | second Chrome-init entry |
| Chrome+0xdbbefc6 | `ff 25 cc 40 5e 01` (jmpq *0x15e40cc(%rip)) | the stub — **not** patched |

So the step-24 "clean" run was already on **pristine staging** with no armed
trap: the SIGTRAP there was not a planted byte.

## 2. mldr change — honest fallback log line

`src/startup/mldr/freebsd_syscall_trap.c`, `sigtrap_handler()` fallback ("Not
our site: honest death"): before the `SIG_DFL` + `raise`, one line now prints
`rip`, `si_code`, `si_addr`, `byte[rip-1]`, `byte[rip-2]` and the resolved
site — modelled on `crash_debug_handler` in the same file. Rebuilt with
`sh build-freebsd/build-mldr-only.sh`.

## 3. Run — pristine, no traps

Recipe docs/BZERO-DEATH-ARGS.md, **no** `MLDR_TRAP_RESUME_AT`, wrapper
`timeout -k 30 1700`, log `diag/sigexc-pristine-1.log`.

## Result — the fallback line NEVER fired

- Log frozen at **1 292 538 lines** / 99 851 111 B (plus the wrapper's own
  `RUN_DONE rc=133` → 1 292 539 lines) — the **same** point as the step-24
  clean run. The run did **not** get past the old death point: acceptance (а)
  is **not** met.
- `grep -c NOT-OUR-SITE diag/sigexc-pristine-1.log` → **0**. The new fallback
  line never printed.
- `dmesg`: `pid 4241 (mldr), jid 0, uid 0: exited on signal 5` (SIGTRAP).

So neither acceptance branch is met literally: the log did not survive, and the
SIGTRAP did **not** recur *through `sigtrap_handler`* — its fallback never ran,
so there is no `rip`/`si_code` line to quote. The honest measurement is the
opposite of the step's assumption:

**The SIGTRAP is natural (no planted byte, no armed trap) AND it never reaches
mldr's `sigtrap_handler`.** The handler is installed for SIGTRAP
(`setup_macos_syscall_trap`), yet the fatal SIGTRAP arrives with the disposition
already `SIG_DFL` (or on a path that bypasses it). The next probe is therefore
not "the planted byte" but "why the SIGTRAP does not reach the installed
handler": read the SIGTRAP disposition at the death point, install the handler
earlier, or check whether the guest's own signal machinery resets it.

## Repro

```sh
# code: sigtrap_handler fallback log line (this commit)
# rebuild: sh build-freebsd/build-mldr-only.sh
export PATH=/usr/local/bin:/usr/local/sbin:/usr/bin:/bin:/sbin:/usr/sbin
export DARLING_SRC_DIR=$DARLING_SRC_DIR DARLING_OVERLAY=$DARLING_OVERLAY DARLING_BUILD_DIR=$DARLING_BUILD_DIR
export DARLING_TEST_BINARY=cft-fwmacho-probe-macho
export DARLING_STAGING_TREES=$(cat /tmp/stub76-staging.txt); export DARLING_SMOKE_REFRESH=1
RUN="env ... ${DARLING_BUILD_DIR}/launch-dynamic"   # full form: docs/BZERO-DEATH-ARGS.md; NO MLDR_TRAP_RESUME_AT
cd diag && timeout -k 30 1700 sudo ${RUN} > sigexc-pristine-1.log 2>&1
grep -c NOT-OUR-SITE sigexc-pristine-1.log   # 0
sudo dmesg | tail                             # pid N (mldr) ... exited on signal 5
```

Log `diag/sigexc-pristine-1.log` (99 851 111 B frozen; `RUN_DONE rc=133`).
