/*
 * linux_ucontext.c — implementation of the FreeBSD <-> Linux x86-64 ucontext
 * converter declared in linux_ucontext.h. See that file's header comment for
 * the full citation of where the Linux-side and FreeBSD-side field names
 * came from, and for what is deliberately NOT converted (signal mask, FPU
 * state) and why.
 *
 * Both functions are plain, allocation-free field copies: no globals, no
 * dynamic memory, safe to call from inside a signal handler (this is
 * exactly the context mldr calls them from — see the raw-Linux-ABI
 * rt_sigaction emulation path in freebsd_syscall_trap.c).
 */

#ifdef DARLING_FREEBSD

#include "linux_ucontext.h"

#include <string.h>

/*
 * purpose:    Convert a FreeBSD-native ucontext_t into the Linux x86-64
 *             ucontext layout a Linux-ABI guest signal handler expects.
 * input/output/sideEffects: see linux_ucontext.h.
 *
 * FreeBSD-side field names used below (mc_rdi, mc_rsi, mc_rdx, mc_rcx,
 * mc_r8..mc_r15, mc_rbp, mc_rbx, mc_rax, mc_rsp, mc_rip, mc_rflags) are
 * CONFIRMED against live use elsewhere in this tree — see
 * src/startup/mldr/freebsd_syscall_trap.c:2551-2556, 2610-2620, 2763-2777,
 * 2798-2827, which already reads/writes exactly these mcontext_t fields from
 * real FreeBSD 15.1 SIGSYS/SIGILL handlers in this codebase.
 *
 * mc_cs, mc_fs, mc_gs (16-bit segment selectors) are NOT used anywhere else
 * in this tree — freebsd_syscall_trap.c only ever touches mc_fsbase/
 * mc_gsbase (the 64-bit MSR-backed segment BASE addresses used for TLS, a
 * completely different pair of fields from the 16-bit selectors). The names
 * and 16-bit width used here are from the author's recollection of FreeBSD's
 * amd64 <machine/ucontext.h> struct mcontext layout, NOT verified against an
 * actual FreeBSD header in this session (no FreeBSD headers are available on
 * this Linux host — see the task's honesty requirement and the report's
 * "uncertainties" list). If mc_cs/mc_fs/mc_gs turn out to be named or sized
 * differently on the real target, this is the block to fix; everything else
 * in this function does not depend on them.
 */
void
mldr_ucontext_freebsd_to_linux(const ucontext_t *native,
                                struct linux_ucontext_compat *out)
{
    memset(out, 0, sizeof(*out));

    if (native == NULL || out == NULL)
        return;

    const mcontext_t *mc = &native->uc_mcontext;
    struct linux_gregset_compat *g = &out->uc_mcontext.gregs;

    g->r8  = (long long)mc->mc_r8;
    g->r9  = (long long)mc->mc_r9;
    g->r10 = (long long)mc->mc_r10;
    g->r11 = (long long)mc->mc_r11;
    g->r12 = (long long)mc->mc_r12;
    g->r13 = (long long)mc->mc_r13;
    g->r14 = (long long)mc->mc_r14;
    g->r15 = (long long)mc->mc_r15;

    g->rdi = (long long)mc->mc_rdi;
    g->rsi = (long long)mc->mc_rsi;
    g->rbp = (long long)mc->mc_rbp;
    g->rbx = (long long)mc->mc_rbx;

    g->rdx = (long long)mc->mc_rdx;
    g->rax = (long long)mc->mc_rax;
    g->rcx = (long long)mc->mc_rcx;
    g->rsp = (long long)mc->mc_rsp;
    g->rip = (long long)mc->mc_rip;
    g->efl = (long long)mc->mc_rflags;

    /* Segment selectors — see the function-header caveat above: field names
     * and width are UNVERIFIED against a real FreeBSD amd64 header in this
     * session. */
    g->cs = (short)mc->mc_cs;
    g->gs = (short)mc->mc_gs;
    g->fs = (short)mc->mc_fs;
    g->__pad0 = 0;

    /* err/trapno/oldmask/cr2: left zeroed. See the field comment on
     * struct linux_gregset_compat in linux_ucontext.h for why — FreeBSD's
     * mc_trapno/mc_err exist but are not populated by mldr's existing
     * SIGSYS/SIGILL handlers, and oldmask is the caller's job (signal-mask
     * translation table, not register-context conversion). */

    /* uc_flags / uc_link: no equivalent concept is tracked by mldr today;
     * left zero/NULL (already zeroed by the memset above). */

    /* uc_stack: intentionally left zeroed — see linux_ucontext.h's
     * mldr_ucontext_freebsd_to_linux() sideEffects comment. */

    /* uc_mcontext.fpregs: intentionally left NULL — see the
     * struct linux_mcontext_compat.fpregs comment in linux_ucontext.h for
     * the consequence (a handler reading FPU state sees none). */
    out->uc_mcontext.fpregs = (struct linux_fpstate_compat *)0;

    /* uc_sigmask: intentionally left zeroed — see the struct
     * linux_ucontext_compat.uc_sigmask comment in linux_ucontext.h. */
}

/*
 * purpose:    Write a (possibly guest-handler-modified) Linux x86-64
 *             ucontext's general-purpose registers back into the
 *             FreeBSD-native ucontext_t that will actually be restored by
 *             sigreturn(2).
 * input/output/sideEffects: see linux_ucontext.h.
 */
void
mldr_ucontext_linux_to_freebsd(const struct linux_ucontext_compat *in,
                                ucontext_t *native)
{
    if (in == NULL || native == NULL)
        return;

    mcontext_t *mc = &native->uc_mcontext;
    const struct linux_gregset_compat *g = &in->uc_mcontext.gregs;

    mc->mc_r8  = (__typeof__(mc->mc_r8))g->r8;
    mc->mc_r9  = (__typeof__(mc->mc_r9))g->r9;
    mc->mc_r10 = (__typeof__(mc->mc_r10))g->r10;
    mc->mc_r11 = (__typeof__(mc->mc_r11))g->r11;
    mc->mc_r12 = (__typeof__(mc->mc_r12))g->r12;
    mc->mc_r13 = (__typeof__(mc->mc_r13))g->r13;
    mc->mc_r14 = (__typeof__(mc->mc_r14))g->r14;
    mc->mc_r15 = (__typeof__(mc->mc_r15))g->r15;

    mc->mc_rdi = (__typeof__(mc->mc_rdi))g->rdi;
    mc->mc_rsi = (__typeof__(mc->mc_rsi))g->rsi;
    mc->mc_rbp = (__typeof__(mc->mc_rbp))g->rbp;
    mc->mc_rbx = (__typeof__(mc->mc_rbx))g->rbx;

    mc->mc_rdx = (__typeof__(mc->mc_rdx))g->rdx;
    mc->mc_rax = (__typeof__(mc->mc_rax))g->rax;
    mc->mc_rcx = (__typeof__(mc->mc_rcx))g->rcx;
    mc->mc_rsp = (__typeof__(mc->mc_rsp))g->rsp;
    /* mc_rip is the field most likely to have been deliberately changed by
     * the guest handler (signal-based longjmp, or a handler that fixes up
     * the faulting instruction and wants to resume elsewhere) — this is the
     * whole reason a *return* conversion is needed at all, per the task's
     * "obработчик может изменить регистры" motivation. */
    mc->mc_rip = (__typeof__(mc->mc_rip))g->rip;
    mc->mc_rflags = (__typeof__(mc->mc_rflags))g->efl;

    /* Segment selectors — same UNVERIFIED-field-name caveat as in
     * mldr_ucontext_freebsd_to_linux() above. */
    mc->mc_cs = (__typeof__(mc->mc_cs))g->cs;
    mc->mc_gs = (__typeof__(mc->mc_gs))g->gs;
    mc->mc_fs = (__typeof__(mc->mc_fs))g->fs;

    /* Everything else in *native (mc_trapno, mc_err, mc_addr, mc_fsbase,
     * mc_gsbase, mc_fpstate/mc_ownedfp/mc_fpformat, uc_stack, uc_sigmask,
     * uc_flags, ...) is deliberately left untouched: this function only
     * owns the general-purpose integer registers it read out of the Linux
     * side. FreeBSD's kernel-filled values for everything else must survive
     * unmodified for sigreturn(2) to work correctly (e.g. mc_fsbase/
     * mc_gsbase — see the arch_prctl-related comments at
     * freebsd_syscall_trap.c:2309-2318 for why sigreturn is picky about
     * these two specifically). FPU state is a known gap — see the
     * struct linux_mcontext_compat.fpregs comment in linux_ucontext.h: if
     * the guest handler edited FPU/XMM state via uc_mcontext.fpregs, that
     * edit is silently dropped here. */
}

#endif /* DARLING_FREEBSD */
