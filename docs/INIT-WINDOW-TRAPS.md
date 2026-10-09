# Control #142 step 16 — init window traps: guest reaches 0xdbbefc6, the first call inside 0x212aa00

**Date:** 2026-10-09
**Branch:** task/init-window-traps
**Base:** pr-arm64 = d22363946f (after step 15 merge)

## Offline disassembly of window [0x212a8c0..0x212a9d2)

65 instructions. Exactly one callq outside the window:

```
212a9cd: e8 2e 00 00 00    callq  0x212aa00
```

Three local jne (0x212a8e4, 0x212a8fd, 0x212a909) and the rest are mov/test/
xor/leaq — no imports via __stubs in this window. The prologue is visible in
step 15's hit stack: `55 48 89 e5 53 48 81 ec …`.

## Run v1 — trap on the only call target 0x212aa00

`MLDR_TRAP_RESUME_AT='Google Chrome for Testing Framework+0x212aa00'`, no
fatal trap, same recipe. Log: diag/init-window-traps.log (1 292 545 lines,
102 452 071 B). One hit (line 1292541):

```
[darling-mldr] === MLDR_TRAP_AT hit #1: Google Chrome for Testing Framework+0x212aa00 ===
  rdi=0x1b743745d880 rsi=0x7fffffdfdad8 rdx=0xf0000 rcx=0x7fffffdfdaeb rax=0x0
  guest tid = 0x34387
  guest TSD tid = 0x307 (%gs:0x18)
  called from Google Chrome for Testing Framework+0x212a9d2
  code @rip: 55 48 89 e5 41 57 41 56 53 48 81 ec e8 00 00 00
  stack slots resolving into known images:
    [rsp+  0] Google Chrome for Testing Framework+0x212a9d2
    [rsp+256] Google Chrome for Testing Framework+0x212a567
    [rsp+368] Google Chrome for Testing Framework+0x212a4c0
```

Caller is +0x212a9d2 (the callq at 0x212a9cd + 5). The stack shows the chain
0x212a4c0 → 0x212a567 → 0x212a9d2 → 0x212aa00 — the step-10 chain,
confirmed live. The guest died on entry to 0x212aa00 (mldr zombie, log
frozen).

## Run v2 — traps on every call target inside 0x212aa00

`MLDR_TRAP_RESUME_AT` on the five unique call targets inside [0x212aa00..0x212abc8):
0xdbbefc6 (stub, called at 0x212aa9d and 0x212aaec), 0xccdb60 (stub, at
0x212ab3a and 0x212ab4b), 0xdbbefba (stub, 0x212ab81), 0x1862820 (in-image,
0x212ab8c), 0xdbbef66 (stub, 0x212abaf). Log: diag/init-window-traps-v2.log
(1 292 560 lines, 102 453 348 B). One hit (line 1292546):

```
[darling-mldr] === MLDR_TRAP_AT hit #1: Google Chrome for Testing Framework+0xdbbefc6 ===
  rdi=0x2acb7d45d948 rsi=0x14f9 rdx=0xf0000 rcx=0x7fffffdfdaeb rax=0xffffffffffffffff
  guest tid = 0x18f76
  guest TSD tid = 0x307 (%gs:0x18)
  called from Google Chrome for Testing Framework+0x212aaa2
  code @rip: ff 25 cc 40 5e 01 ff 25 ce 40 5e 01 ff 25 d0 40
  stub jmp *0x2acb7c59a098 -> 0x2acb6d084c70 libsystem_platform.dylib+0x6c70
  stack slots resolving into known images:
    [rsp+  0] Google Chrome for Testing Framework+0x212aaa2
    [rsp+ 24] libsystem_kernel.dylib+0x1858e
    [rsp+ 32] libsystem_kernel.dylib+0x2345c
```

Caller is +0x212aaa2 (callq at 0x212aa9d + 5) — the FIRST call inside
0x212aa00. The stub jumps to libsystem_platform.dylib+0x6c70.

## Why every trap here is fatal (resume=0) — a correction to step 15

The mldr emulation table (freebsd_syscall_trap.c) only resumes on four
original-byte patterns: `xchgl %eax,(%rcx)`, `movl %eax,%ebx`, `callq rel32`,
`movq %rsp,%rbp` (48 89 e5). Every address used in steps 15–16 starts with
`pushq %rbp` (55 48 89 e5) or is a stub (`ff 25`), so mldr set resume=0
(fatal) for all of them. Consequences:

* Step 15's "guest dies inside 0x212a8c0" was an ARTIFACT: the trap on
  0x212a8c0 was fatal (resume=0), the guest died ON ENTRY. Without that trap
  (run v1 here) the guest passes 0x212a8c0 and reaches 0x212aa00.
* Run v1's hit on 0x212aa00 is also fatal (same reason) — the guest dies on
  entry to 0x212aa00, not deeper.
* Run v2's hit on 0xdbbefc6 is fatal too — the guest dies at the first call
  inside 0x212aa00, not deeper.

So the live runs bound the death only as "guest reaches 0xdbbefc6 (first
call inside 0x212aa00)"; the natural death point is still after it. The
resumable machinery cannot be planted on pushq-%rbp prologues — that needs an
emulation table extension (new mechanism, not this step).

## Verdict

Farthest point reached by the guest (by hit lines): the first call inside
0x212aa00 — callq 0xdbbefc6 at 0x212aa9d (hit #1 in run v2, called from
+0x212aaa2, stub → libsystem_platform.dylib+0x6c70). The step-15 result
"dies inside 0x212a8c0" is corrected: that was a fatal-trap artifact; the
guest actually passes 0x212a8c0.

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
timeout 1700 sudo env "MLDR_TRAP_RESUME_AT=Google Chrome for Testing Framework+0xdbbefc6,Google Chrome for Testing Framework+0xccdb60,Google Chrome for Testing Framework+0xdbbefba,Google Chrome for Testing Framework+0x1862820,Google Chrome for Testing Framework+0xdbbef66" ${RUN} > init-window-traps-v2.log 2>&1
grep -c "MLDR_TRAP_AT hit" init-window-traps-v2.log   # 1
grep -n "MLDR_TRAP_AT hit" init-window-traps-v2.log   # 1292546: hit #1 on +0xdbbefc6
```

Logs: `diag/init-window-traps.log` (102 452 071 B, v1 — hit on 0x212aa00),
`diag/init-window-traps-v2.log` (102 453 348 B, v2 — hit on 0xdbbefc6).
