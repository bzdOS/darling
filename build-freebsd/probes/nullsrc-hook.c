/* nullsrc-hook: proof probe for the Chrome rc133 CHECK (ChromeFW+0x11a0c40, 2nd argument NULL).
 * Static finding: the caller ChromeFW+0x245eaf0 gets its object from +[NSFont systemFontOfSize:] (arg = +[NSFont systemFontSize]).
 * Loaded via DARLING_INSERT_EXTRA; active only with DARLING_NULLSRC_PROBE=1. Lines "NULLSRC ..." on stderr.
 * In the SIGTRAP handler (and once at image-add of the Chrome framework + 1 s later is not needed) it calls the same
 * selectors and reports: result of systemFontOfSize:, the IMP and its image, CTFontCreateWithName("Helvetica"), whether
 * the class is the AppKit one and which dylib holds the IMP. Not a fix. */
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdio.h>
#include <stdint.h>
#include <stdarg.h>

typedef struct { const char *dli_fname; void *dli_fbase; const char *dli_sname; void *dli_saddr; } dlinfo;
extern int dladdr(const void *, dlinfo *);
extern void *dlsym(void *, const char *);
extern int sigaction(int, const void *, void *);
struct my_sa { void (*h)(int, void *, void *); unsigned mask; int flags; };
static struct my_sa g_chain;
static int g_on, g_ours;

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
		snprintf(buf, len, "%s(%s)", s ? s + 1 : d.dli_fname, d.dli_sname ? d.dli_sname : "?");
	} else snprintf(buf, len, "?");
	return buf;
}
static int my_sigaction(int sig, const struct my_sa *act, struct my_sa *old)
{
	if (sig == 5 && g_ours && act) { if (old) *old = g_chain; g_chain = *act; return 0; }
	return sigaction(sig, act, old);
}
static void report(const char *tag)
{
	char w[200];
	void *(*gc)(const char *) = dlsym((void *)-2, "objc_getClass");
	void *(*gs)(const char *) = dlsym((void *)-2, "sel_registerName");
	void *(*og)(void *) = dlsym((void *)-2, "object_getClass");
	void *(*gi)(void *, void *) = dlsym((void *)-2, "class_getMethodImplementation");
	const char *(*cn)(void *) = dlsym((void *)-2, "class_getName");
	void *(*ms)(void *, void *, ...) = dlsym((void *)-2, "objc_msgSend");
	if (!gc || !gs || !og || !gi || !ms) { out("NULLSRC %s objc-symbols-missing\n", tag); return; }
	void *cls = gc("NSFont");
	out("NULLSRC %s NSFont class=%p\n", tag, cls);
	if (!cls) return;
	void *meta = og(cls);
	void *imp = gi(meta, gs("systemFontOfSize:"));
	out("NULLSRC %s imp(+systemFontOfSize:)=%p in %s\n", tag, imp, where(imp, w, sizeof w));
	imp = gi(meta, gs("systemFontSize"));
	out("NULLSRC %s imp(+systemFontSize)=%p in %s\n", tag, imp, where(imp, w, sizeof w));
	double (*fs)(void *, void *) = (double (*)(void *, void *))ms;
	double sz = fs(cls, gs("systemFontSize"));
	out("NULLSRC %s systemFontSize=%g\n", tag, sz);
	void *(*fo)(void *, void *, double) = (void *(*)(void *, void *, double))ms;
	void *f = fo(cls, gs("systemFontOfSize:"), sz);
	out("NULLSRC %s systemFontOfSize:=%p (%s)\n", tag, f, f ? "non-nil" : "NIL");
	if (f) out("NULLSRC %s result class=%s\n", tag, cn(og(f)));
	void *(*mk)(const void *, double, const void *) = dlsym((void *)-2, "CTFontCreateWithName");
	void *(*mks)(const void *, const char *, unsigned) = dlsym((void *)-2, "CFStringCreateWithCString");
	out("NULLSRC %s CTFontCreateWithName=%p CFStringCreateWithCString=%p\n", tag, (void *)mk, (void *)mks);
	if (mk && mks) {
		void *n = mks(0, "Helvetica", 0x08000100);
		void *ct = mk(n, 12.0, 0);
		out("NULLSRC %s CTFontCreateWithName(Helvetica,12)=%p (%s) in %s\n", tag, ct, ct ? "non-nil" : "NIL", where((void *)mk, w, sizeof w));
	}
}
static void on_trap(int sig, void *info, void *uc)
{
	uintptr_t *mc = *(uintptr_t **)((char *)uc + 48);
	out("NULLSRC trap rip=%p rsi=%p r12=%p\n", (void *)mc[18], (void *)mc[7], (void *)mc[14]);
	report("trap");
	{ struct my_sa sa = { 0, 0, 0 }; sigaction(5, &sa, 0); }
}
__attribute__((constructor)) static void init(void)
{
	g_on = getenv("DARLING_NULLSRC_PROBE") != 0;
	if (!g_on) return;
	struct my_sa sa = { on_trap, 0, 0x40 };
	sigaction(5, &sa, 0);
	g_ours = 1;
	out("NULLSRC init pid=%d\n", (int)getpid());
}
struct interpose { void *replacement, *replacee; };
__attribute__((used, section("__DATA,__interpose"))) static struct interpose interposers[] = {
	{ (void *)my_sigaction, (void *)sigaction },
};
