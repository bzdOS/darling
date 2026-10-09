# Control #154 step 32 — clean chrome run on the fd=-1 mldr: the SIGTRAP death is gone

**Date:** 2026-10-09
**Branch:** task/chrome-retry
**Base:** pr-arm64 = 4060c78ef (step 31 merged)
**Method:** clean runs (no dtrace, no `MLDR_TRAP_AT`/`RESUME_AT`), recipe
`docs/SIGTRAP-RIP.md`, `timeout -k 5 300`, mldr rebuilt with the step-31 fd=-1
fix. Two binaries were run: the scenario's `cft-fwmacho-probe-macho` (which
loads the framework and so reaches the old crash) and the dispatch's named
`chrome-macho`. Logs: `$DARLING_SRC_DIR/../diag/chrome-retry.log` (probe),
`.../chrome-macho-run.log` (chrome-macho).

---

## 0. Result

- **The SIGTRAP death of steps 23-27 is gone.** In the probe run there is
  **no `errno=22`** and **no `SIGTRAP-AUDIT-DELIVER`** (no SIGTRAP delivered),
  and the run's mldr did **not** exit on signal 5 (unlike steps 30/31a). The
  initializer that used to crash (`Chrome+0x212A4C0`, PartitionAlloc) now runs
  to completion and the **next** initializer runs.
- **The new stop is a framework `dlopen` path problem, not the allocator.**
  - `cft-fwmacho-probe-macho` (the SIGTRAP-RIP scenario binary) loads the
    framework and then ends at its own `dlopen` path probe: path `[0]`
    `/Frameworks/...` → OK; `/tmp/Frameworks/...`, `//../Frameworks/...`,
    `/FWMACHO` → `image not found`.
  - `chrome-macho` stops **earlier**: it tries to `dlopen` the framework at
    `//../Frameworks/...` (relative to its own location) and that fails
    `image not found` — the framework never loads, so this run does not even
    reach the reservation. `errno=22` and SIGTRAP are likewise absent.

## 1. The probe run (`cft-fwmacho-probe-macho`) — old crash gone

The run log ends (`chrome-retry.log`, lines 1292541-1292560):

```
dyld: calling initializer function 0x19b681214c0 in .../Google Chrome for Testing Framework   <- the old crash point (Chrome+0x212A4C0), now completes
dyld: calling initializer function 0x19b6805c300 in .../Google Chrome for Testing Framework   <- the next initializer runs
  dlopen_internal(/Frameworks/.../Google Chrome for Testing Framework) ==> 0x7fba4c477ff1
[0] /Frameworks/.../Google Chrome for Testing Framework
  stat=1  dlopen=OK
[1] /tmp/Frameworks/...   dlopen=...: image not found
[2] //../Frameworks/...   dlopen=...: image not found
[3] /FWMACHO             dlopen=...: image not found
```

- `grep -c 'errno=22' chrome-retry.log` → **0**
- `grep -c 'SIGTRAP-AUDIT-DELIVER' chrome-retry.log` → **0**
- the only `SIGTRAP` lines are two `SIGTRAP-AUDIT: rt_sigaction(5)` **handler
  registrations** (the guest installing its crash handler), not deliveries.
- `dmesg` shows no `exited on signal 5` for this run's mldr.

So the failure that step 27 located (the crash routine `Chrome+0x95D0780`
called from `Chrome+0x18633E3` after the 32 GiB reservation returned NULL) no
longer happens: the reservation now succeeds (step 31), so the crash path is
never entered. The run progresses to the framework's own `dlopen`/initializer
phase.

## 2. The `chrome-macho` run — stops earlier, at the framework dlopen

`chrome-macho` (`tests/chrome-macho`, 26 KB) `dlopen`s the framework at a path
relative to its own location and calls `ChromeMain`. It fails before loading the
framework (`chrome-macho-run.log`, last lines):

```
  _NSGetExecutablePath(...)
dlopen_internal(//../Frameworks/Google Chrome for Testing Framework.framework/Versions/154.0.8029.0/Google Chrome for Testing Framework, 0x00000105)
  dlopen_internal() failed, error: 'dlopen(//../Frameworks/.../Google Chrome for Testing Framework, 261): image not found'
dlopen //../Frameworks/.../Google Chrome for Testing Framework: dlopen(//../Frameworks/...): image not found
```

- `grep -c 'errno=22'` → **0**; `grep -c 'SIGTRAP-AUDIT-DELIVER'` → **0**.
- No `Google Chrome for Testing Framework` initializer line — the framework was
  never loaded.
- Early warning in the log (line 9): `Failed to allocate stack of 65536 bytes: 3
  (No such process)` (the run continued past it).

**New blocker, named:** `chrome-macho` cannot resolve its own framework path —
`dlopen("//../Frameworks/Google Chrome for Testing Framework.framework/...")`
returns `image not found`. This is a path-resolution blocker before the
framework loads, distinct from the allocator death that step 31 fixed. The
probe's `[2] //../Frameworks/...` fallback fails the same way; only the
absolute `/Frameworks/...` path resolves.

## 3. Repro

```sh
# mldr with the fd=-1 fix (step 31) must already be built:
sh build-freebsd/build-mldr-only.sh        # installs ${DARLING_BUILD_DIR}/dserver/mldr-real/mldr

# clean run (no dtrace, no MLDR_TRAP_*), recipe docs/SIGTRAP-RIP.md, timeout -k 5 300
timeout -k 5 300 env DARLING_SRC_DIR=$DARLING_SRC_DIR DARLING_OVERLAY=$DARLING_OVERLAY \
  DARLING_BUILD_DIR=$DARLING_BUILD_DIR DARLING_TEST_BINARY=cft-fwmacho-probe-macho \
  DARLING_STAGING_TREES="$(cat /tmp/stub76-staging.txt)" DARLING_SMOKE_REFRESH=1 \
  DYLD_BIND_AT_LAUNCH=1 DYLD_PRINT_LIBRARIES=1 DYLD_PRINT_INITIALIZERS=1 DYLD_PRINT_TO_STDERR=1 \
  $DARLING_BUILD_DIR/launch-dynamic > diag/chrome-retry.log 2>&1
grep -c 'errno=22\|SIGTRAP-AUDIT-DELIVER' diag/chrome-retry.log   # 0

# the head's named binary
#   ... DARLING_TEST_BINARY=chrome-macho ... > diag/chrome-macho-run.log 2>&1
```
