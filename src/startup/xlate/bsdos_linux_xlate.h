/*
 * bsdos_linux_xlate.h - the single table that translates constants crossing the boundary between the
 * FreeBSD host (darlingserver, mldr) and the guest Linux-ABI syscall layer (numbered like Linux).
 *
 * purpose:  Everything the host hands to the guest, or the guest hands to the host, in a Linux-numbered
 *           word goes through here: errno values, signal numbers, mmap and msync flags. Header-only so
 *           mldr (C) and darlingserver (C++) share one definition; FreeBSD hosts only.
 * contract: "guest" values use Linux numbering, "host" values are the FreeBSD constants of the including
 *           translation unit. A value with no counterpart maps to the documented fallback.
 */
#ifndef BSDOS_LINUX_XLATE_H
#define BSDOS_LINUX_XLATE_H

#include <errno.h>
#include <signal.h>
#include <sys/mman.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- errno ------------------------------------------------------------------------------------ */

struct bsdos_errno_pair { int host; int guest; };

/* host FreeBSD errno -> Linux errno. Codes equal in both spaces are listed too, so a miss means
 * "no counterpart". */
static const struct bsdos_errno_pair bsdos_errno_table[] = {
	{ EPERM, 1 }, { ENOENT, 2 }, { ESRCH, 3 }, { EINTR, 4 }, { EIO, 5 }, { ENXIO, 6 }, { E2BIG, 7 },
	{ ENOEXEC, 8 }, { EBADF, 9 }, { ECHILD, 10 }, { EDEADLK, 35 }, { ENOMEM, 12 }, { EACCES, 13 },
	{ EFAULT, 14 }, { ENOTBLK, 15 }, { EBUSY, 16 }, { EEXIST, 17 }, { EXDEV, 18 }, { ENODEV, 19 },
	{ ENOTDIR, 20 }, { EISDIR, 21 }, { EINVAL, 22 }, { ENFILE, 23 }, { EMFILE, 24 }, { ENOTTY, 25 },
	{ ETXTBSY, 26 }, { EFBIG, 27 }, { ENOSPC, 28 }, { ESPIPE, 29 }, { EROFS, 30 }, { EMLINK, 31 },
	{ EPIPE, 32 }, { EDOM, 33 }, { ERANGE, 34 }, { EAGAIN, 11 }, { EINPROGRESS, 115 }, { EALREADY, 114 },
	{ ENOTSOCK, 88 }, { EDESTADDRREQ, 89 }, { EMSGSIZE, 90 }, { EPROTOTYPE, 91 }, { ENOPROTOOPT, 92 },
	{ EPROTONOSUPPORT, 93 }, { ESOCKTNOSUPPORT, 94 }, { EOPNOTSUPP, 95 }, { EPFNOSUPPORT, 96 },
	{ EAFNOSUPPORT, 97 }, { EADDRINUSE, 98 }, { EADDRNOTAVAIL, 99 }, { ENETDOWN, 100 },
	{ ENETUNREACH, 101 }, { ENETRESET, 102 }, { ECONNABORTED, 103 }, { ECONNRESET, 104 },
	{ ENOBUFS, 105 }, { EISCONN, 106 }, { ENOTCONN, 107 }, { ESHUTDOWN, 108 }, { ETOOMANYREFS, 109 },
	{ ETIMEDOUT, 110 }, { ECONNREFUSED, 111 }, { ELOOP, 40 }, { ENAMETOOLONG, 36 }, { EHOSTDOWN, 112 },
	{ EHOSTUNREACH, 113 }, { ENOTEMPTY, 39 }, { EUSERS, 87 }, { EDQUOT, 122 }, { ESTALE, 116 },
	{ EREMOTE, 66 }, { ENOLCK, 37 }, { ENOSYS, 38 }, { EIDRM, 43 }, { ENOMSG, 42 }, { EOVERFLOW, 75 },
	{ ECANCELED, 125 }, { EILSEQ, 84 }, { EBADMSG, 74 }, { EMULTIHOP, 72 }, { ENOLINK, 67 },
	{ EPROTO, 71 }, { ENOTRECOVERABLE, 131 }, { EOWNERDEAD, 130 },
	{ ENOATTR, 61 }, /* Linux ENODATA: a missing xattr */
};
#define BSDOS_ERRNO_COUNT ((int)(sizeof(bsdos_errno_table) / sizeof(bsdos_errno_table[0])))

/* host errno -> guest (Linux) errno; an unknown code becomes EINVAL (22), never a different real error. */
static inline int bsdos_errno_host_to_guest(int host) {
	for (int i = 0; i < BSDOS_ERRNO_COUNT; i++)
		if (bsdos_errno_table[i].host == host) return bsdos_errno_table[i].guest;
	return 22;
}

/* guest (Linux) errno -> host errno; an unknown code becomes EINVAL. */
static inline int bsdos_errno_guest_to_host(int guest) {
	for (int i = 0; i < BSDOS_ERRNO_COUNT; i++)
		if (bsdos_errno_table[i].guest == guest) return bsdos_errno_table[i].host;
	return EINVAL;
}

/* ---- signals ---------------------------------------------------------------------------------- */

struct bsdos_signo_pair { int host; int guest; };

static const struct bsdos_signo_pair bsdos_signo_table[] = {
	{ SIGHUP, 1 }, { SIGINT, 2 }, { SIGQUIT, 3 }, { SIGILL, 4 }, { SIGTRAP, 5 }, { SIGABRT, 6 },
	{ SIGBUS, 7 }, { SIGFPE, 8 }, { SIGKILL, 9 }, { SIGUSR1, 10 }, { SIGSEGV, 11 }, { SIGUSR2, 12 },
	{ SIGPIPE, 13 }, { SIGALRM, 14 }, { SIGTERM, 15 }, { SIGCHLD, 17 }, { SIGCONT, 18 },
	{ SIGSTOP, 19 }, { SIGTSTP, 20 }, { SIGTTIN, 21 }, { SIGTTOU, 22 }, { SIGURG, 23 },
	{ SIGXCPU, 24 }, { SIGXFSZ, 25 }, { SIGVTALRM, 26 }, { SIGPROF, 27 }, { SIGWINCH, 28 },
	{ SIGIO, 29 }, { SIGSYS, 31 },
};
#define BSDOS_SIGNO_COUNT ((int)(sizeof(bsdos_signo_table) / sizeof(bsdos_signo_table[0])))

/* Guest signal numbers 65..126 name the FreeBSD real-time signals directly (the same numbers on both
 * sides of the boundary); 32..64 and the Linux-only STKFLT/PWR have no counterpart. */
#define BSDOS_SIGNO_RT_FIRST 65
#define BSDOS_SIGNO_RT_LAST 126

/* guest (Linux) signal -> host signal. 0 -> 0 (the "does the target exist" probe); no counterpart -> -1. */
static inline int bsdos_signo_guest_to_host(int guest) {
	if (guest == 0) return 0;
	if (guest >= BSDOS_SIGNO_RT_FIRST && guest <= BSDOS_SIGNO_RT_LAST) return guest;
	for (int i = 0; i < BSDOS_SIGNO_COUNT; i++)
		if (bsdos_signo_table[i].guest == guest) return bsdos_signo_table[i].host;
	return -1;
}

/* host signal -> guest (Linux) signal; no counterpart (SIGEMT, SIGINFO, ...) -> -1. */
static inline int bsdos_signo_host_to_guest(int host) {
	if (host == 0) return 0;
	if (host >= BSDOS_SIGNO_RT_FIRST && host <= BSDOS_SIGNO_RT_LAST) return host;
	for (int i = 0; i < BSDOS_SIGNO_COUNT; i++)
		if (bsdos_signo_table[i].host == host) return bsdos_signo_table[i].guest;
	return -1;
}

/* ---- mmap / msync ----------------------------------------------------------------------------- */

#define BSDOS_G_MAP_SHARED 0x1
#define BSDOS_G_MAP_PRIVATE 0x2
#define BSDOS_G_MAP_FIXED 0x10
#define BSDOS_G_MAP_ANONYMOUS 0x20
#define BSDOS_G_MAP_NORESERVE 0x4000
#define BSDOS_G_MAP_STACK 0x20000
#define BSDOS_G_MAP_FIXED_NOREPLACE 0x100000

/* PROT_READ/WRITE/EXEC are 1/2/4 on both sides: no translation (asserted by the static table check). */

/* guest mmap flags -> host flags. Linux FIXED_NOREPLACE becomes FIXED|EXCL. */
static inline int bsdos_mmap_flags_guest_to_host(long g) {
	int h = 0;
	if (g & BSDOS_G_MAP_SHARED) h |= MAP_SHARED;
	if (g & BSDOS_G_MAP_PRIVATE) h |= MAP_PRIVATE;
	if (g & BSDOS_G_MAP_FIXED) h |= MAP_FIXED;
	if (g & BSDOS_G_MAP_ANONYMOUS) h |= MAP_ANON;
	if (g & BSDOS_G_MAP_STACK) h |= MAP_STACK;
	if (g & BSDOS_G_MAP_FIXED_NOREPLACE) h |= MAP_FIXED | MAP_EXCL;
	return h;
}

/* host mmap flags -> guest flags. FIXED|EXCL is the host spelling of FIXED_NOREPLACE. */
static inline int bsdos_mmap_flags_host_to_guest(int h) {
	int g = 0;
	if (h & MAP_SHARED) g |= BSDOS_G_MAP_SHARED;
	if (h & MAP_PRIVATE) g |= BSDOS_G_MAP_PRIVATE;
	if ((h & MAP_FIXED) && (h & MAP_EXCL)) g |= BSDOS_G_MAP_FIXED_NOREPLACE;
	else if (h & MAP_FIXED) g |= BSDOS_G_MAP_FIXED;
	if (h & MAP_ANON) g |= BSDOS_G_MAP_ANONYMOUS;
	if (h & MAP_STACK) g |= BSDOS_G_MAP_STACK;
	return g;
}

#define BSDOS_G_MS_ASYNC 1
#define BSDOS_G_MS_INVALIDATE 2
#define BSDOS_G_MS_SYNC 4

/* guest msync flags -> host. Linux MS_SYNC is 4; FreeBSD MS_SYNC is 0 and its 4 is not a defined flag. */
static inline int bsdos_msync_flags_guest_to_host(long g) {
	int h = 0;
	if (g & BSDOS_G_MS_ASYNC) h |= MS_ASYNC;
	if (g & BSDOS_G_MS_INVALIDATE) h |= MS_INVALIDATE;
	if (g & BSDOS_G_MS_SYNC) h |= MS_SYNC;
	return h;
}

/* host msync flags -> guest. Host MS_SYNC is the absence of MS_ASYNC. */
static inline int bsdos_msync_flags_host_to_guest(int h) {
	int g = 0;
	if (h & MS_ASYNC) g |= BSDOS_G_MS_ASYNC; else g |= BSDOS_G_MS_SYNC;
	if (h & MS_INVALIDATE) g |= BSDOS_G_MS_INVALIDATE;
	return g;
}

#ifdef __cplusplus
}
#endif
#endif /* BSDOS_LINUX_XLATE_H */
