# Control #143 step 21 — sigexc resume path: `state_to_kernel` data fault on `ctxt->uc_mcontext.fpregs`

**Date:** 2026-10-09
**Branch:** task/sigexc-state-deref
**Base:** pr-arm64 = 9ec82da70 (step 20 merged)
**Input:** docs/DEATH-BT-SYMBOLIZE.md (step 20). **Static only** — no mldr run, no rebuild.

## Summary

The step-20 data fault (frame #0 `_state_to_kernel+0x1a2`, `r15 = 0x10002`,
called from frame #1 `_sigexc_handler+0xa7`) is an **ABI mismatch**: the guest
signal handler is handed a **raw FreeBSD `ucontext_t`** but reads it as a
**Linux `ucontext`**. At the Linux `fpregs` offset the FreeBSD ucontext holds
`mcontext.mc_fpformat` (`_MC_FPFMT_XMM` = `0x10002`), which `state_to_kernel`
dereferences as a pointer.

## (a) The two frames, file:line

Source: `src/external/xnu/darling/src/libsystem_kernel/emulation/src/linux_premigration/signal/sigexc.c`.

| frame | offset | source line | what |
|---|---|---|---|
| #0 `_state_to_kernel` | `+0x1a2` | **`sigexc.c:446`** | `kern_printf("sigexc: fcw out: 0x%x", fx->cwd);` — the deref `fx->cwd`, inside `mcontext_to_float_state()` (`sigexc.c:437`) **inlined** into `state_to_kernel` |
| #0 (arg load) | `+0x192` | `sigexc.c:256` | `mcontext_to_float_state(ctxt->uc_mcontext.fpregs, …)` — loads `fx` |
| #1 `_sigexc_handler` | `+0xa7` | **`sigexc.c:325`** | return address just after `state_to_kernel(ctxt, &tstate, &fstate);` |

`llvm-objdump -d <overlay>/usr/lib/system/libsystem_kernel.dylib`, symbols
`_state_to_kernel` @0x2bd70, `_sigexc_handler` @0x2c2a0:

```
 2bd7d: 49 89 f7        movq  %rsi, %r15        ; tstate
 2bd80: 49 89 fe        movq  %rdi, %r14        ; ctxt
 2bd87: e8 c4 08 00 00  callq 0x2c650 <_dump_gregs>       ; sigexc.c:254
 ...
 2bf02: 4d 8b be e0 00 00 00  movq 0xe0(%r14), %r15       ; +0x192  fx = ctxt->uc_mcontext.fpregs  (sigexc.c:256)
 2bf09: 4d 85 ff        testq %r15, %r15        ; +0x199  if (fx == NULL)          (sigexc.c:440)
 2bf0c: 0f 84 e7 00 00 00  je   0x2bff9          ;          -> memset path
 2bf12: 41 0f b7 37     movzwl (%r15), %esi     ; +0x1a2  <-- FAULT: reads fx->cwd (sigexc.c:446)
 2bf16: 48 8d 3d c6 0a 03 00  leaq 0x30ac6(%rip), %rdi
 2bf1f: e8 2c cf ff ff  callq 0x28e50 <___simple_kprintf>
```

```
 2c342: e8 29 fa ff ff  callq 0x2bd70 <_state_to_kernel>   ; sigexc.c:325
 2c347: 41 8b 4e 08     movl 0x8(%r14), %ecx     ; +0xa7  frame #1 (r14 = info)
```

## (b) Where `r15 = 0x10002` comes from — the call-to-deref trace

1. `_sigexc_handler` receives `ctxt` as its 3rd argument (rdx → rbx at
   `0x2c2b4`). `ctxt` is whatever the host signal-delivery path passed.
2. It calls `state_to_kernel(ctxt, &tstate, &fstate)` (`0x2c342`), so `ctxt`
   becomes `state_to_kernel`'s `r14` (`0x2bd80`).
3. `state_to_kernel` loads `r15 = *(ctxt + 0xe0)` (`0x2bf02`). Per
   `struct linux_ucontext` (`…/emulation/include/conversion/signal/sigaction.h:184-210`)
   `+0xe0` **is** `uc_mcontext.fpregs`:
   `uc_flags(0) uc_link(8) uc_stack(16,24B) → uc_mcontext @0x28`; inside,
   `gregs` is `sizeof(linux_gregset)=0xb8`, so `fpregs = 0x28 + 0xb8 = 0xe0`.
4. The `testq/je` NULL guard (`0x2bf09`) passes, because `r15 = 0x10002 ≠ 0`.
5. `0x2bf12` `movzwl (%r15),%esi` dereferences `0x10002` → SIGSEGV.

`0x10002` is not a pointer; it is the **FreeBSD FPU-format tag**. `ctxt` here
is a **FreeBSD `ucontext_t`** (`/usr/include/sys/_ucontext.h:34-50`:
`uc_sigmask(0,16) uc_mcontext(0x10) …`), and in FreeBSD's `mcontext_t`
(`/usr/include/x86/ucontext.h:106-166`) `mc_fpformat` sits at `+0xd0` with
`_MC_FPFMT_XMM = 0x10002`:

```
FreeBSD ucontext offset of mc_fpformat = uc_mcontext(0x10) + 0xd0 = 0xe0
```

So the byte the Linux code reads as `fpregs` is, in the FreeBSD layout,
`mcontext.mc_fpformat`, and its value for an XMM FPU save area is exactly
`0x10002`. **The handler was called with the native FreeBSD ucontext, not the
Linux one.**

## Why the raw FreeBSD ucontext reaches the guest handler

- `mldr_sys_rt_sigaction()` (`src/startup/mldr/sigaction_translate.c:275`)
  installs the guest handler **untranslated** as the FreeBSD disposition:
  `native_new.sa_handler = wire_act->lsa_handler;` (`:348`) →
  `sat_raw_syscall(SYS_sigaction, …)` (`:375`). Its own comment says the guest
  handler "is passed through untranslated".
- The FreeBSD→Linux ucontext converter exists —
  `mldr_ucontext_freebsd_to_linux()` (`src/startup/mldr/linux_ucontext.c:45`,
  which sets `out->uc_mcontext.fpregs = NULL` at `:100`) and its inverse
  `mldr_ucontext_linux_to_freebsd()` (`:114`) — but **neither has a single
  caller** (grep over `src/startup/mldr`: only the definition/declaration in
  `linux_ucontext.{c,h}`). The delivery-side conversion was never wired in.
- This is the SPEC's own **Open Question #5**
  (`docs/SPEC-signal-abi-bridge.md:617-630`): "если сама трансляция (не
  `rt_sigaction`, а именно доставка) не реализована где-то ещё,
  guest-обработчик получит указатели неверного layout при первом же реальном
  срабатывании поставленного обработчика". The prediction has now been
  observed.

## (c) Minimal-fix hypothesis (NOT applied)

Fix at the **delivery boundary**, not inside `state_to_kernel` — a guard there
cannot help, because `fpregs` is non-NULL garbage, not NULL.

1. In `mldr_sys_rt_sigaction()` (`sigaction_translate.c:348`), instead of
   storing the guest handler directly, install a thin mldr wrapper (a
   `sat_guest_handler_trampoline`, async-signal-safe, no `malloc`) as the
   FreeBSD `sa_sigaction`.
2. In that wrapper, at delivery, build Linux-ABI arguments:
   - `struct linux_siginfo` from the FreeBSD `siginfo_t` — the converter for
     this already exists (`src/startup/mldr/linux_siginfo.c`);
   - `struct linux_ucontext_compat` from the FreeBSD `ucontext_t` via
     `mldr_ucontext_freebsd_to_linux()` (`linux_ucontext.c:45`). This sets
     `fpregs = NULL` (`:100`), which makes the existing NULL guard in
     `mcontext_to_float_state()` (`sigexc.c:440`) actually fire, and the
     `movzwl (%r15),%esi` at `sigexc.c:446` is never reached.
3. Call the guest handler with those two Linux-ABI pointers.
4. After it returns, `mldr_ucontext_linux_to_freebsd()`
   (`linux_ucontext.c:114`) writes any guest register edits (RIP redirect for
   a signal-based longjmp, etc.) back into the real FreeBSD ucontext that
   `sigreturn(2)` will restore.

Owner of the fix is the same person who owns the `rt_sigaction` bridge
(`sigaction_translate.c` + `linux_ucontext.c`); the four reserved signals
(SIGILL/SIGBUS/SIGSEGV/SIGSYS) keep mldr's own handlers and are unaffected
(`sigaction_translate.c:313-336`).

## Notes / uncertainties

- The delivered signal number is not recoverable from this static pass (the
  core's `ucontext` is the FreeBSD one; the guest's `ctxt->uc_mcontext` read is
  garbage by construction). It must be a **non-reserved** signal, since
  SIGILL/SIGBUS/SIGSEGV/SIGSYS are not installed on the guest handler.
- The crash runs on mldr's `_sigsys_altstack`, i.e. the alt-stack mldr set up;
  the guest's own `sys_sigaltstack()` (guest `sigexc.c:246`) evidently did not
  take effect for this delivery. Recorded, not chased here.

## Repro

```sh
D=<overlay>/usr/lib/system/libsystem_kernel.dylib
llvm-nm -n "$D" | grep -E '_state_to_kernel|_sigexc_handler'
llvm-objdump -d --start-address=0x2bd70 --stop-address=0x2bf40 "$D"
llvm-objdump -d --start-address=0x2c2a0 --stop-address=0x2c370 "$D"
```
