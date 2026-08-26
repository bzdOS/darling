/*
 * linux_siginfo.c — FreeBSD siginfo_t -> Linux-ABI siginfo_t conversion.
 *
 * purpose:    Implementation of mldr_siginfo_freebsd_to_linux(). See
 *             linux_siginfo.h for the full rationale and the two sources
 *             the Linux-side layout was cross-checked against.
 * sideEffects: none (see per-function contract below).
 */

#ifdef DARLING_FREEBSD

#include "linux_siginfo.h"

#include <string.h> /* memset */

/*
 * FreeBSD si_code values for kernel/library-generated (non-hardware-fault)
 * signals, i.e. the FreeBSD equivalents of Linux's SI_USER/SI_QUEUE/...
 *
 * UNVERIFIED — this build host is Linux, there is no FreeBSD <sys/signal.h>
 * available here to check these against. Taken from the author's
 * recollection of the FreeBSD 15 ABI (historically stable since 4.x):
 * FreeBSD groups these codes as 0x10001.. so they can never collide with
 * the small positive hardware-fault subcodes (SEGV_MAPERR=1, ILL_ILLOPC=1,
 * etc., which are POSIX-standardized and shared with Linux — see
 * linux_siginfo.h). CONFIRM THIS BLOCK against the real
 * /usr/include/sys/signal.h on the FreeBSD dev VM (185) before trusting it
 * in production; if wrong, the practical effect is limited to a queued/
 * user-sent signal's si_code being misreported to the Linux-ABI guest
 * handler as 0 (SI_USER) via the fallback below — not a crash, but a
 * misleading signal origin.
 */
#define MLDR_FREEBSD_SI_USER    0x10001
#define MLDR_FREEBSD_SI_QUEUE   0x10002
#define MLDR_FREEBSD_SI_TIMER   0x10003
#define MLDR_FREEBSD_SI_ASYNCIO 0x10004
#define MLDR_FREEBSD_SI_MESGQ   0x10005

/*
 * purpose:     Map a FreeBSD sender-identification si_code (SI_USER/
 *              SI_QUEUE/SI_TIMER/SI_ASYNCIO/SI_MESGQ) to its Linux
 *              equivalent. Hardware-fault subcodes (SEGV_*, ILL_*, FPE_*,
 *              BUS_*, CLD_*, TRAP_*) are POSIX-standardized small positive
 *              integers shared verbatim between FreeBSD and Linux (see
 *              linux_siginfo.h's documentation block), so this function is
 *              only consulted for the sender-identification codes; the
 *              caller passes those subcodes straight through instead of
 *              calling this.
 * input:       freebsd_code — native->si_code from the FreeBSD siginfo_t.
 * output:      the matching MLDR_LINUX_SI_* value, or MLDR_LINUX_SI_USER
 *              (0) as a safe default when freebsd_code isn't one of the
 *              five known FreeBSD sender codes above (e.g. because the
 *              UNVERIFIED constants above turn out to be wrong, or because
 *              this is actually a hardware-fault subcode being asked about
 *              by mistake — SI_USER is the least surprising fallback since
 *              it carries no fabricated pid/uid/addr semantics beyond
 *              zero-filled fields).
 * sideEffects: none.
 */
static int
mldr_map_si_code_sender(int freebsd_code)
{
	switch (freebsd_code) {
	case MLDR_FREEBSD_SI_USER:
		return MLDR_LINUX_SI_USER;
	case MLDR_FREEBSD_SI_QUEUE:
		return MLDR_LINUX_SI_QUEUE;
	case MLDR_FREEBSD_SI_TIMER:
		return MLDR_LINUX_SI_TIMER;
	case MLDR_FREEBSD_SI_ASYNCIO:
		return MLDR_LINUX_SI_ASYNCIO;
	case MLDR_FREEBSD_SI_MESGQ:
		return MLDR_LINUX_SI_MESGQ;
	default:
		return MLDR_LINUX_SI_USER;
	}
}

/*
 * purpose:     Decide whether `code` is one of the sender-identification
 *              codes handled by mldr_map_si_code_sender() rather than a
 *              signal-specific hardware-fault subcode.
 * input:       code — native->si_code.
 * output:      nonzero if `code` matches one of the FreeBSD SI_* sender
 *              constants above.
 * sideEffects: none.
 */
static int
mldr_si_code_is_sender(int code)
{
	return code == MLDR_FREEBSD_SI_USER || code == MLDR_FREEBSD_SI_QUEUE ||
	    code == MLDR_FREEBSD_SI_TIMER || code == MLDR_FREEBSD_SI_ASYNCIO ||
	    code == MLDR_FREEBSD_SI_MESGQ;
}

void
mldr_siginfo_freebsd_to_linux(const siginfo_t *native, int linux_signo,
    struct linux_siginfo_compat *out)
{
	int code_is_sender;

	memset(out, 0, sizeof(*out));

	if (native == NULL || out == NULL)
		return; /* defensive: mirrors the NULL-info path in
		         * handler_linux_to_bsd() (sigaction.c:24), which
		         * likewise treats a missing siginfo as "leave the
		         * zeroed struct as-is" rather than faulting. */

	out->si_signo = linux_signo; /* caller-supplied — see contract in
	                               * linux_siginfo.h; we do not attempt
	                               * FreeBSD->Linux signal NUMBER
	                               * translation here. */

	/*
	 * si_errno: FreeBSD and Linux share POSIX errno numbering for the
	 * common values (EINTR, EAGAIN, ...), so a straight copy is correct
	 * for the errno values si_errno is realistically ever set to inside
	 * a synchronous signal (it's almost always 0 — POSIX signal
	 * delivery rarely populates this field at all). NOT verified against
	 * a full FreeBSD errno.h diff; if mldr ever surfaces a signal whose
	 * si_errno carries an FreeBSD-only errno value, this needs a real
	 * errno_bsd_to_linux() table (one already exists in the Darling tree
	 * at .../conversion/errno.h — reuse it if this ever bites).
	 */
	out->si_errno = native->si_errno;

	/*
	 * si_code: two cases.
	 *   1. Sender-identification (SI_USER/SI_QUEUE/SI_TIMER/...) — OS-
	 *      specific integer space, needs the lookup table above.
	 *   2. Hardware-fault subcode (SEGV_MAPERR, ILL_ILLOPC, FPE_INTDIV,
	 *      BUS_ADRALN, CLD_EXITED, TRAP_BRKPT, ...) — POSIX.1b-
	 *      standardized small positive integers, IDENTICAL on FreeBSD
	 *      and Linux (confirmed for the Linux side against this host's
	 *      own /usr/include/bits/siginfo-consts.h; the FreeBSD side is
	 *      UNVERIFIED here but these values have been stable POSIX RT
	 *      signal-extension constants across BSD and Linux for decades,
	 *      so pass-through is the correct default even if not confirmed
	 *      against a FreeBSD header on this host).
	 */
	code_is_sender = mldr_si_code_is_sender(native->si_code);
	if (code_is_sender)
		out->si_code = mldr_map_si_code_sender(native->si_code);
	else
		out->si_code = native->si_code; /* hardware-fault subcode,
		                                  * pass through unchanged */

	/*
	 * Union payload: which member is live depends on linux_signo, per
	 * the Linux kernel's own dispatch (see waitid.h:84-90's comment
	 * "We keep only the fields defined in the BSD version" — same idea,
	 * reversed direction). SIGKILL/SIGSTOP/etc. that carry no meaningful
	 * payload fall through to the _kill member (harmless: it's already
	 * zeroed by memset above if native has nothing to offer).
	 */
	switch (linux_signo) {
	case SIGCHLD:
		/* FreeBSD siginfo_t carries si_pid/si_uid/si_status as flat
		 * fields (unlike Linux's per-signal union) — see the field
		 * list in the task description / linux_siginfo.h's FreeBSD-
		 * side note. UNVERIFIED against a FreeBSD header on this
		 * host, but these three field NAMES are long-standing FreeBSD
		 * ABI and match POSIX siginfo_t. */
		out->_sifields._sigchld.si_pid = native->si_pid;
		out->_sifields._sigchld.si_uid = native->si_uid;
		out->_sifields._sigchld.si_status = native->si_status;
		break;

	case SIGSEGV:
	case SIGBUS:
	case SIGILL:
	case SIGFPE:
	case SIGTRAP:
		/* Hardware faults: only si_addr is meaningful (si_addr_lsb
		 * and the SEGV_BNDERR/SEGV_PKUERR extra payloads that real
		 * glibc siginfo_t also has are deliberately NOT modeled —
		 * see linux_siginfo.h's header comment). SIGTRAP has no
		 * si_addr on the Linux side in the general case (TRAP_BRKPT/
		 * TRAP_TRACE carry none), but zero-filling si_addr is
		 * harmless when the guest handler doesn't read it for those
		 * codes, and some SIGTRAP delivery paths (breakpoint traps
		 * exposed via ptrace-adjacent mechanisms) DO expect an
		 * address here, so we forward it rather than leave stale
		 * zero silently — cheap and not observably wrong either way.
		 */
		out->_sifields._sigfault.si_addr = native->si_addr;
		break;

	default:
		if (out->si_code == MLDR_LINUX_SI_TIMER) {
			/* POSIX timer expiration: Linux's _timer member has
			 * no FreeBSD-native equivalent for si_tid/si_overrun
			 * (mldr does not implement Linux POSIX timers on top
			 * of FreeBSD's itimer/kqueue timers as of this
			 * writing), so those stay 0 — only the sigval carries
			 * real information, and only if the caller populated
			 * native->si_value at all (most FreeBSD signal
			 * deliveries leave it zeroed too). */
			out->_sifields._timer.si_tid = 0;
			out->_sifields._timer.si_overrun = 0;
			out->_sifields._timer.si_sigval = native->si_value;
		} else if (code_is_sender &&
		    (native->si_value.sival_ptr != NULL ||
		    native->si_value.sival_int != 0)) {
			/* sigqueue(2)-style delivery with a real payload:
			 * Linux's _rt member (SI_QUEUE + sigval) is the
			 * closest match. */
			out->_sifields._rt.si_pid = native->si_pid;
			out->_sifields._rt.si_uid = native->si_uid;
			out->_sifields._rt.si_sigval = native->si_value;
		} else {
			/* Plain kill(2)/raise(2) (SI_USER) or anything else
			 * this switch doesn't special-case: _kill is the
			 * Linux kernel's own default for "just pid/uid, no
			 * extra payload". */
			out->_sifields._kill.si_pid = native->si_pid;
			out->_sifields._kill.si_uid = native->si_uid;
		}
		break;
	}
}

#endif /* DARLING_FREEBSD */
