/* tz-probe: time zone setup must terminate and work in the guest, on the main thread and on a small-stack thread.
 * tzset + localtime_r + CFTimeZoneCopySystem + CFTimeZoneGetName, each bounded by a call counter on open() of the
 * zone files (a runaway re-entry of tzload shows up as a count above the bound, a stack overflow as a fault).
 * Prints TZPROBE lines and one TZ-VERDICT line. */
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <execinfo.h>
#include <dlfcn.h>
#include <stdlib.h>
extern const void *CFTimeZoneCopySystem(void);
extern const void *CFTimeZoneGetName(const void *);
extern const void *CFTimeZoneCreateWithName(const void *, const void *, unsigned char);
extern const void *CFStringCreateWithCString(const void *, const char *, unsigned);
extern double CFTimeZoneGetSecondsFromGMT(const void *, double);
extern long CFStringGetLength(const void *);
extern void CFRelease(const void *);
extern void *malloc_default_zone(void);
extern unsigned long CFAllocatorGetTypeID(void), CFStringGetTypeID(void);
extern const void *kCFAllocatorSystemDefault;
typedef struct { long version; void *info; void *a, *b, *c; void *alloc; void *d, *e, *f; } ACtx;
extern void CFAllocatorGetContext(const void *, ACtx *);
extern const void *CFDataCreate(const void *, const unsigned char *, long);

static volatile int stage;
static void onsig(int s, siginfo_t *si, void *uc)
{
	char b[128]; void *bt[40]; int k;
	char *mc = *(char **)((char *)uc + 48); unsigned long rip = *(unsigned long *)(mc + 144), rbp = *(unsigned long *)(mc + 16 + 8 * 6); Dl_info di;
	if (dladdr((void *)rip, &di)) { int m = snprintf(b, sizeof b, "TZPROBE rip=%p %s+%ld in %s\n", (void *)rip, di.dli_sname ? di.dli_sname : "?", di.dli_sname ? (long)(rip - (unsigned long)di.dli_saddr) : 0L, di.dli_fname); write(1, b, m); }
	for (int f = 0; f < 12 && rbp > 4096 && !(rbp & 7); f++) { unsigned long ra = ((unsigned long *)rbp)[1]; if (dladdr((void *)ra, &di)) { int m = snprintf(b, sizeof b, "TZPROBE  <- %s+%ld in %s\n", di.dli_sname ? di.dli_sname : "?", di.dli_sname ? (long)(ra - (unsigned long)di.dli_saddr) : 0L, di.dli_fname); write(1, b, m); } rbp = ((unsigned long *)rbp)[0]; }
	k = 0;
	int n = snprintf(b, sizeof b, "TZPROBE fault addr=%p frames=%d\n", si->si_addr, k); write(1, b, n);
	backtrace_symbols_fd(bt, k > 24 ? 24 : k, 1);
	n = snprintf(b, sizeof b, "TZ-VERDICT FAIL signal=%d stage=%d\n", s, stage); write(1, b, n); _exit(1);
}
static void onalrm(int s) { char b[96]; int n = snprintf(b, sizeof b, "TZ-VERDICT FAIL hang stage=%d\n", stage); (void)s; write(1, b, n); _exit(1); }

static int work(const char *who)
{
	time_t t = 1700000000; struct tm tm; memset(&tm, 0, sizeof tm);
	{ ACtx c; memset(&c, 0, sizeof c); CFAllocatorGetContext(kCFAllocatorSystemDefault, &c);
	  printf("TZPROBE %s default_zone=%p sysalloc_info=%p alloc_typeid=%lu str_typeid=%lu\n", who, malloc_default_zone(), c.info, CFAllocatorGetTypeID(), CFStringGetTypeID()); fflush(stdout); }
	stage++; tzset();
	if (access("/etc/localtime", R_OK) || access("/var/db/timezone/zoneinfo/UTC", R_OK)) {
		printf("TZPROBE %s zone data missing: /etc/localtime or /var/db/timezone/zoneinfo/UTC not readable\n", who); return 1; }
	for (int i = 0; i < 2000; i++) localtime_r(&t, &tm);
	stage++; if (!localtime_r(&t, &tm) || tm.tm_year != 123) { printf("TZPROBE %s localtime bad year=%d\n", who, tm.tm_year); return 1; }
	stage++; const void *z = CFTimeZoneCopySystem();
	if (!z) { printf("TZPROBE %s CFTimeZoneCopySystem nil\n", who); return 1; }
	stage++; const void *nm = CFTimeZoneGetName(z);
	long l = nm ? CFStringGetLength(nm) : -1;
	printf("TZPROBE %s ok year=%d zonename_len=%ld\n", who, tm.tm_year + 1900, l);
	CFRelease(z);
	stage++; { const void *nm2 = CFStringCreateWithCString(0, "Europe/Berlin", 0x08000100);
	  const void *b = CFTimeZoneCreateWithName(0, nm2, 0);
	  if (!b || CFTimeZoneGetSecondsFromGMT(b, 1700000000.0 - 978307200.0) != 3600.0) { printf("TZPROBE %s Europe/Berlin not resolved from zone data\n", who); return 1; } }
	return l > 0 ? 0 : 1;
}
static void *thr(void *a) { *(int *)a = work("thread64k"); return 0; }

int main(void)
{
	{ void *h = dlopen("/System/Library/Frameworks/CoreFoundation.framework/Versions/A/CoreFoundation", RTLD_NOW);
	  void (*ini)(void) = (void (*)(void))dlsym(RTLD_DEFAULT, "__CFInitialize");
	  printf("TZPROBE pre dlopen=%p initsym=%p alloc_typeid=%lu\n", h, (void *)ini, CFAllocatorGetTypeID());
	  if (ini && !CFAllocatorGetTypeID()) { ini(); printf("TZPROBE CF constructor had not run (no LC_ROUTINES); called __CFInitialize, alloc_typeid=%lu\n", CFAllocatorGetTypeID()); }
	  fflush(stdout); }
	int r1, r2 = 1; pthread_t th; pthread_attr_t at;
	static char alt[65536]; stack_t ss = { alt, sizeof alt, 0 }; struct sigaction sa;
	sigaltstack(&ss, 0); memset(&sa, 0, sizeof sa); sa.sa_sigaction = onsig; sa.sa_flags = SA_SIGINFO | SA_ONSTACK;
	sigaction(SIGSEGV, &sa, 0); sigaction(SIGBUS, &sa, 0); signal(SIGALRM, onalrm); alarm(40);
	r1 = work("main");
	pthread_attr_init(&at); pthread_attr_setstacksize(&at, 64 * 1024);
	if (pthread_create(&th, &at, thr, &r2) == 0) pthread_join(th, 0);
	if (!r1 && !r2) puts("TZ-VERDICT pass main=ok thread64k=ok");
	else printf("TZ-VERDICT FAIL main=%d thread64k=%d\n", r1, r2);
	return r1 | r2;
}
