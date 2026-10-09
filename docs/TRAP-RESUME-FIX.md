# Control #142 step 17 — trap resume fix: pushq %rbp emulation, one run walks the chain

**Date:** 2026-10-09
**Branch:** task/trap-resume-fix
**Base:** pr-arm64 = 354577544c (after step 16 merge)

## Code change — emulate=6 for pushq %rbp

`src/startup/mldr/freebsd_syscall_trap.c`: the emulation table resumed only
on four original-byte patterns (xchgl %eax,(%rcx), movl %eax,%ebx, callq
rel32, movq %rsp,%rbp). Added a fifth: `pushq %rbp; movq %rsp,%rbp`
(55 48 89 e5) → emulate=6. Emulation: `[rsp-8]=rbp; rsp-=8; rbp=rsp;
rip+=4` (the 2-byte ud2 clobbers 55 48, leaving 89 e5; both instructions are
emulated, execution resumes at addr+4). Stubs (ff 25) untouched — their
final targets are trapped directly.

mldr rebuilt narrowly (`build-freebsd/build-mldr-only.sh`, objects + relink
only) and installed to `build/dserver/mldr-real/mldr`.

## Run — RESUME on the three reached push-rbp entries

`MLDR_TRAP_RESUME_AT='Google Chrome for Testing Framework+0x212a8c0,Google Chrome for Testing Framework+0x212aa00,libsystem_platform.dylib+0x6c70'`
(0x6c70 = final target of stub 0xdbbefc6 = `__platform_bzero`, verified by
`llvm-lipo --thin x86_64` + objdump: starts with pushq %rbp). Same recipe,
log `diag/trap-resume-fix.log` (1 292 568 lines, 102 453 890 B).

All three armed as resume=1 emulate=6 (the new path works):

```
[darling-mldr] MLDR_TRAP_AT: armed libsystem_platform.dylib+0x6c70 at 0x2900a5a84c70 (resume=1 emulate=6 orig=5548)
[darling-mldr] MLDR_TRAP_AT: armed Google Chrome for Testing Framework+0x212a8c0 at 0x2900a7f218c0 (resume=1 emulate=6 orig=5548)
[darling-mldr] MLDR_TRAP_AT: armed Google Chrome for Testing Framework+0x212aa00 at 0x2900a7f21a00 (resume=1 emulate=6 orig=5548)
```

Five hits, all resumable (the chain walks live in one run):

| # | hit | line | called from | meaning |
|---|-----|------|-------------|---------|
| 1 | libsystem_platform.dylib+0x6c70 | 26471 | libsystem_pthread.dylib+0x534a | before the Chrome initializer |
| 2 | Chrome+0x212a8c0 | 1292548 | Chrome+0x212a567 | first link entered |
| 3 | Chrome+0x212aa00 | 1292552 | Chrome+0x212a9d2 | second link entered |
| 4 | libsystem_platform.dylib+0x6c70 | 1292555 | Chrome+0x212aaa2 | first call of stub 0xdbbefc6 (callq 0x212aa9d) |
| 5 | libsystem_platform.dylib+0x6c70 | 1292562 | Chrome+0x212aaf1 | second call of stub 0xdbbefc6 (callq 0x212aaec) |

Last hit (line 1292562, the log's last line):

```
[darling-mldr] hit #3 libsystem_platform.dylib+0x6c70 rdi=0x2900b5e5eec0 rax=0x2900b5e5d948 tid=0x19324 tsd=0x307 from Google Chrome for Testing Framework+0x212aaf1 
  [rsp] 2900a7f21af1 10300000000 7fa4a0904080 2900a4b5b58e 2900a4b6645c 10300000000
  [rsp+  0] Google Chrome for Testing Framework+0x212aaf1
  [rsp+ 24] libsystem_kernel.dylib+0x1858e
  [rsp+ 32] libsystem_kernel.dylib+0x2345c
  [rsp+112] libsystem_platform.dylib+0x737f
  [rsp+176] libsystem_malloc.dylib+0x7750
```

The guest died right after hit #5 — inside `__platform_bzero` (libsystem_platform
+0x6c70), called from Chrome+0x212aaf1 (the second 0xdbbefc6 call site). mldr
became a zombie; the log froze. The death is now located after 0xdbbefc6's
final target — one step past step 16 (where the guest died on 0xdbbefc6
itself, because that trap was fatal).

## Verdict against the acceptance criterion

≥3 hits in one log: yes (5). Log continues past the last hit: NO — the guest
died right after hit #5 (mldr zombie, log frozen at 102 453 890 B). The
criterion is partially met: the new emulation works (pushq %rbp entries are
resumable, the chain 0x212a8c0 → 0x212aa00 → 0xdbbefc6 → __platform_bzero is
walked in one run), but the natural death point is still after
__platform_bzero and the log does not continue past it.

## Repro

```sh
# code: emulate=6 in freebsd_syscall_trap.c (pushq %rbp; movq %rsp,%rbp)
# rebuild: sh build-freebsd/build-mldr-only.sh   (needs DARLING_SRC_DIR/DARLING_BUILD_DIR)
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
timeout 1700 sudo env "MLDR_TRAP_RESUME_AT=Google Chrome for Testing Framework+0x212a8c0,Google Chrome for Testing Framework+0x212aa00,libsystem_platform.dylib+0x6c70" ${RUN} > trap-resume-fix.log 2>&1
grep -c "hit #" trap-resume-fix.log   # 5
grep -n "hit #" trap-resume-fix.log   # last: hit #3 libsystem_platform.dylib+0x6c70, then silence
```

Logs: `diag/trap-resume-fix.log` (102 453 890 B).
