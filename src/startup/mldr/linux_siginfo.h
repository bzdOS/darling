/*
 * linux_siginfo.h — FreeBSD-native siginfo_t -> Linux-ABI siginfo_t converter.
 *
 * purpose:    mldr runs raw-Linux-ABI code (the upstream dyld/libdyld.dylib
 *             overlay — see the "Third class" comment block in
 *             freebsd_syscall_trap.c) directly on FreeBSD. When the FreeBSD
 *             kernel delivers a signal (SIGSEGV, SIGBUS, SIGILL, SIGFPE, ...)
 *             to that Linux-ABI code, the kernel fills in a native FreeBSD
 *             `siginfo_t`. A Linux-ABI `rt_sigaction` handler installed by
 *             that guest code expects the Linux kernel's `siginfo_t` layout
 *             instead — different field order, different union tag values,
 *             different SI_* sender codes. Without translation the guest
 *             handler misreads si_code / si_addr / si_pid and either crashes
 *             harder or takes the wrong recovery path.
 *
 * Authoritative Linux layout: this struct is modeled on the real Linux
 * kernel/glibc `siginfo_t` union, cross-checked against two independent
 * sources so the field layout isn't reconstructed from memory:
 *   - src/external/xnu/darling/src/libsystem_kernel/emulation/include/
 *     xnu_syscall/bsd/impl/process/waitid.h:19-82 (Darling's own
 *     `linux_siginfo_t`, used elsewhere in this tree to receive a REAL
 *     Linux-kernel-filled siginfo_t from a `waitid` syscall — i.e. it is
 *     already verified against the actual Linux ABI, not aspirational).
 *   - /usr/include/x86_64-linux-gnu/bits/types/siginfo_t.h (this build
 *     host's own glibc headers — this host runs Linux, confirmed against
 *     waitid.h field-for-field: si_signo/si_errno/si_code order, the
 *     _kill/_timer/_rt/_sigchld/_sigfault/_sigpoll/_sigsys union, and the
 *     SI_USER/SI_QUEUE/SI_TIMER/... and SEGV_*/ILL_*/FPE_*/BUS_*/CLD_*
 *     enum values in bits/siginfo-consts.h).
 * Deliberately NOT modeled on
 * src/external/xnu/darling/.../conversion/signal/sigaction.h:46-66
 * (`struct linux_siginfo`) — that struct only carries the fields the BSD
 * union at sigaction.h:25-37 (`struct bsd_siginfo`) has room for, no
 * si_status/si_band/si_tid/si_overrun, so it would drop information a
 * guest SIGCHLD/SIGPOLL/POSIX-timer handler needs.
 *
 * We intentionally omit the glibc SEGV_BNDERR / SEGV_PKUERR extra fields
 * (MPX/PKU bounds-fault payloads in _sigfault._addr_bnd / _addr_pkey) —
 * FreeBSD has no equivalent hardware-fault data to source them from, and
 * mldr does not emulate MPX/PKU. si_code is never set to those two values
 * by this converter (see mldr_siginfo_freebsd_to_linux()).
 *
 * FreeBSD side: `siginfo_t` field names (si_signo, si_errno, si_code,
 * si_pid, si_uid, si_status, si_addr, si_value, and the `_reason` union)
 * are taken from the FreeBSD 15 <sys/signal.h> ABI as the author recalls
 * it — THIS BUILD HOST IS LINUX, so there is no local FreeBSD header to
 * check them against. Every FreeBSD-side field read in linux_siginfo.c is
 * flagged there as unverified; confirm against the real header when
 * building on the FreeBSD dev VM (185).
 *
 * sideEffects: none (pure header, no state).
 */

#pragma once

#ifdef DARLING_FREEBSD

#include <signal.h>   /* siginfo_t (FreeBSD-native, from the build's libc) */
#include <sys/types.h> /* pid_t, uid_t */

/*
 * Linux si_code space is split by kernel convention:
 *   > 0  : sub-code for a hardware-generated signal (SIGSEGV/SIGILL/SIGFPE/
 *          SIGBUS/SIGTRAP/SIGCHLD/SIGPOLL), meaning depends on si_signo.
 *   == 0 : SI_USER (kill(2)/raise(2)).
 *   < 0  : SI_QUEUE/SI_TIMER/SI_MESGQ/SI_ASYNCIO/SI_SIGIO/SI_TKILL — kernel-
 *          or library-generated, sigqueue(2)/POSIX timers/AIO/etc.
 *   0x80 : SI_KERNEL (Linux-internal use only; a real signal delivered to
 *          guest code should never carry this, but we still translate it
 *          defensively instead of leaving si_code unmapped).
 * Values below match /usr/include/asm-generic/siginfo.h and
 * /usr/include/bits/siginfo-consts.h on this (Linux) build host.
 */
#define MLDR_LINUX_SI_USER     0
#define MLDR_LINUX_SI_KERNEL   0x80
#define MLDR_LINUX_SI_QUEUE    (-1)
#define MLDR_LINUX_SI_TIMER    (-2)
#define MLDR_LINUX_SI_MESGQ    (-3)
#define MLDR_LINUX_SI_ASYNCIO  (-4)
#define MLDR_LINUX_SI_SIGIO    (-5)
#define MLDR_LINUX_SI_TKILL    (-6)

/* SIGILL si_code (ILL_*) — POSIX.1b-standardized values, identical on
 * Linux and FreeBSD (both derive them from the same POSIX RT signals
 * extension numbering), so no translation table is needed for these —
 * si_code is passed through unchanged for SIGILL. Listed here only for
 * documentation / future-proofing if that assumption ever needs revisiting. */
/* ILL_ILLOPC=1 ILL_ILLOPN=2 ILL_ILLADR=3 ILL_ILLTRP=4 ILL_PRVOPC=5
 * ILL_PRVREG=6 ILL_COPROC=7 ILL_BADSTK=8 */

/* SIGFPE (FPE_*), SIGSEGV (SEGV_*), SIGBUS (BUS_*), SIGCHLD (CLD_*),
 * SIGTRAP (TRAP_*): same story as ILL_* above — POSIX-standardized integer
 * values, passed through unchanged. See mldr_siginfo_freebsd_to_linux()
 * for the one place this assumption is called out again next to the code
 * that relies on it. */

/*
 * purpose:    Linux-kernel-ABI-compatible siginfo_t as delivered by a real
 *             Linux kernel's rt_sigaction() to a userspace handler
 *             (x86_64 / aarch64, both LP64 — this struct does NOT cover
 *             32-bit Linux ABIs, mldr does not run 32-bit guest code).
 * Layout source: darling's own `linux_siginfo_t` at
 *   src/external/xnu/darling/src/libsystem_kernel/emulation/include/
 *   xnu_syscall/bsd/impl/process/waitid.h:19-82
 * (that struct is filled directly by a real `waitid` Linux syscall in
 * waitid.c:21, so its layout is exercised, not just declared), field names
 * renamed with an `mldr_` prefix on the accessor macros below to avoid
 * colliding with any Linux guest header we do NOT include here (we must
 * not pull in real linux/signal.h — that would fight the FreeBSD libc
 * signal.h already included above).
 */
struct linux_siginfo_compat {
	int si_signo;   /* Linux signal number (already translated by the
	                 * caller — see mldr_siginfo_freebsd_to_linux()'s
	                 * `linux_signo` parameter; this function does not
	                 * touch signal-number translation). */
	int si_errno;   /* glibc/kernel order is signo, errno, code (see
	                 * LINUX_SI_ERRNO_THEN_CODE in waitid.h:14-16 — true
	                 * for every architecture mldr targets). */
	int si_code;
#if defined(__LP64__) || defined(__x86_64__) || defined(__aarch64__)
	int __pad0;     /* explicit padding before the union on 64-bit,
	                  * matching waitid.h:30-32 (`__WORDSIZE == 64`). */
#endif
	union {
		/* kill(2) / SI_USER, SI_QUEUE (without an explicit sigval),
		 * SI_TKILL. */
		struct {
			pid_t si_pid;
			uid_t si_uid;
		} _kill;

		/* POSIX.1b timers (SI_TIMER). */
		struct {
			int si_tid;
			int si_overrun;
			union sigval si_sigval;
		} _timer;

		/* POSIX.1b queued signals (sigqueue(2), SI_QUEUE with a
		 * payload; also covers plain SI_ASYNCIO/SI_MESGQ senders
		 * that carry a sigval). */
		struct {
			pid_t si_pid;
			uid_t si_uid;
			union sigval si_sigval;
		} _rt;

		/* SIGCHLD. */
		struct {
			pid_t si_pid;
			uid_t si_uid;
			int si_status;
		} _sigchld;

		/* SIGILL, SIGFPE, SIGSEGV, SIGBUS. */
		struct {
			void *si_addr;
		} _sigfault;

		/* SIGPOLL/SIGIO. mldr does not currently deliver SIGPOLL to
		 * guest code, but the field is included for completeness and
		 * zero-filled defensively (see .c). */
		struct {
			long si_band;
			int si_fd;
		} _sigpoll;
	} _sifields;
};

/* Convenience accessors mirroring waitid.h:85-90, scoped to this
 * translation unit's struct name to avoid clashing with any other
 * `si_*` macros in this file's includers. */
#define mldr_lsi_pid      _sifields._kill.si_pid
#define mldr_lsi_uid      _sifields._kill.si_uid
#define mldr_lsi_status   _sifields._sigchld.si_status
#define mldr_lsi_value    _sifields._rt.si_sigval
#define mldr_lsi_addr     _sifields._sigfault.si_addr
#define mldr_lsi_band     _sifields._sigpoll.si_band

/*
 * purpose:     Translate a FreeBSD-native siginfo_t (as the FreeBSD kernel
 *              hands it to a SA_SIGINFO handler) into the Linux-ABI layout
 *              a Linux-ABI guest rt_sigaction handler expects.
 * input:       native      — FreeBSD siginfo_t from the FreeBSD signal
 *                             delivery (must not be NULL).
 *              linux_signo — the Linux signal number to stamp into
 *                             out->si_signo. Signal-NUMBER translation
 *                             (FreeBSD SIGSEGV == Linux SIGSEGV numerically
 *                             for the fault signals mldr cares about, but
 *                             this is not true in general — e.g. SIGBUS/
 *                             real-time signal numbering diverges) is the
 *                             caller's responsibility; this function only
 *                             reshapes the payload.
 *              out         — destination buffer, caller-owned, must not be
 *                             NULL, no alignment requirement beyond
 *                             `struct linux_siginfo_compat`'s own.
 * output:      none (writes through `out`).
 * sideEffects: none — no allocation, no global state, stack-only. Safe to
 *              call from a signal handler (see freebsd_syscall_trap.c's
 *              SA_ONSTACK signal handlers, which are mldr's caller for
 *              this function).
 */
void mldr_siginfo_freebsd_to_linux(const siginfo_t *native, int linux_signo,
    struct linux_siginfo_compat *out);

#endif /* DARLING_FREEBSD */
