# Control #142 step 15 — init chain crawl: the guest dies inside the first link 0x212a8c0

**Date:** 2026-10-09
**Branch:** task/init-chain-crawl
**Base:** pr-arm64 = f16c11b961 (after step 14 merge)

## What was armed

`MLDR_TRAP_RESUME_AT='Google Chrome for Testing Framework+0x212a8c0,Google Chrome for Testing Framework+0x212aa00,Google Chrome for Testing Framework+0xccdb60,Google Chrome for Testing Framework+0x64d020'`
— resumable traps on the four links of the step-10 chain, no fatal trap. Each
hit prints the entry stack, so the hits show how far the guest got. Same
recipe: cft-fwmacho-probe-macho, staging from /tmp/stub76-staging.txt,
DARLING_SMOKE_REFRESH=1. Log: diag/init-chain-crawl.log (1 292 559 lines,
102 453 073 B).

## Result — one hit, then the guest dies inside the first link

All four traps armed (4 `MLDR_TRAP_AT: armed` lines). Exactly one hit, on the
FIRST link 0x212a8c0 (log lines 1292543–1292559):

```
dyld: calling initializer function 0x23116f3214c0 in /Frameworks/Google Chrome for Testing Framework.framework/Versions/154.0.8029.0/Google Chrome for Testing Framework

[darling-mldr] === MLDR_TRAP_AT hit #1: Google Chrome for Testing Framework+0x212a8c0 ===
  rdi=0x0 rsi=0x3563000 rdx=0x23116bf37270 rcx=0x0 rax=0x3563000
  guest tid = 0x18f6e
  guest TSD tid = 0x307 (%gs:0x18)
  called from Google Chrome for Testing Framework+0x212a567
  code @rip: 55 48 89 e5 53 48 81 ec e8 00 00 00 48 8b 05 4d
  stack slots resolving into known images:
    [rsp+  0] Google Chrome for Testing Framework+0x212a567
    [rsp+112] Google Chrome for Testing Framework+0x212a4c0
    [rsp+192] Google Chrome for Testing Framework+0x212a4c0
    [rsp+208] Google Chrome for Testing Framework+0x0
    [rsp+232] Google Chrome for Testing Framework+0x0
    [rsp+248] Google Chrome for Testing Framework+0x212a4c0
    [rsp+344] Google Chrome for Testing Framework+0x212a4c0
    [rsp+352] Google Chrome for Testing Framework+0x0
```

The caller is +0x212a567 — inside the initializer 0x212a4c0 (offset 0xa7 from
its entry): the first link is called by the initializer, as step 10 mapped.
The log ends right after the hit stack: the resumable trap emulated the
prologue and resumed, and the guest died inside 0x212a8c0 — no hit on
0x212aa00, 0xccdb60 or 0x64d020 (0 `MLDR_TRAP_AT hit` lines for those three).
mldr became a zombie; the log froze at 102 453 073 B.

## Verdict

The farthest link reached is **0x212a8c0**, the first link after the
initializer. The guest entered it (hit #1, called from +0x212a567) and died
inside it, before reaching 0x212aa00. So the death is now located between
0x212a8c0 and 0x212aa00 — one link closer than step 13's "somewhere inside
the initializer 0x212a4c0". The silent death is real, not a trap artifact:
step 13's trap-free control died the same way.

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
timeout 1700 sudo env "MLDR_TRAP_RESUME_AT=Google Chrome for Testing Framework+0x212a8c0,Google Chrome for Testing Framework+0x212aa00,Google Chrome for Testing Framework+0xccdb60,Google Chrome for Testing Framework+0x64d020" ${RUN} > init-chain-crawl.log 2>&1
grep -c "MLDR_TRAP_AT hit" init-chain-crawl.log   # 1
grep -n "MLDR_TRAP_AT hit" init-chain-crawl.log   # 1292545: hit #1 on +0x212a8c0
tail -3 init-chain-crawl.log   # stack of hit #1, then silence
```

Logs: `diag/init-chain-crawl.log` (102 453 073 B).
