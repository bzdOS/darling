/*
 * linux_ucontext.h — FreeBSD ucontext_t <-> Linux x86-64 ucontext ABI converter.
 *
 * purpose:  mldr executes Linux-ABI guest code (see freebsd_syscall_trap.c's
 *           "raw Linux-ABI syscalls" section — the upstream dyld/libdyld.dylib
 *           overlay makes real Linux syscalls, including rt_sigaction). When a
 *           signal reaches a guest-installed Linux signal handler, that
 *           handler is called with a `void *ucontext` third argument it
 *           expects to be a Linux `struct ucontext` (glibc/kernel ABI).
 *           FreeBSD's signal trampoline instead builds and passes a FreeBSD
 *           `ucontext_t`. The two have completely different field order and
 *           layout, so a raw pointer pass-through hands the guest handler
 *           garbage the moment it dereferences `uc_mcontext.gregs[...]` — the
 *           same class of bug documented for struct stat/sockaddr_un/
 *           sigaltstack elsewhere in this tree (see freebsd_syscall_trap.c
 *           top-of-file comment).
 *
 *           This header/source pair convert in both directions:
 *             freebsd_to_linux — before invoking the guest handler, so it
 *               reads registers in the layout it expects.
 *             linux_to_freebsd — after the guest handler returns, so any
 *               edits it made (RIP redirect for a signal-based longjmp,
 *               clearing a flag, etc.) are reflected back into the real
 *               FreeBSD ucontext_t that FreeBSD's kernel will restore from
 *               on sigreturn(2).
 *
 * Architecture: x86-64 only. mldr's Linux-ABI syscall trap (SIGSYS class 3 /
 *           raw-syscall trampoline patching, see freebsd_syscall_trap.c) is
 *           x86-64-only, so there is no aarch64 caller for this yet.
 *
 * Authoritative Linux x86-64 gregset layout: this project already has one
 * hand-verified Linux<->BSD ucontext converter, written for the XNU/Darling
 * BSD-syscall side of mldr (Linux guest -> Darling's *macOS*-flavoured BSD
 * ucontext, not FreeBSD's, but the Linux-side struct layout is identical
 * since it is dictated by the Linux kernel/glibc ABI, not by which BSD reads
 * it). See:
 *   src/external/xnu/darling/src/libsystem_kernel/emulation/include/conversion/signal/sigaction.h:118-124
 *     struct linux_gregset {
 *         long long r8, r9, r10, r11, r12, r13, r14, r15, rdi, rsi, rbp, rbx;
 *         long long rdx, rax, rcx, rsp, rip, efl;
 *         short cs, gs, fs, __pad0;
 *         long long err, trapno, oldmask, cr2;
 *     };
 *   and its consumer at .../emulation/src/conversion/signal/sigaction.c:118-124
 *     (copyreg() list confirms which named fields are actually populated:
 *     rax,rbx,rcx,rdx,rdi,rsi,rbp,rsp,r8-r15,rip,cs,fs,gs,efl — err/trapno/
 *     oldmask/cr2 are NOT touched by that converter, i.e. even the existing
 *     authoritative converter in this tree treats them as not-worth-forging
 *     on the outbound/inbound direction it implements).
 * This struct layout matches the well-known glibc/Linux x86-64
 * `mcontext_t.gregs[NGREG]` REG_* index order (R8..R15, RDI, RSI, RBP, RBX,
 * RDX, RAX, RCX, RSP, RIP, EFL, CSGSFS, ERR, TRAPNO, OLDMASK, CR2) — the
 * `short cs,gs,fs,__pad0` quartet here is the same 8 bytes as glibc's single
 * REG_CSGSFS slot, just spelled out field-by-field instead of as one opaque
 * greg_t.
 *
 * struct linux_ucontext_compat below is our OWN struct (defined in this file,
 * not included from XNU/Linux guest headers per the task constraint of not
 * pulling in guest headers), but its layout is copied field-for-field from
 * struct linux_ucontext / struct linux_mcontext at sigaction.h:184-210 so that
 * it is byte-compatible with what a Linux x86-64 signal handler expects to
 * find at the pointer it is handed.
 *
 * FreeBSD-side field names (mc_rdi, mc_rsi, mc_rax, mc_rip, mc_rsp, mc_rflags,
 * mc_r8..mc_r15, mc_trapno, mc_err) are taken from LIVE USE in this tree at
 * src/startup/mldr/freebsd_syscall_trap.c (e.g. lines 2551-2556, 2610-2620,
 * 2763-2777, 2798-2827) — that file already reads/writes exactly these
 * mcontext_t fields from real SIGSYS/SIGILL handlers on this codebase's
 * target FreeBSD 15.1, so they are confirmed to compile and match the field
 * names the FreeBSD <ucontext.h>/<machine/ucontext.h> on this project's
 * target actually uses. mc_trapno/mc_err are NOT used anywhere in
 * freebsd_syscall_trap.c itself (only named in the compiler-rt/OpenJDK BSD
 * shims under src/external/, which are third-party code for a related but
 * not identical struct) — see the honesty note in the .c file and the task
 * report for what is and is not confirmed.
 *
 * input/output/sideEffects: documented per-function below.
 */

#pragma once

#ifdef DARLING_FREEBSD

#include <stdint.h>
#include <ucontext.h>

/* ── Linux x86-64 struct linux_stack — sigaltstack(2) stack_t layout ───────
 * Field ORDER differs from FreeBSD's stack_t (ss_sp, ss_size, ss_flags):
 * Linux orders it (ss_sp, ss_flags, ss_size). Confirmed against this tree's
 * existing Linux-ABI converter at
 * src/external/xnu/darling/src/libsystem_kernel/emulation/include/xnu_syscall/bsd/impl/signal/sigaltstack.h:11-16
 * and the divergence is called out explicitly in this tree at
 * freebsd_syscall_trap.c:257-260 (LINUX_SYS_sigaltstack comment). */
struct linux_stack_compat {
    void          *ss_sp;
    int            ss_flags;
    unsigned long  ss_size;
};

/* ── Linux x86-64 general-purpose register set ──────────────────────────────
 * Field order and sizes copied verbatim from struct linux_gregset at
 * sigaction.h:118-124 (see file header comment above for the full citation
 * and why that struct is authoritative for the Linux side of this
 * conversion). This is the layout a Linux x86-64 signal handler's
 * `ucontext->uc_mcontext.gregs[...]` / named-field access expects. */
struct linux_gregset_compat {
    long long r8, r9, r10, r11, r12, r13, r14, r15;
    long long rdi, rsi, rbp, rbx;
    long long rdx, rax, rcx, rsp, rip, efl;
    /* Packed the same as glibc's single 8-byte REG_CSGSFS slot. */
    short cs, gs, fs, __pad0;
    /* NOT populated by mldr_ucontext_freebsd_to_linux / read back by
     * mldr_ucontext_linux_to_freebsd — see function contracts below for why
     * (oldmask: signal-mask translation is out of scope here by design;
     * err/trapno/cr2: FreeBSD's SIGSYS/SIGILL delivery for the cases mldr
     * handles does not carry meaningful values for these — mc_trapno/mc_err
     * exist on the FreeBSD side but are not populated by mldr's existing
     * SIGSYS/SIGILL handlers in freebsd_syscall_trap.c, so there is nothing
     * authentic to copy from). Left zeroed; a handler that inspects these
     * (e.g. to distinguish fault types) will see 0, not the real fault
     * cause. */
    long long err, trapno, oldmask, cr2;
};

/* ── Linux x86-64 FPU/XMM state (_fpstate) ──────────────────────────────────
 * Layout copied from sigaction.h:104-116 (`struct _fpstate` / linux_fpregset_t)
 * purely for documentation of what is NOT converted (see mcontext_compat.fpregs
 * below) — this project does not allocate or populate one; kept here only so
 * a future implementer has the authoritative shape at hand without having to
 * re-derive it. */
struct linux_fpstate_compat {
    unsigned short cwd, swd, ftw, fop;
    unsigned long long rip, rdp;
    unsigned mxcsr, mxcr_mask;
    struct {
        unsigned short significand[4], exponent, padding[3];
    } _st[8];
    struct {
        unsigned element[4];
    } _xmm[16];
    unsigned padding[24];
};

/* ── Linux x86-64 mcontext ───────────────────────────────────────────────── */
struct linux_mcontext_compat {
    struct linux_gregset_compat gregs;
    /*
     * fpregs: a Linux mcontext's fpregs pointer normally points at an
     * _fpstate block (either inline in the signal frame or in
     * uc_mcontext.fpregs->... depending on kernel version) holding
     * SSE/x87/AVX register state at signal-delivery time.
     *
     * KNOWN LIMITATION: mldr_ucontext_freebsd_to_linux() always sets this to
     * NULL rather than translating FreeBSD's FPU save area (mc_fpstate /
     * struct savefpu) into a linux_fpstate_compat. Consequence: a guest
     * signal handler that reads uc_mcontext.fpregs (e.g. to inspect or
     * restore XMM/x87 state, as some runtime unwinders, JITs, or crash
     * handlers do) will dereference NULL if it does not first check for
     * NULL, or will see "no FPU state available" if it does check — either
     * way it will NOT see the real FPU register contents at fault time. This
     * is acceptable for mldr's current use (translating handlers this tree
     * installs itself, e.g. simple SIGSEGV/SIGBUS handlers that only inspect
     * integer registers and the fault address) but is a correctness gap for
     * any guest handler that touches FPU state.
     */
    struct linux_fpstate_compat *fpregs;
    unsigned long long __reserved[8];
};

/* ── Linux x86-64 ucontext ───────────────────────────────────────────────── */
struct linux_ucontext_compat {
    unsigned long                  uc_flags;
    struct linux_ucontext_compat  *uc_link;
    struct linux_stack_compat      uc_stack;
    struct linux_mcontext_compat   uc_mcontext;
    /*
     * uc_sigmask: intentionally left as raw zeroed bytes, NOT translated.
     *
     * Rationale (per task spec): translating a signal mask between Linux's
     * and FreeBSD's signal-number spaces requires the same
     * mldr_linux_signo_to_freebsd()-style per-bit table that already exists
     * in freebsd_syscall_trap.c (see mldr_linux_signo_to_freebsd(),
     * freebsd_syscall_trap.c:319-341) plus its inverse. Duplicating that
     * table here would create two sources of truth for the same mapping.
     * The caller (whoever owns signal delivery / rt_sigaction /
     * rt_sigprocmask emulation) is responsible for populating/consuming the
     * mask using the existing table; this converter only moves register
     * state.
     *
     * Sized as a raw byte array (not `linux_sigset_t`, a guest type we do
     * not include) matching the Linux x86-64 kernel sigset_t ABI, which is a
     * 64-bit (8-byte) bitmask on x86-64 (NOT glibc's 128-byte userspace
     * sigset_t — the ucontext installed by the *kernel* on the signal frame
     * uses the kernel's __kernel_sigset_t, 8 bytes on x86-64/64 signals).
     * UNVERIFIED: this project has not confirmed against a live Linux
     * x86-64 kernel header that the kernel-frame uc_sigmask is exactly 8
     * bytes here rather than glibc's wider __NSIG_WORDS*8 (a debugger
     * dumping the actual struct captured at signal delivery would settle
     * it) — flagged in the task report as a known uncertainty.
     */
    unsigned char uc_sigmask[8];
};

/*
 * purpose:    Convert a FreeBSD-native ucontext_t (as passed to a FreeBSD
 *             signal handler / captured at SIGSYS/SIGILL trap time in this
 *             tree) into the Linux x86-64 ucontext layout a Linux-ABI guest
 *             signal handler expects as its third argument.
 * input:      native — FreeBSD ucontext_t captured for the current signal
 *             (must not be NULL; caller owns its lifetime).
 * output:     *out is fully overwritten with the Linux-layout equivalent:
 *             all 16 general-purpose registers + rip/efl + cs/fs/gs are
 *             copied; uc_stack is NOT populated (see sideEffects); uc_flags
 *             is set to 0; uc_link is set to NULL; uc_sigmask is zeroed
 *             (see the field comment on struct linux_ucontext_compat);
 *             uc_mcontext.fpregs is set to NULL (see the field comment on
 *             struct linux_mcontext_compat); err/trapno/oldmask/cr2 in the
 *             gregset are zeroed.
 * sideEffects: none (pure read of *native, pure write of *out). uc_stack is
 *             left zeroed rather than derived from the FreeBSD ucontext's
 *             own alt-stack fields — mldr does not currently need a guest
 *             handler to see altstack info via ucontext (it would come from
 *             sigaltstack(2) emulation instead), and FreeBSD's ucontext_t
 *             stack fields (uc_stack) are FreeBSD's OWN altstack shape, not
 *             a 1:1 match for struct linux_stack_compat's semantics anyway.
 */
void mldr_ucontext_freebsd_to_linux(const ucontext_t *native,
                                     struct linux_ucontext_compat *out);

/*
 * purpose:    Convert a (possibly guest-handler-modified) Linux x86-64
 *             ucontext back into the FreeBSD-native ucontext_t that
 *             FreeBSD's kernel will restore from on return from the signal
 *             trampoline (sigreturn(2)). Needed because a Linux guest
 *             handler may deliberately rewrite registers — most commonly
 *             uc_mcontext.gregs[REG_RIP] for a signal-based non-local jump,
 *             but in principle any integer register.
 * input:      in — Linux ucontext as (possibly) modified by the guest
 *             handler that mldr_ucontext_freebsd_to_linux() built (must not
 *             be NULL).
 * output:     *native has its general-purpose registers (rdi, rsi, rdx, rcx,
 *             r8-r15, rbp, rbx, rax, rsp, rip, rflags) overwritten from
 *             *in's gregset. Segment registers (cs/fs/gs) are copied back as
 *             well, matching what mldr_ucontext_freebsd_to_linux() exported.
 *             Everything else in *native (uc_stack, uc_sigmask, uc_flags,
 *             mc_trapno/mc_err/mc_fsbase/mc_gsbase/FPU save area, etc.) is
 *             left untouched — this function only overwrites the specific
 *             mc_* fields it owns, so the caller's existing native ucontext
 *             (which the kernel filled in and which sigreturn(2) needs
 *             intact for anything this function doesn't know about) is not
 *             clobbered.
 * sideEffects: none beyond the *native writes described above. Does not
 *             touch FPU state (see struct linux_mcontext_compat.fpregs
 *             comment) — if the guest handler modified FPU/XMM state via
 *             uc_mcontext.fpregs, that modification is silently dropped;
 *             the FreeBSD FPU save area is left exactly as the kernel set
 *             it up.
 */
void mldr_ucontext_linux_to_freebsd(const struct linux_ucontext_compat *in,
                                     ucontext_t *native);

#endif /* DARLING_FREEBSD */
