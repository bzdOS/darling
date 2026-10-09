# Control #142 step 14 — initializer-entry trap: hit fires, caller does not resolve

**Date:** 2026-10-09
**Branch:** task/init-entry-trap
**Base:** pr-arm64 = f5e98e5883 (after step 13 merge)

## Address: absolute vs offset (first attempt failed)

Step 13's control log ended at `calling initializer function 0x395eff214c0`.
That is an absolute guest address; `MLDR_TRAP_AT` takes an image-relative
offset. First run armed `Google Chrome for Testing Framework+0x395eff214c0`
and mldr refused:

```
[darling-mldr] MLDR_TRAP_AT: offset 0x395eff214c0 is past the end of Google Chrome for Testing Framework's 0xf1a3000-byte executable mapping — ignored
```

The run then ended in the same silent death as step 13 (mldr zombie, log
frozen at 102 494 694 B, last line unlock trap #95 from +0x16e41d2).

Correct offset: the initializer is the step-10 initializer at __TEXT+0x212a4c0
(`llvm-objdump -d --start-address=0x212a4c0` shows the prologue). Verified
against the v2 log: `calling initializer function 0x3852829214c0` minus the
image base (0x3852807f7000, from `libsystem_c.dylib` rebase lines) = 0x212a4c0.

## The hit — fatal trap on initializer entry

`MLDR_TRAP_AT='Google Chrome for Testing Framework+0x212a4c0'` (fatal
one-shot) + `MLDR_TRAP_RESUME_AT='libsystem_platform.dylib+0x23b1,libsystem_platform.dylib+0x28c1'`
(lock/unlock entries). Same recipe: cft-fwmacho-probe-macho, staging from
/tmp/stub76-staging.txt, DARLING_SMOKE_REFRESH=1. Log: diag/init-entry-trap-v2.log.

The trap fired once (fatal, by design) — log lines 1293231–1293246:

```
dyld: calling initializer function 0x3852829214c0 in /Frameworks/Google Chrome for Testing Framework.framework/Versions/154.0.8029.0/Google Chrome for Testing Framework

[darling-mldr] === MLDR_TRAP_AT hit #1: Google Chrome for Testing Framework+0x212a4c0 ===
  rdi=0x1 rsi=0x7fffffdffdd8 rdx=0x7fffffdffde8 rcx=0x7fffffdfff38 rax=0x3852829214c0
  guest tid = 0x43c19
  guest TSD tid = 0x307 (%gs:0x18)
  called from 0x828bd1f43 <unknown>
  code @rip: 55 48 89 e5 41 57 41 56 41 55 41 54 53 48 83 ec
  stack slots resolving into known images:
    [rsp+ 32] Google Chrome for Testing Framework+0x212a4c0
    [rsp+112] Google Chrome for Testing Framework+0x212a4c0
    [rsp+128] Google Chrome for Testing Framework+0x0
    [rsp+152] Google Chrome for Testing Framework+0x0
    [rsp+168] Google Chrome for Testing Framework+0x212a4c0
    [rsp+264] Google Chrome for Testing Framework+0x212a4c0
    [rsp+272] Google Chrome for Testing Framework+0x0
    [rsp+304] Google Chrome for Testing Framework+0xdbc15fc
```

The guest died on initializer entry (fatal trap, one hit). The caller does not
resolve: `called from 0x828bd1f43 <unknown>` — 0x828bd1f43 is outside every
registered guest image (Chrome Framework base 0x3852807f7000, libdyld base
0x38528036c000, libsystem_c base 0x385280000000). The stack slots hold only
pointers to the initializer itself (+0x212a4c0) and one +0xdbc15fc — no call
chain.

## Context before the hit (resumable traps, 97 hits)

Last resumable hit before the fatal one (line 1293224):

```
[darling-mldr] hit #90 libsystem_platform.dylib+0x28c1 rdi=0x3852801038e4 rax=0x0 tid=0x43c19 tsd=0x307 from libsystem_c.dylib+0x67600
  [rsp] 7fffffdfd980 385280067600 7fffffdfd9b0 3852800676a7 3852912d79bc 0
  [rsp+  8] libsystem_c.dylib+0x67600
  [rsp+ 24] libsystem_c.dylib+0x676a7
```

Then: unlock (libsystem_c) → dyld calls CoreGraphics initializer → lazy
binds → dyld calls the Chrome Framework initializer → fatal trap. The guest
never got past initializer entry.

## Verdict

The trap works mechanically (armed, one fatal hit, registers/stack printed),
but the caller chain does not resolve: the return address 0x828bd1f43 is
outside every registered guest image. The step-10 static chain
(0x212a4c0 → 0x212a8c0 → 0x212aa00 → 0xccdb60 → _getentropy) remains the
only named path into the dying initializer; the live run adds that the guest
now dies ON ENTRY to 0x212a4c0, before any of its own calls.

Exact next step (same mechanism, no new one): arm a resumable
(`MLDR_TRAP_RESUME_AT`) trap on +0x212a4c0 instead of a fatal one — the
guest proceeds into the initializer and dies inside it (as step 13 showed),
and the resumable hit prints the entry stack with the same caller; if the
caller still does not resolve, register the host image (mldr) in the address
resolver so host-side return addresses name themselves.

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
timeout 1700 sudo env "MLDR_TRAP_AT=Google Chrome for Testing Framework+0x212a4c0" "MLDR_TRAP_RESUME_AT=libsystem_platform.dylib+0x23b1,libsystem_platform.dylib+0x28c1" ${RUN} > init-entry-trap-v2.log 2>&1
grep -n -A12 "MLDR_TRAP_AT hit" init-entry-trap-v2.log   # 1293233: called from 0x828bd1f43 <unknown>
```

Logs: `diag/init-entry-trap-v2.log` (102 493 069 B, fatal hit on entry),
`diag/init-entry-trap.log` (102 494 694 B, wrong-offset attempt — silent
death, no trap fired).
