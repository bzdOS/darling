/*
 * trap_log.h — async-signal-safe trap/bridge markers.
 *
 * Logging-only instrumentation (authorized by the logging slice): every
 * marker is a single write(2) to fd 2 built with snprintf on a stack
 * buffer — snprintf is lock-free on FreeBSD; stdio FILE streams are NOT
 * used, because the in-handler fprintf path can block on the stderr
 * mutex (the hazard named in TRAP-WEDGE-READ.md).
 *
 * The gate: DARLING_TRAP_LOG=1 enables markers; default off, so normal
 * runs stay clean. Handler-side code must use the setup-time static
 * (mldr_trap_log_enabled, set once from setup_macos_syscall_trap before
 * any thread exists); normal-context code may re-check getenv per call.
 */
#ifndef MLDR_TRAP_LOG_H
#define MLDR_TRAP_LOG_H

#include <stdio.h>   /* snprintf only — no FILE streams */
#include <unistd.h>  /* write */
#include <sys/types.h>
#include <sys/thr.h> /* thr_self */

/* 1 when DARLING_TRAP_LOG was set at trap setup; handler-safe to read. */
extern int mldr_trap_log_enabled;

static inline void
mldr_tlog(const char *tag, long a, long b)
{
	char buf[160];
	long tid = 0;
	int n;

	thr_self(&tid); /* FreeBSD ABI: out-parameter, async-signal-safe */
	n = snprintf(buf, sizeof(buf), "[traplog] %s tid=%ld a=%ld b=%ld\n",
		     tag, tid, a, b);
	if (n > 0)
		(void)!write(2, buf, (size_t)(n < (int)sizeof(buf) ? n : (int)sizeof(buf) - 1));
}

/* hex-address variant for pointer crumbs (frames, handles) */
static inline void
mldr_tlogx(const char *tag, const void *p, long b)
{
	char buf[160];
	long tid = 0;
	int n;

	thr_self(&tid);
	n = snprintf(buf, sizeof(buf), "[traplog] %s tid=%ld p=%p b=%ld\n",
		     tag, tid, p, b);
	if (n > 0)
		(void)!write(2, buf, (size_t)(n < (int)sizeof(buf) ? n : (int)sizeof(buf) - 1));
}

#endif /* MLDR_TRAP_LOG_H */
