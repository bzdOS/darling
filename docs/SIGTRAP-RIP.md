# Control #149 step 27 — SIGTRAP RIP: the fatal trap is `int3` (0xCC) in Chrome's initializer

**Date:** 2026-10-09
**Branch:** task/sigtrap-rip
**Base:** pr-arm64 = caef39751 (step 26 merged)
**mldr:** rebuilt with rip/si_code/si_addr in the SIGTRAP-AUDIT-DELIVER line.

## 1. What was instrumented

Added `rip`, `si_code`, `si_addr` to the existing SIGTRAP-AUDIT-DELIVER marker in
`sat_guest_handler_trampoline()` (`sigaction_translate.c:338-348`):

```c
const ucontext_t *uc = (const ucontext_t *)native_uctx;
long long rip = (long long)uc->uc_mcontext.mc_rip;
int si_code = native_info->si_code;
void *si_addr = native_info->si_addr;
```

These extract the instruction pointer from the FreeBSD `ucontext_t`'s
`mcontext.mc_rip` and the signal code/address from `siginfo_t`. The `mc_rip`
offset was confirmed against the FreeBSD layout in step 21
(`docs/SIGEXC-STATE-DEREF.md`).

## 2. Run — pristine, no instrumentation traps

Recipe `docs/BZERO-DEATH-ARGS.md`, **no** `MLDR_TRAP_RESUME_AT`, wrapper
`timeout -k 30 1700`, log `diag/sigexc-rip-2.log`.

## 3. Result — the DELIVER line with rip

```
[darling-mldr] SIGTRAP-AUDIT-DELIVER: native SIGTRAP -> guest handler=0x219527d6f2a0 (linux_signo=5) rip=0x2195325c77c1 si_code=1 si_addr=0x2195325c77c1
```

- `rip = 0x2195325c77c1`
- `si_code = 1` = `TRAP_BRKPT` (breakpoint trap)
- `si_addr = 0x2195325c77c1` (same as rip — characteristic of int3 delivery)
- Log frozen at **1 292 541 lines** / 102 451 684 B; no `RUN_DONE`.
- `dmesg`: `pid 15815 (mldr), jid 0, uid 0: exited on signal 5`

## 4. Resolving the rip — image + offset

Chrome framework mapping from the log's dyld output:

```
__TEXT at 0x219528FF7000->0x219538199FFF with permissions r.x
```

```
offset = rip - __TEXT_base
       = 0x2195325c77c1 - 0x219528FF7000
       = 0x95D07C1
```

Image: `Google Chrome for Testing Framework` (154.0.8029.0), offset **0x95D07C1**.

## 5. The instruction at rip — `int3` (0xCC)

RIP points one byte past the `int3` — this is how x86 delivers breakpoint
traps: the CPU advances RIP past the 1-byte `0xCC` instruction before
delivering `SIGTRAP` with `si_code = TRAP_BRKPT`.

`llvm-objdump -d --start-address=0x95D07b0 --stop-address=0x95D07d0` on the
Chrome binary:

```
 95d07b0: 48 89 df          movq   %rbx, %rdi
 95d07b3: e8 38 f9 ff ff    callq  0x95d00f0
 95d07b8: 48 89 df          movq   %rbx, %rdi
 95d07bb: e8 50 5e a3 f6    callq  0x6610     <_ChromeMain+0x2630>
 95d07c0: cc                int3              <-- THE TRAP (0xCC)
 95d07c1: 0f 0b             ud2
 95d07c3: 66 66 66 66 ...   nopw   ...
```

Raw bytes at 0x95D07C0 confirmed from the file: `0xCC` = `int3`.
The `ud2` (0x0F 0B) immediately after is the standard compiler pattern:
`int3; ud2` = `__builtin_trap()` or an `__assume(0)` / unreachable path.

### Nearest symbol

Chrome is stripped: only 3 defined text symbols (`_ChromeMain` @0x3fe0,
`_ChromeAppModeStart_v8` @0x2840, `_ChromeWebAppShortcutCopierMain` @0x2a00).
The nearest is `_ChromeMain` at 0x3fe0; the trap is at `_ChromeMain+0x95CC7E0`
— effectively **stripped**.

## 6. Verdict

The fatal SIGTRAP is a **deliberate `int3` breakpoint** (0xCC) compiled into
Chrome's own code, at offset 0x95D07C0 in the `__TEXT` segment. It is not a
mldr defect, not a stray byte, and not an external signal source. The pattern
`call; int3; ud2` is the compiler's `__builtin_trap()` emission — Chrome's code
hits an assertion or unreachable path during its initializer.

The initializer call from dyld (`calling initializer function 0x21952b1214c0`)
lands at Chrome+0x212A4C0. That initializer runs code that eventually reaches
Chrome+0x95D07C0, which executes `int3`. The signal is delivered to mldr's
trampoline, which forwards it to the guest's `_sigexc_handler`; the guest
resets the disposition to `SIG_DFL` and re-raises, dying on signal 5.

## Repro

```sh
# code: rip/si_code/si_addr in SIGTRAP-AUDIT-DELIVER (this commit)
# rebuild: sh build-freebsd/build-mldr-only.sh
export PATH=/usr/local/bin:/usr/local/sbin:/usr/bin:/bin:/sbin:/usr/sbin
export DARLING_SRC_DIR=$DARLING_SRC_DIR
export DARLING_OVERLAY=$DARLING_OVERLAY
export DARLING_BUILD_DIR=$DARLING_BUILD_DIR
export DARLING_TEST_BINARY=cft-fwmacho-probe-macho
export DARLING_STAGING_TREES=$(cat /tmp/stub76-staging.txt)
export DARLING_SMOKE_REFRESH=1
RUN="env DARLING_SRC_DIR=${DARLING_SRC_DIR} DARLING_OVERLAY=${DARLING_OVERLAY} DARLING_BUILD_DIR=${DARLING_BUILD_DIR}"
RUN="${RUN} DARLING_TEST_BINARY=${DARLING_TEST_BINARY}"
RUN="${RUN} DARLING_STAGING_TREES=${DARLING_STAGING_TREES}"
RUN="${RUN} DARLING_SMOKE_REFRESH=1"
for v in DYLD_BIND_AT_LAUNCH DYLD_PRINT_LIBRARIES DYLD_PRINT_LIBRARIES_POST_LAUNCH DYLD_PRINT_BINDINGS DYLD_PRINT_WEAK_BINDINGS DYLD_PRINT_APIS DYLD_PRINT_INTERPOSING DYLD_PRINT_SEGMENTS DYLD_PRINT_STATISTICS DYLD_PRINT_STATISTICS_DETAILS DYLD_PRINT_RPATHS DYLD_PRINT_WARNINGS DYLD_PRINT_INITIALIZERS DYLD_PRINT_DOFS DYLD_PRINT_OPTS DYLD_PRINT_ENV DYLD_PRINT_CODE_SIGNATURES DYLD_PRINT_REBASINGS DYLD_PRINT_TO_STDERR; do RUN="${RUN} ${v}=1"; done
RUN="${RUN} ${DARLING_BUILD_DIR}/launch-dynamic"
cd $DARLING_SRC_DIR/../diag && timeout -k 30 1700 sudo ${RUN} > sigexc-rip-2.log 2>&1
grep 'SIGTRAP-AUDIT-DELIVER' sigexc-rip-2.log
# rip=0x2195325c77c1 si_code=1 si_addr=0x2195325c77c1

# Resolve:
grep '__TEXT at' sigexc-rip-2.log | grep Chrome   # base = 0x219528FF7000
# offset = 0x2195325c77c1 - 0x219528FF7000 = 0x95D07C1

# Disassemble:
CHROME_BIN="$DARLING_OVERLAY/Frameworks/Google Chrome for Testing Framework.framework/Versions/154.0.8029.0/Google Chrome for Testing Framework"
llvm-objdump -d --start-address=0x95D07b0 --stop-address=0x95D07d0 "$CHROME_BIN"
# 95d07c0: cc  int3

# Raw bytes:
python3 -c "
with open('$CHROME_BIN', 'rb') as f:
    f.seek(0x95D07c0); print(f'{f.read(1)[0]:02x}')  # cc = int3
"
```

Log `diag/sigexc-rip-2.log` (102 451 684 B; no `RUN_DONE` — died on signal 5).