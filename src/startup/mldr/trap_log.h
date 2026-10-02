/*
 * trap_log.h — locale-free, async-signal-safe trap/bridge markers.
 *
 * Prepared for the traplog-nolocale lane: the fault
 * object measured in SURVIVE-WINDOW.md is the thread's LOCALE data read
 * inside libc's vfprintf (vfprintf.c:463, decimal_point =
 * localeconv_l(locale)->decimal_point), so ANY stdio formatting on the
 * marker path touches the faulting object. This version formats with
 * hand-rolled converters only — decimal and lowercase hex — and emits
 * with write(2). No printf/snprintf/fprintf anywhere on the marker
 * path; traplog-formats-test.c byte-checks the converters.
 *
 * The gate: DARLING_TRAP_LOG=1 enables markers; default off. Handler-
 * side code must use the setup-time static (mldr_trap_log_enabled, set
 * once from setup_macos_syscall_trap); normal-context code may re-check
 * getenv per call. The crash path (crash_debug_handler's FATAL print)
 * is NOT part of this header and is not touched by the lane.
 */
#ifndef MLDR_TRAP_LOG_H
#define MLDR_TRAP_LOG_H

#include <string.h>  /* memcpy/memmove — no stdio */
#include <unistd.h>  /* write */
#include <sys/types.h>
#include <sys/thr.h> /* thr_self */

/* 1 when DARLING_TRAP_LOG was set at trap setup; handler-safe to read. */
extern int mldr_trap_log_enabled;

/* Handler-side sub-gate (survive-window slice). 1 = handlers log
 * (default when the main gate is on); 0 = DARLING_TRAP_LOG_HANDLERS=0
 * silences only the in-handler writes. Handler-safe to read. */
extern int mldr_trap_log_handlers;

/* ── locale-free converters ───────────────────────────────────────────────
 * Write the value into [*low, end) right-aligned, return the new low
 * pointer. Digits are produced least-significant-first and placed
 * backwards so the result reads correctly left-to-right; the sign is
 * written first into the tail. No locale, no allocation, no FILE. */
static inline char *
mldr_fmt_dec(char *end, long v)
{
	char tmp[24];
	int n = 0;
	unsigned long u;
	int i;
	int neg = 0;

	if (v < 0) {
		neg = 1;
		u = (unsigned long)(-(v + 1)) + 1; /* safe for LONG_MIN */
	} else {
		u = (unsigned long)v;
	}
	do {
		tmp[n++] = (char)('0' + (int)(u % 10));
		u /= 10;
	} while (u != 0 && n < (int)sizeof(tmp));
	for (i = 0; i < n; i++)
		*--end = tmp[i];
	if (neg)
		*--end = '-';
	return end;
}

static inline char *
mldr_fmt_hex(char *end, unsigned long u)
{
	static const char digits[] = "0123456789abcdef";
	char tmp[20];
	int n = 0;
	int i;

	do {
		tmp[n++] = digits[u & 0xf];
		u >>= 4;
	} while (u != 0 && n < (int)sizeof(tmp));
	for (i = 0; i < n; i++)
		*--end = tmp[i];
	return end;
}

/* "[traplog] <tag> tid=<dec> a=<dec> b=<dec>\n" -> buf, returns length */
static inline int
mldr_tlog_build(char *buf, int cap, const char *tag, long a, long b)
{
	char out[192];
	char tail[64];
	char *w;
	int o = 0;
	int tlen;
	long tid = 0;

	thr_self(&tid);

	tlen = (int)strlen(tag);
	if (tlen > 60)
		tlen = 60;
	memcpy(out, "[traplog] ", 10);
	o = 10;
	memcpy(out + o, tag, (size_t)tlen);
	o += tlen;
	memcpy(out + o, " tid=", 5);
	o += 5;
	w = mldr_fmt_dec(tail + sizeof(tail), tid);
	memcpy(out + o, w, (size_t)((tail + sizeof(tail)) - w));
	o += (int)((tail + sizeof(tail)) - w);
	out[o++] = ' ';
	out[o++] = 'a';
	out[o++] = '=';
	w = mldr_fmt_dec(tail + sizeof(tail), a);
	memcpy(out + o, w, (size_t)((tail + sizeof(tail)) - w));
	o += (int)((tail + sizeof(tail)) - w);
	out[o++] = ' ';
	out[o++] = 'b';
	out[o++] = '=';
	w = mldr_fmt_dec(tail + sizeof(tail), b);
	memcpy(out + o, w, (size_t)((tail + sizeof(tail)) - w));
	o += (int)((tail + sizeof(tail)) - w);
	out[o++] = '\n';
	if (o > cap)
		o = cap;
	memcpy(buf, out, (size_t)o);
	return o;
}

static inline void
mldr_tlog(const char *tag, long a, long b)
{
	char buf[192];
	int n = mldr_tlog_build(buf, (int)sizeof(buf), tag, a, b);

	if (n > 0)
		(void)!write(2, buf, (size_t)n);
}

/* "[traplog] <tag> tid=<dec> p=0x<hex> b=<dec>\n" -> buf, returns length */
static inline int
mldr_tlogx_build(char *buf, int cap, const char *tag, const void *pv, long b)
{
	char out[192];
	char tail[64];
	char *w;
	int o = 0;
	int tlen;
	long tid = 0;

	thr_self(&tid);

	tlen = (int)strlen(tag);
	if (tlen > 60)
		tlen = 60;
	memcpy(out, "[traplog] ", 10);
	o = 10;
	memcpy(out + o, tag, (size_t)tlen);
	o += tlen;
	memcpy(out + o, " tid=", 5);
	o += 5;
	w = mldr_fmt_dec(tail + sizeof(tail), tid);
	memcpy(out + o, w, (size_t)((tail + sizeof(tail)) - w));
	o += (int)((tail + sizeof(tail)) - w);
	memcpy(out + o, " p=0x", 5);
	o += 5;
	w = mldr_fmt_hex(tail + sizeof(tail), (unsigned long)(uintptr_t)pv);
	memcpy(out + o, w, (size_t)((tail + sizeof(tail)) - w));
	o += (int)((tail + sizeof(tail)) - w);
	out[o++] = ' ';
	out[o++] = 'b';
	out[o++] = '=';
	w = mldr_fmt_dec(tail + sizeof(tail), b);
	memcpy(out + o, w, (size_t)((tail + sizeof(tail)) - w));
	o += (int)((tail + sizeof(tail)) - w);
	out[o++] = '\n';
	if (o > cap)
		o = cap;
	memcpy(buf, out, (size_t)o);
	return o;
}

static inline void
mldr_tlogx(const char *tag, const void *pv, long b)
{
	char buf[192];
	int n = mldr_tlogx_build(buf, (int)sizeof(buf), tag, pv, b);

	if (n > 0)
		(void)!write(2, buf, (size_t)n);
}

#endif /* MLDR_TRAP_LOG_H */
