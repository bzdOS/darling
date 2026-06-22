/* FreeBSD shim: sys/syscall.h — maps Linux-only syscall numbers to FreeBSD.
 *
 * Only mappings actually used in the darlingserver port are listed here.
 * Unmappable Linux syscalls get a sentinel value (SYS_ENOSYS_SENTINEL) so
 * callers that guard with #ifdef DARLING_FREEBSD can still reference the
 * symbol name without undefined-identifier errors. */
#pragma once
#include_next <sys/syscall.h>

#ifdef DARLING_FREEBSD

/* tgkill(tgid, tid, sig) → thr_kill2(pid, id, sig): same argument order */
#ifndef SYS_tgkill
#  define SYS_tgkill SYS_thr_kill2
#endif

/* gettid: FreeBSD provides lwpid via thr_self() syscall */
#ifndef SYS_gettid
#  define SYS_gettid SYS_thr_self
#endif

#endif /* DARLING_FREEBSD */
