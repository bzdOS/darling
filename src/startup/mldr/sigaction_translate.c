/*
 * sigaction_translate.c — Linux ABI `rt_sigaction` -> FreeBSD `sigaction(2)`
 * bridge for mldr. Implementation of mldr_sys_rt_sigaction(); see
 * sigaction_translate.h for the full contract and
 * docs/SPEC-signal-abi-bridge.md for the derivation of every layout/flag
 * table below.
 *
 * sideEffects: defines sat_registry[], a process-wide static array (see
 *              "Threading" in the header) that records, per Linux signal
 *              number, the last wire-format request the guest made — it is
 *              the only state this file owns.
 */

#ifdef DARLING_FREEBSD

#include "sigaction_translate.h"

#include <signal.h>
#include <sys/syscall.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <errno.h>

/* ── Linux wire-format struct sigaction (SPEC §1) ─────────────────────────
 *
 * Not available as a guest/kernel header from mldr's build (guest-side
 * headers under src/external/xnu are not part of mldr's compilation unit),
 * so this is a local, hand-verified copy — same rationale the sigaltstack
 * case already uses one file up in freebsd_syscall_trap.c for `struct
 * linux_sigaltstack`.
 *
 * Layout (x86-64, natural C alignment, alignof=8), from
 * src/external/xnu/darling/src/libsystem_kernel/emulation/include/
 * conversion/signal/sigaction.h:96-102 and duct_signals.h:53:
 *   offset 0  (8) sa_sigaction — handler fn ptr, or SIG_DFL(0)/SIG_IGN(1)/
 *                 SIG_ERR(-1)
 *   offset 8  (4) sa_flags
 *   offset 12 (4) compiler padding — NEVER read this as part of sa_flags
 *                 (see the risk note in the SPEC doc: sa_restorer is a
 *                 stack local on the guest side and this padding is never
 *                 memset, so it can carry stack garbage)
 *   offset 16 (8) sa_restorer — always guest's sig_restorer when handler
 *                 isn't DFL/IGN/ERR; FreeBSD has no matching field, kept
 *                 here only so it can be echoed back to a later oldact
 *                 query (see sat_registry below)
 *   offset 24 (8) sa_mask — linux_sigset_t, ONE 64-bit word (not the
 *                 64-word kernel sigset_t rt_sigprocmask uses)
 */
/* NOT sa_handler: FreeBSD's <signal.h> defines that name as a macro
 * (#define sa_handler __sigaction_u.__sa_handler), so using it as a member
 * name here does not declare a field — it expands mid-struct and the
 * declaration fails to parse. The native struct's own sa_handler below still
 * relies on that macro, so #undef is not an option; the wire struct's field
 * is renamed instead. */
struct linux_sigaction_wire {
    void    *lsa_handler;
    int32_t  sa_flags;
    void   (*sa_restorer)(void);
    uint64_t sa_mask;
};
_Static_assert(sizeof(struct linux_sigaction_wire) == 32,
               "linux_sigaction_wire layout drift — see SPEC §1");

/* Linux SIG_DFL/SIG_IGN/SIG_ERR numerically coincide with FreeBSD/POSIX's
 * (0/1/-1 on both — SPEC §7 step 4), so these are passed through as raw
 * pointer values without translation; named here only so the "is this a
 * real handler" checks below are self-documenting. */
#define SAT_LINUX_SIG_DFL ((void *)0)
#define SAT_LINUX_SIG_IGN ((void *)1)
#define SAT_LINUX_SIG_ERR ((void *)-1)

/* ── Linux sa_flags bit values (SPEC §3.1) ────────────────────────────────
 * Prefixed SAT_ to avoid collision with freebsd_syscall_trap.c's own
 * LINUX_SA_* (if any) — this is a separate translation unit and must not
 * assume symbols from that file. */
#define SAT_LINUX_SA_NOCLDSTOP 0x00000001
#define SAT_LINUX_SA_NOCLDWAIT 0x00000002
#define SAT_LINUX_SA_SIGINFO   0x00000004
#define SAT_LINUX_SA_ONSTACK   0x08000000
#define SAT_LINUX_SA_RESTART   0x10000000
#define SAT_LINUX_SA_NODEFER   0x40000000
#define SAT_LINUX_SA_RESETHAND 0x80000000
#define SAT_LINUX_SA_RESTORER  0x04000000 /* no FreeBSD analog — dropped, SPEC §3.2 */

/* ── raw FreeBSD syscall helper ────────────────────────────────────────────
 * Byte-for-byte the same as freebsd_raw_syscall() in freebsd_syscall_trap.c
 * (kept as a private copy here since that one is `static` to its own
 * translation unit — same pattern already used for psp_raw_syscall() in
 * process_spawn.c). Same calling convention: >=0 success, <0 = -errno. */
static long
sat_raw_syscall(long nr, long a1, long a2, long a3, long a4, long a5, long a6)
{
    long ret;
#if defined(__x86_64__)
    register long r10 __asm__("r10") = a4;
    register long r8  __asm__("r8")  = a5;
    register long r9  __asm__("r9")  = a6;
    __asm__ volatile (
        "syscall\n\t"
        "jnc 1f\n\t"
        "neg %0\n\t"
        "1:"
        : "=a"(ret)
        : "0"(nr), "D"(a1), "S"(a2), "d"(a3), "r"(r10), "r"(r8), "r"(r9)
        : "rcx", "r11", "memory"
    );
#elif defined(__aarch64__)
    register long _nr  __asm__("x8")  = nr;
    register long _a1  __asm__("x0")  = a1;
    register long _a2  __asm__("x1")  = a2;
    register long _a3  __asm__("x2")  = a3;
    register long _a4  __asm__("x3")  = a4;
    register long _a5  __asm__("x4")  = a5;
    register long _a6  __asm__("x5")  = a6;
    __asm__ volatile (
        "svc #0\n\t"
        "b.cc 1f\n\t"
        "neg %0, %0\n\t"
        "1:"
        : "=r"(_a1)
        : "r"(_nr), "0"(_a1), "r"(_a2), "r"(_a3), "r"(_a4), "r"(_a5), "r"(_a6)
        : "memory"
    );
    ret = _a1;
#else
    #error sigaction_translate.c: unsupported architecture
#endif
    return ret;
}

/*
 * purpose:  Map a Linux signal number to its FreeBSD equivalent.
 *
 *           Private copy of mldr_linux_signo_to_freebsd()
 *           (freebsd_syscall_trap.c:353-374) — that one is `static` to its
 *           own translation unit, see the "Ownership" note in
 *           sigaction_translate.h for why this is duplicated rather than
 *           shared, and the drift risk that implies.
 *
 * input:    linux_signo — 1..31 (this file only ever calls it in that
 *           range; SPEC §0 notes the wire sa_mask here is the 8-byte
 *           linux_sigset_t, not the 64-word kernel one rt_sigprocmask
 *           uses, so signals above 31 cannot appear in it at all).
 * output:   the FreeBSD signal number, or 0 when there is no counterpart
 *           (Linux SIGSTKFLT=16 and SIGPWR=30 have none).
 * sideEffects: none.
 */
static int
sat_linux_signo_to_freebsd(int linux_signo)
{
    static const unsigned char map[32] = {
        [1]  = SIGHUP,  [2]  = SIGINT,  [3]  = SIGQUIT, [4]  = SIGILL,
        [5]  = SIGTRAP, [6]  = SIGABRT, [7]  = SIGBUS,  [8]  = SIGFPE,
        [9]  = SIGKILL, [10] = SIGUSR1, [11] = SIGSEGV, [12] = SIGUSR2,
        [13] = SIGPIPE, [14] = SIGALRM, [15] = SIGTERM,
        [16] = 0,            /* SIGSTKFLT — no FreeBSD counterpart */
        [17] = SIGCHLD, [18] = SIGCONT, [19] = SIGSTOP, [20] = SIGTSTP,
        [21] = SIGTTIN, [22] = SIGTTOU, [23] = SIGURG,  [24] = SIGXCPU,
        [25] = SIGXFSZ, [26] = SIGVTALRM, [27] = SIGPROF, [28] = SIGWINCH,
        [29] = SIGIO,
        [30] = 0,            /* SIGPWR — no FreeBSD counterpart */
        [31] = SIGSYS,
    };

    if (linux_signo < 1 || linux_signo >= (int)(sizeof(map) / sizeof(map[0])))
        return 0;

    return (int)map[linux_signo];
}

/*
 * purpose:  Answer whether a FreeBSD signal number is one mldr's own
 *           setup_macos_syscall_trap() has already claimed
 *           (freebsd_syscall_trap.c:2901-2962), and therefore one a guest
 *           rt_sigaction() call must NEVER be allowed to actually install
 *           a handler for. See SPEC §5.
 * input:    native_signo — a FreeBSD signal number (post-translation).
 * output:   true for SIGILL/SIGBUS/SIGSEGV/SIGSYS, false otherwise.
 * sideEffects: none.
 */
static bool
sat_is_reserved_freebsd_signo(int native_signo)
{
    return native_signo == SIGILL || native_signo == SIGBUS ||
           native_signo == SIGSEGV || native_signo == SIGSYS;
}

/*
 * purpose:  Linux sa_flags -> FreeBSD sa_flags, per SPEC §3.1/§3.3.
 * input:    linux_flags — raw wire sa_flags (already read as int32_t only —
 *           see the padding warning on struct linux_sigaction_wire).
 *           handler — the (untranslated, passed-through) sa_handler value,
 *           needed only to decide whether SA_SIGINFO must be forced on
 *           (SPEC §3.3: FreeBSD SA_SIGINFO is set unconditionally whenever
 *           the handler isn't DFL/IGN/ERR, independent of what the guest's
 *           own SA_SIGINFO bit said — the guest always sets it anyway, see
 *           SPEC §3.1's note that sys_sigaction() hardcodes it).
 * output:   FreeBSD sa_flags bitmask. LINUX_SA_RESTORER is silently
 *           dropped (no FreeBSD analog — SPEC §3.2, and the most likely
 *           cause of today's EINVAL per SPEC's open-questions #1).
 * sideEffects: none.
 */
static int
sat_flags_linux_to_native(int32_t linux_flags, void *handler)
{
    int native_flags = 0;

    if (linux_flags & SAT_LINUX_SA_NOCLDSTOP) native_flags |= SA_NOCLDSTOP;
    if (linux_flags & SAT_LINUX_SA_NOCLDWAIT) native_flags |= SA_NOCLDWAIT;
    if (linux_flags & SAT_LINUX_SA_ONSTACK)   native_flags |= SA_ONSTACK;
    if (linux_flags & SAT_LINUX_SA_RESTART)   native_flags |= SA_RESTART;
    if (linux_flags & SAT_LINUX_SA_NODEFER)   native_flags |= SA_NODEFER;
    if (linux_flags & SAT_LINUX_SA_RESETHAND) native_flags |= SA_RESETHAND;
    /* SAT_LINUX_SA_RESTORER: intentionally never translated — see above. */

    if (handler != SAT_LINUX_SIG_DFL && handler != SAT_LINUX_SIG_IGN &&
        handler != SAT_LINUX_SIG_ERR) {
        native_flags |= SA_SIGINFO;
    }

    return native_flags;
}

/*
 * purpose:  FreeBSD sa_flags -> Linux sa_flags, the reverse of
 *           sat_flags_linux_to_native(), for filling in `oldact`.
 * input:    native_flags — sa_flags as FreeBSD's sigaction(2) reported it.
 * output:   Linux sa_flags bitmask. Never sets LINUX_SA_RESTORER — FreeBSD
 *           SA_SIGINFO does not imply a guest ever asked for a restorer,
 *           and fabricating that bit on a read-back is not something any
 *           caller needs (see sat_registry for the one place sa_restorer
 *           itself, not this flag, is actually echoed).
 * sideEffects: none.
 */
static int32_t
sat_flags_native_to_linux(int native_flags)
{
    int32_t linux_flags = 0;

    if (native_flags & SA_NOCLDSTOP) linux_flags |= SAT_LINUX_SA_NOCLDSTOP;
    if (native_flags & SA_NOCLDWAIT) linux_flags |= SAT_LINUX_SA_NOCLDWAIT;
    if (native_flags & SA_SIGINFO)   linux_flags |= SAT_LINUX_SA_SIGINFO;
    if (native_flags & SA_ONSTACK)   linux_flags |= SAT_LINUX_SA_ONSTACK;
    if (native_flags & SA_RESTART)   linux_flags |= SAT_LINUX_SA_RESTART;
    if (native_flags & SA_NODEFER)   linux_flags |= SAT_LINUX_SA_NODEFER;
    if (native_flags & SA_RESETHAND) linux_flags |= SAT_LINUX_SA_RESETHAND;

    return linux_flags;
}

/* ── per-signal "what did the guest last ask for" registry (SPEC §7 step 5)
 *
 * FreeBSD's sigaction(2) has no sa_restorer field at all, so there is
 * nothing in a kernel oldact query to read one back from. mldr keeps its
 * own copy of the guest's last wire-format request per Linux signal
 * number, purely so a later rt_sigaction(signo, NULL, &oldact, ...) query
 * can hand back the sa_restorer (and, as a cross-check, sa_handler/flags)
 * the guest itself last installed — mirroring the guest-side
 * sig_handlers[]/sig_flags[]/sig_masks[] bookkeeping in sigaction.c:25-30
 * that already exists for the same reason on the OTHER side of this
 * boundary.
 *
 * Indexed 1..31 (index 0 unused, same convention as
 * sat_linux_signo_to_freebsd()'s map). NOT lock-protected — see the
 * "Threading" note in sigaction_translate.h for why that is an accepted,
 * documented gap rather than an oversight.
 */
struct sat_registry_entry {
    struct linux_sigaction_wire wire;
    bool valid;
};
static struct sat_registry_entry sat_registry[32];

long
mldr_sys_rt_sigaction(int linux_signo, const void *act, void *oldact,
                       size_t sigsetsize)
{
    /* Linux's own rt_sigaction entry point rejects any sigsetsize other
     * than sizeof(linux kernel-visible sigset_t) defensively against ABI
     * skew between caller and kernel; this bridge holds the wire struct
     * to the SAME fixed 8-byte linux_sigset_t this whole file assumes
     * (SPEC §0/§1), so a mismatch here means the caller and this file
     * disagree about the wire layout and must not proceed. */
    if (sigsetsize != sizeof(uint64_t))
        return -EINVAL;

    /* sat_linux_signo_to_freebsd()'s map[32] only covers indices 1..31;
     * anything outside that (including realtime signals, which this
     * 8-byte-sigset ABI cannot represent past bit 63 anyway) is rejected
     * up front rather than silently masked into range. */
    if (linux_signo < 1 || linux_signo > 31)
        return -EINVAL;

    const struct linux_sigaction_wire *wire_act =
        (const struct linux_sigaction_wire *)act;
    struct linux_sigaction_wire *wire_old =
        (struct linux_sigaction_wire *)oldact;

    int native_signo = sat_linux_signo_to_freebsd(linux_signo);

    if (native_signo == 0) {
        /* SIGSTKFLT/SIGPWR: no FreeBSD counterpart at all. Quiet success,
         * zeroed oldact — SPEC §7 step 2, matching the guest-side
         * precedent in sigaction.c:38-50 (signum_bsd_to_linux()==0 case)
         * so code that probes every signal number in sequence does not
         * die on the first gap in the 1:1 mapping. */
        if (wire_old != NULL)
            memset(wire_old, 0, sizeof(*wire_old));
        return 0;
    }

    if (sat_is_reserved_freebsd_signo(native_signo)) {
        /* SIGILL/SIGBUS/SIGSEGV/SIGSYS carry mldr's own syscall-trap
         * machinery (sigill_handler/sigsys_handler/crash_debug_handler).
         * A real sigaction(2) here would tear that mechanism down from
         * inside a call reached THROUGH it. Track the guest's request in
         * the registry only, and answer oldact from there — never from
         * the kernel, which always shows mldr's own handler for these
         * four. SPEC §5. This is the one deliberate "success without
         * doing the real thing" branch in this file, and it is mandatory
         * (not a placeholder) for the reason above. */
        struct sat_registry_entry *ent = &sat_registry[linux_signo];

        if (wire_old != NULL) {
            if (ent->valid)
                *wire_old = ent->wire;
            else
                memset(wire_old, 0, sizeof(*wire_old));
        }
        if (wire_act != NULL) {
            ent->wire = *wire_act;
            ent->valid = true;
        }
        return 0;
    }

    struct sigaction native_new, native_old;
    struct sigaction *pnew = NULL, *pold = (wire_old != NULL) ? &native_old : NULL;

    if (wire_act != NULL) {
        memset(&native_new, 0, sizeof(native_new));
        /* sa_handler/sa_sigaction share one union slot on FreeBSD; the
         * guest handler is passed through untranslated (SPEC §7 step 4 —
         * either a real trampoline pointer, expected to be called
         * three-arg since we force SA_SIGINFO below, or one of
         * SIG_DFL/IGN/ERR, numerically identical on both ABIs). */
        native_new.sa_handler = (void (*)(int))wire_act->lsa_handler;
        native_new.sa_flags = sat_flags_linux_to_native(wire_act->sa_flags,
                                                          wire_act->lsa_handler);

        sigemptyset(&native_new.sa_mask);
        /* wire_act->sa_mask is the 8-byte linux_sigset_t (SPEC §1) — bit
         * i-1 is Linux signal i, valid for i in 1..64, but only 1..31 can
         * ever be set here since that's all this 8-byte mask can encode
         * that also has any translation target below. */
        for (int ls = 1; ls <= 31; ls++) {
            if ((wire_act->sa_mask & (1ULL << (ls - 1))) == 0)
                continue;
            int ns = sat_linux_signo_to_freebsd(ls);
            if (ns > 0)
                sigaddset(&native_new.sa_mask, ns);
        }
        /* Same rationale as LINUX_SYS_rt_sigprocmask's existing handling
         * (freebsd_syscall_trap.c): SIGILL/SIGSYS are how every guest
         * syscall reaches mldr at all. A handler mask that blocks them
         * would disarm that mechanism for the duration of THIS handler's
         * execution — never honor a guest request to block either. */
        sigdelset(&native_new.sa_mask, SIGILL);
        sigdelset(&native_new.sa_mask, SIGSYS);

        pnew = &native_new;
    }

    long r = sat_raw_syscall(SYS_sigaction, native_signo, (long)pnew,
                              (long)pold, 0, 0, 0);
    if (r < 0)
        return r;

    if (wire_old != NULL) {
        wire_old->lsa_handler = (void *)native_old.sa_handler;
        wire_old->sa_flags = sat_flags_native_to_linux(native_old.sa_flags);

        wire_old->sa_mask = 0;
        for (int ls = 1; ls <= 31; ls++) {
            int ns = sat_linux_signo_to_freebsd(ls);
            if (ns > 0 && sigismember(&native_old.sa_mask, ns))
                wire_old->sa_mask |= (1ULL << (ls - 1));
        }

        /* FreeBSD's sigaction(2) has no sa_restorer field to read this
         * back from at all (SPEC §7 step 5) — the only source of truth is
         * this file's own registry of what the guest last sent for this
         * signal. If the guest never previously set a handler through
         * this path (e.g. it's asking for the still-default
         * disposition), there is nothing to echo and this is left NULL —
         * a guest that then tries to use that as a jump target would be
         * relying on undefined behavior already (default disposition has
         * no restorer to speak of on the Linux side either). */
        struct sat_registry_entry *ent = &sat_registry[linux_signo];
        wire_old->sa_restorer = ent->valid ? ent->wire.sa_restorer : NULL;
    }

    if (wire_act != NULL) {
        sat_registry[linux_signo].wire = *wire_act;
        sat_registry[linux_signo].valid = true;
    }

    return 0;
}

#endif /* DARLING_FREEBSD */
