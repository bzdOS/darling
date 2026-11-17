/* v5store-hook: proof probe for the Chrome rc133 CHECK at ChromeFW+0x916d75 (class A, caller SafeBrowsing.V5Store).
 * Static finding: ChromeFW+0x916d1f is base ClockNow(): clock_gettime(16 = CLOCK_THREAD_CPUTIME_ID, &ts) != 0 -> int3
 * (the trap is at +0x916d73). libsystem_c clock_gettime(16) returns -1 when clock_gettime_nsec_np(16) == 0, i.e. when
 * __thread_selfusage() (syscall 482, sys_thread_selfusage) returns 0.
 * Loaded via DARLING_INSERT_EXTRA; active only with DARLING_V5STORE_PROBE=1. Lines "V5STORE ..." on stderr.
 * Interposes clock_gettime, clock_gettime_nsec_np, __thread_selfusage: logs args, result, errno, caller, tid for clock 16
 * (first calls and every failure). In the SIGTRAP handler prints registers and re-calls the three. Not a fix. */
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdio.h>
#include <stdint.h>
#include <stdarg.h>
#include <errno.h>
#include <time.h>
#include <sys/time.h>
#include <sys/resource.h>

typedef struct { const char *dli_fname; void *dli_fbase; const char *dli_sname; void *dli_saddr; } dlinfo;
extern int dladdr(const void *, dlinfo *);
extern int sigaction(int, const void *, void *);
extern uint64_t __thread_selfusage(void);
extern int pthread_threadid_np(void *, uint64_t *);
struct my_sa { void (*h)(int, void *, void *); unsigned mask; int flags; };
static struct my_sa g_chain;
static int g_on, g_ours;
static volatile int g_n16, g_nfail16, g_nsu, g_nsu0, g_nns, g_nns0;

static void out(const char *f, ...)
{
	char b[600]; va_list ap; va_start(ap, f);
	int n = vsnprintf(b, sizeof b, f, ap); va_end(ap);
	if (n > 0) write(2, b, n < (int)sizeof b ? n : (int)sizeof b - 1);
}
static const char *where(const void *a, char *buf, int len)
{
	dlinfo d;
	if (a && dladdr(a, &d) && d.dli_fname) {
		const char *s = strrchr(d.dli_fname, 47);
		snprintf(buf, len, "%s+0x%lx", s ? s + 1 : d.dli_fname, (unsigned long)((char *)a - (char *)d.dli_fbase));
	} else snprintf(buf, len, "?");
	return buf;
}
static uint64_t tid(void) { uint64_t t = 0; pthread_threadid_np(0, &t); return t; }


extern int __linux_syscall(int nr, ...);
extern uint64_t sys_thread_selfusage(void);
static void raw_probe(const char *tag)
{
	long ts[2]; int i;
	static const long clks[] = { 0, 1, 2, 3, 4, 5, 6, 7 };
	for (i = 0; i < 8; i++) {
		long r;
		ts[0] = -7; ts[1] = -7;
		r = (long)__linux_syscall(228, clks[i], ts, 0L, 0L, 0L, 0L);
		out("V5STORE raw %s __linux_syscall(228 clock_gettime, clk=%ld)=%ld ts=%ld.%09ld\n", tag, clks[i], r, ts[0], ts[1]);
	}
	{
		unsigned long long u = sys_thread_selfusage();
		out("V5STORE raw %s sys_thread_selfusage()=%llu\n", tag, u);
	}
}
static volatile int g_rawn;
static uint64_t my_selfusage(void)
{
	int e0 = errno;
	uint64_t r = __thread_selfusage();
	int e = errno; char w[160];
	int n = __sync_add_and_fetch(&g_nsu, 1);
	if (!r) __sync_add_and_fetch(&g_nsu0, 1);
	if (g_on && (n <= 5 || !r))
		out("V5STORE __thread_selfusage()=%llu errno=%d (before %d) tid=%llu caller=%s\n", (unsigned long long)r, e, e0, (unsigned long long)tid(), where(__builtin_return_address(0), w, sizeof w));
	if (g_on && !r && __sync_add_and_fetch(&g_rawn, 1) <= 2) raw_probe("on-zero");
	return r;
}
static uint64_t my_nsec_np(int clk)
{
	uint64_t r = clock_gettime_nsec_np(clk);
	int e = errno; char w[160];
	if (clk == 16) {
		int n = __sync_add_and_fetch(&g_nns, 1);
		if (!r) __sync_add_and_fetch(&g_nns0, 1);
		if (g_on && (n <= 5 || !r))
			out("V5STORE clock_gettime_nsec_np(16)=%llu errno=%d tid=%llu caller=%s\n", (unsigned long long)r, e, (unsigned long long)tid(), where(__builtin_return_address(0), w, sizeof w));
	}
	return r;
}
static int my_cgt(int clk, struct timespec *tp)
{
	struct timespec before = { -1, -1 };
	if (tp) before = *tp;
	errno = 0;
	int r = clock_gettime(clk, tp);
	int e = errno; char w[160];
	if (clk == 16) {
		int n = __sync_add_and_fetch(&g_n16, 1);
		if (r) __sync_add_and_fetch(&g_nfail16, 1);
		if (g_on && (n <= 5 || r))
			out("V5STORE clock_gettime(16,%p)=%d errno=%d ts=%ld.%09ld tid=%llu caller=%s n16=%d fail=%d\n", (void *)tp, r, e,
			    tp ? (long)tp->tv_sec : -1L, tp ? tp->tv_nsec : -1L, (unsigned long long)tid(), where(__builtin_return_address(0), w, sizeof w), n, g_nfail16);
	}
	return r;
}
static int my_sigaction(int sig, const struct my_sa *act, struct my_sa *old)
{
	if (sig == 5 && g_ours && act) { if (old) *old = g_chain; g_chain = *act; return 0; }
	return sigaction(sig, act, old);
}
static void on_trap(int sig, void *info, void *uc)
{
	uintptr_t *mc = *(uintptr_t **)((char *)uc + 48);
	char w[160];
	out("V5STORE trap tid=%llu rip=%p (%s) rax=%p rbx=%p rcx=%p rdx=%p rdi=%p rsi=%p rbp=%p rsp=%p\n", (unsigned long long)tid(), (void *)mc[18],
	    where((void *)mc[18], w, sizeof w), (void *)mc[2], (void *)mc[3], (void *)mc[4], (void *)mc[5], (void *)mc[6], (void *)mc[7], (void *)mc[8], (void *)mc[9]);
	out("V5STORE trap r8=%p r12=%p r14=%p r15=%p counts: cgt16=%d fail=%d selfusage=%d zero=%d nsec_np16=%d zero=%d\n", (void *)mc[10], (void *)mc[14], (void *)mc[16], (void *)mc[17],
	    g_n16, g_nfail16, g_nsu, g_nsu0, g_nns, g_nns0);
	/* saved ts of the failed call is at rbp-16 */
	if (mc[8]) { long *ts = (long *)(mc[8] - 16); out("V5STORE trap ts[rbp-16]=%ld.%09ld\n", ts[0], ts[1]); }
	raw_probe("trap");
	errno = 0;
	uint64_t a = __thread_selfusage(); int ea = errno;
	uint64_t b = clock_gettime_nsec_np(16); int eb = errno;
	struct timespec t = { -1, -1 }; errno = 0; int c = clock_gettime(16, &t); int ec = errno;
	struct rusage ru; int d = getrusage(RUSAGE_SELF, &ru);
	out("V5STORE trap recall: selfusage=%llu(errno %d) nsec_np=%llu(errno %d) cgt=%d(errno %d ts=%ld.%09ld) getrusage=%d utime=%ld.%06d\n",
	    (unsigned long long)a, ea, (unsigned long long)b, eb, c, ec, (long)t.tv_sec, (long)t.tv_nsec, d, (long)ru.ru_utime.tv_sec, (int)ru.ru_utime.tv_usec);
	{ struct my_sa sa = { 0, 0, 0 }; sigaction(5, &sa, 0); }
}
__attribute__((constructor)) static void init(void)
{
	g_on = getenv("DARLING_V5STORE_PROBE") != 0;
	if (!g_on) return;
	struct my_sa sa = { on_trap, 0, 0x40 };
	sigaction(5, &sa, 0);
	g_ours = 1;
	out("V5STORE init pid=%d\n", (int)getpid());
	raw_probe("init");
}
struct interpose { void *replacement, *replacee; };
__attribute__((used, section("__DATA,__interpose"))) static struct interpose interposers[] = {
	{ (void *)my_sigaction, (void *)sigaction },
	{ (void *)my_cgt, (void *)clock_gettime },
	{ (void *)my_nsec_np, (void *)clock_gettime_nsec_np },
	{ (void *)my_selfusage, (void *)__thread_selfusage },
};
