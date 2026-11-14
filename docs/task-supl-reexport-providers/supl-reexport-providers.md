# supl-reexport-providers: step 43 (build ok, runs blocked by environment)

Base: pr-arm64 ea69e1ea0 (step 42 merged locally). Branch: task/supl-reexport-providers.

## Changes attempted
- build-freebsd/gen-cg-supl.py: SKIP_NAMES set to the 23 non-bzero FUNC names from step 41 (____mb_cur_max_l, ___darwin_check_fd_set_overflow, ___error, ___exp10, ___exp10f, ___fpclassifyf, ___mac_syscall, ___maskrune, ___math_errhandling, ___memcpy_chk, ___memmove_chk, ___memset_chk, ___sincos_stret, ___sincosf_stret, ___stack_chk_fail, ___strlcat_chk, ___strlcpy_chk, ___tolower, ___toupper, ___ulock_wait, ___ulock_wake, ___atomic_load, ___atomic_store). ___bzero/_bzero still forwarded.
- build-freebsd/build-suppl-stubs.sh: link suppl/cg-supl with -reexport_library for overlay providers libsystem_c, libsystem_kernel, libsystem_m, libsystem_platform (install-name paths /usr/lib/system/libsystem_{c,kernel,m,platform}.dylib).

## (1) Build and static checks
Command:
  export PATH=/usr/local/bin:/usr/local/sbin:/usr/bin:/bin:/sbin:/usr/sbin
  export DARLING_SRC_DIR=/opt/darling/src DARLING_OVERLAY=/opt/darling/overlay DARLING_BUILD_DIR=/opt/darling/build
  sh src/build-freebsd/build-suppl-stubs.sh
Result: RC=0, ld64.lld version warnings only (provider dylibs 11.0.0 > target 10.12).

Generator output:
  suppl:   wrote .../suppl.s: 822 text stubs, 11 data stubs, 2 forwarding stubs, 23 skipped, 858 total
  cg-supl: wrote .../cg-supl.s: 848 text stubs, 13 data stubs, 2 forwarding stubs, 23 skipped, 886 total
Link flags observed (sh -x): -reexport_library .../libsystem_c.dylib -reexport_library .../libsystem_kernel.dylib -reexport_library .../libsystem_m.dylib -reexport_library .../libsystem_platform.dylib

Artifacts inspected: /opt/darling/build/suppl-stubs/{suppl,cg-supl}.dylib (and copied to overlay for attempted runs, then restored).

llvm-nm -m suppl.dylib | grep -E (___stack_chk_fail|___error|___ulock_wait|___ulock_wake|___bzero|_bzero):
  0000000000000801 (__TEXT,__text) external ___bzero
  000000000000088a (__TEXT,__text) external _bzero
  (none of the 23 skipped names present)

All 23 skipped names were present in suppl.names.txt input list (23/23).

llvm-objdump --macho --private-headers suppl.dylib: LC_REEXPORT_DYLIB present for all 4 providers:
  /usr/lib/system/libsystem_c.dylib
  /usr/lib/system/libsystem_kernel.dylib
  /usr/lib/system/libsystem_m.dylib
  /usr/lib/system/libsystem_platform.dylib
(load commands 10/12/14/16 as LC_REEXPORT_DYLIB; dylib flags NO_REEXPORTED_DYLIBS not set).
Same pattern for cg-supl.dylib.

## (2) Run A — not executed (environment blocked)
Intended:
  sudo -n env DARLING_SRC_DIR=... DARLING_OVERLAY=... DARLING_BUILD_DIR=... DARLING_TEST_BINARY=chrome-macho \\
    CHROME_APP="/opt/darling/chrome-cft/chrome-mac-x64/Google Chrome for Testing.app" \\
    "DARLING_STAGING_TREES=$(cat $DARLING_BUILD_DIR/chrome-staging-trees.txt)" \\
    WAYLAND_DISPLAY=wayland-1 XDG_RUNTIME_DIR=/tmp/wayland-test DARLING_SMOKE_REFRESH=1 \\
    MLDR_TRAP_AT="Foundation+0x790b0" timeout -k 5 300 "$DARLING_BUILD_DIR/launch-dynamic"

Actual check: `sudo -n true` => RC=1, stderr:
  sudo: <sudo-binary> must be owned by uid 0 and have the setuid bit set
Short probe with same env and timeout 10 wrote 0-byte log and RC=124 from timeout wrapper, with same sudo stderr; no launch metrics (Symbol not found/FATAL/dyld: loaded/MLDR_TRAP_AT hit not measurable).

## (3) Run B — not executed
Blocked by same sudo failure; Framework+0x212ab3a trap run not performed.

## Fallback / state
- Overlay suppl/cg-supl restored from build/suppl-stubs/backup-step43 to pre-step43 state.
- build-freebsd/gen-cg-supl.py and build-freebsd/build-suppl-stubs.sh reverted to committed baseline (SKIP_NAMES empty, no -reexport_library).
- No duplicate-symbol/linker-cycle/conflict error observed during build; launch-path conflict not measurable without runs.

## Verdict
Re-export link attempt built cleanly and static export shape matches the plan (23 names omitted, 4 provider LC_REEXPORT_DYLIBs present). Dynamic accept/reject classification (Run A/B, including §9.11 site behavior) is unmeasured because chrome-macho launch in this environment requires working sudo and it is currently broken.
