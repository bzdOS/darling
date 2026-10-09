# Control #142 step 18 — bzero death args: second call is bzero(rbx+0x1640, 0x2032)

**Date:** 2026-10-09
**Branch:** task/bzero-death-args
**Base:** pr-arm64 = 4b8bd8de5e (after step 17 merge)

## Offline disassembly of [0x212aa00..0x212abc8)

`rbx` is the object pointer: `movq %rdi, %rbx` at 0x212aa13 (rdi is the
function's first argument — `this`). The two calls of stub 0xdbbefc6
(→ `__platform_bzero`, libsystem_platform+0x6c70):

**First call (callq 0x212aa9d):**

```
212aa7b: 48 81 c7 c8 00 00 00    addq   $0xc8, %rdi        ; rdi = rbx + 0xc8
212aa98: be f9 14 00 00          movl   $0x14f9, %esi       ; rsi = 0x14f9 (5369)
212aa9d: e8 24 45 a9 0b          callq  0xdbbefc6
```

Buffer: object + 0xc8, length 5369 bytes.

**Second call (callq 0x212aaec):**

```
212aae0: 48 8d bb 40 16 00 00    leaq   0x1640(%rbx), %rdi  ; rdi = rbx + 0x1640
212aae7: be 32 20 00 00          movl   $0x2032, %esi       ; rsi = 0x2032 (8242)
212aaec: e8 d5 44 a9 0b          callq  0xdbbefc6
```

Buffer: object + 0x1640, length 8242 bytes. This is the call after which the
guest died in step 17 (hit #5, called from +0x212aaf1).

## Code change — rsi/rdx in the resumable hit print

`src/startup/mldr/freebsd_syscall_trap.c`, `mldr_report_trap`: the resumable
hit line printed `rdi`/`rax` only; added `rsi` and `rdx` between them
(`rdi=0x%llx rsi=0x%llx rdx=0x%llx rax=0x%llx`). mldr rebuilt narrowly
(`build-mldr-only.sh`).

## Run — same recipe, new log name

`MLDR_TRAP_RESUME_AT='Google Chrome for Testing Framework+0x212a8c0,Google Chrome for Testing Framework+0x212aa00,libsystem_platform.dylib+0x6c70'`,
log `diag/trap-resume-fix-2.log` (1 292 569 lines, 102 454 106 B; the old
`trap-resume-fix.log` was not touched). Five hits, all with rsi/rdx:

| # | hit | line | rdi | rsi | rdx |
|---|-----|------|-----|-----|-----|
| 1 | libsystem_platform+0x6c70 | 26471 | 0x7fffffdfda30 | 0x34 | 0x7fffffdfd840 |
| 2 | Chrome+0x212a8c0 | 1292549 | 0x0 | 0x2f34000 | 0x3aae7b137270 |
| 3 | Chrome+0x212aa00 | 1292553 | 0x3aae8c45d880 | 0x7fffffdfdad8 | 0xf0000 |
| 4 | libsystem_platform+0x6c70 | 1292556 | 0x3aae8c45d948 | **0x14f9** | 0xf0000 |
| 5 | libsystem_platform+0x6c70 | 1292563 | 0x3aae8c45eec0 | **0x2032** | 0x3aae8c45ee41 |

Hit #4 (first bzero call): rdi = rbx+0xc8 = 0x3aae8c45d948, rsi = 0x14f9 —
matches the disassembly (`addq $0xc8, %rdi; movl $0x14f9, %esi`).

Hit #5 (second bzero call): rdi = rbx+0x1640 = 0x3aae8c45eec0, rsi = 0x2032 —
matches (`leaq 0x1640(%rbx), %rdi; movl $0x2032, %esi`).

Last line of the log (hit #5's stack, line 1292569):

```
[darling-mldr] hit #3 libsystem_platform.dylib+0x6c70 rdi=0x3aae8c45eec0 rsi=0x2032 rdx=0x3aae8c45ee41 rax=0x3aae8c45d948 tid=0x18a2c tsd=0x307 from Google Chrome for Testing Framework+0x212aaf1 
  [rsp] 3aae7e521af1 10300000000 7f9346704080 3aae7b15b58e 3aae7b16645c 10300000000
  [rsp+  0] Google Chrome for Testing Framework+0x212aaf1
  [rsp+ 24] libsystem_kernel.dylib+0x1858e
  [rsp+ 32] libsystem_kernel.dylib+0x2345c
  [rsp+112] libsystem_platform.dylib+0x737f
  [rsp+176] libsystem_malloc.dylib+0x7750
```

The guest died right after hit #5 — inside `__platform_bzero`, on the second
call, zeroing 8242 bytes at object+0x1640. mldr zombie, log frozen.

## Verdict

The death inside `__platform_bzero` is on the SECOND call: buffer
`this+0x1640` (8242 bytes, a large sub-object field of the initializer's
object), not the first call (`this+0xc8`, 5369 bytes). At the death hit rdx
holds 0x3aae8c45ee41 — unrelated to the buffer (bzero takes only rdi/rsi); the
buffer end is rdi+rsi = 0x3aae8c45eec0 + 0x2032 = 0x3aae8c460ef2.

## Repro

```sh
# code: rsi/rdx added to mldr_report_trap resumable hit line
# rebuild: sh build-freebsd/build-mldr-only.sh
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
timeout 1700 sudo env "MLDR_TRAP_RESUME_AT=Google Chrome for Testing Framework+0x212a8c0,Google Chrome for Testing Framework+0x212aa00,libsystem_platform.dylib+0x6c70" ${RUN} > trap-resume-fix-2.log 2>&1
grep -n "hit #" trap-resume-fix-2.log   # last: hit #3 libsystem_platform.dylib+0x6c70 rdi=... rsi=0x2032
```

Logs: `diag/trap-resume-fix-2.log` (102 454 106 B).
