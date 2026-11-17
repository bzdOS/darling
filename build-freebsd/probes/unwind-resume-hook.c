/* unwind-resume-hook: proof probe for the Chrome int3-after-_Unwind_Resume trap (rc133). Not a fix.
 * Loaded via DARLING_INSERT_EXTRA; active only with DARLING_UNWIND_PROBE=1. Output on stderr, lines "UNWPROBE ...":
 *  - interposes _Unwind_Resume / _Unwind_RaiseException: caller, _Unwind_Exception fields, _Unwind_Backtrace outcome
 *  - counters tell whether interposition reached the framework at all
 *  - add-image callback: when the Chrome framework is mapped, _dyld_find_unwind_sections on base+0x11a0ecf
 *  - SIGTRAP handler (works without interposition): rip, r15 (= the _Unwind_Exception passed to _Unwind_Resume),
 *    the framework's stub slot for the call target resolved with dladdr, counters, find_sections again. */
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdio.h>
#include <stdint.h>
#include <stdarg.h>
static void out_chain_note(void *);

typedef struct { uint64_t cls; void *cleanup; uintptr_t p1, p2; } uexc;
extern void _Unwind_Resume(void *);
extern int _Unwind_RaiseException(void *);
extern int _Unwind_Backtrace(int (*)(void *, void *), void *);
extern uintptr_t _Unwind_GetIP(void *), _Unwind_GetCFA(void *), _Unwind_GetRegionStart(void *), _Unwind_GetLanguageSpecificData(void *);
extern unsigned _dyld_image_count(void);
extern const char *_dyld_get_image_name(unsigned);
extern const void *_dyld_get_image_header(unsigned);
extern void _dyld_register_func_for_add_image(void (*)(const void *, intptr_t));
extern void *dlsym(void *, const char *);
typedef struct { const char *dli_fname; void *dli_fbase; const char *dli_sname; void *dli_saddr; } dlinfo;
extern int dladdr(const void *, dlinfo *);
struct usec { const void *mh, *dwarf; uintptr_t dwarf_len; const void *cu; uintptr_t cu_len; };
struct my_sa { void (*h)(int, void *, void *); unsigned mask; int flags; };
extern int sigaction(int, const void *, void *);
static struct my_sa g_chain; /* the application's own SIGTRAP disposition (Chromium installs a stack-dump handler that would replace ours) */
static int g_ours_installed;
static void on_trap(int, void *, void *);
static int my_sigaction(int sig, const struct my_sa *act, struct my_sa *old)
{
	if (sig == 5 && g_ours_installed && act) {
		if (old) *old = g_chain;
		g_chain = *act;
		out_chain_note(act->h);
		return 0;
	}
	return sigaction(sig, act, old);
}

#define TRAP_OFF 0x11a0ecfUL
#define STUB_OFF 0xdbbefd8UL
static int g_on, g_resume, g_raise, g_bt;
static void out_chain_note(void *);
static uintptr_t g_fw;

static void out(const char *f, ...)
{
	char b[700]; va_list ap; va_start(ap, f);
	int n = vsnprintf(b, sizeof b, f, ap); va_end(ap);
	if (n > 0) write(2, b, n < (int)sizeof b ? n : (int)sizeof b - 1);
}
static const char *where(uintptr_t a, char *buf, int len)
{
	dlinfo d;
	if (a && dladdr((void *)a, &d) && d.dli_fname) {
		const char *s = strrchr(d.dli_fname, 47);
		snprintf(buf, len, "%s+0x%lx(%s)", s ? s + 1 : d.dli_fname, (unsigned long)(a - (uintptr_t)d.dli_fbase), d.dli_sname ? d.dli_sname : "?");
	} else snprintf(buf, len, "?");
	return buf;
}
static void find_sections(const char *tag, uintptr_t addr)
{
	int (*f)(const void *, struct usec *) = dlsym((void *)-2, "_dyld_find_unwind_sections");
	struct usec u; memset(&u, 0, sizeof u);
	if (!f) { out("UNWPROBE find_sections tag=%s sym=missing\n", tag); return; }
	int r = f((void *)addr, &u);
	out("UNWPROBE find_sections tag=%s addr=%p ret=%d mh=%p eh_frame=%p/%lu unwind_info=%p/%lu\n", tag, (void *)addr, r, u.mh, u.dwarf,
	    (unsigned long)u.dwarf_len, u.cu, (unsigned long)u.cu_len);
}
static void add_image(const void *mh, intptr_t slide)
{
	unsigned n = _dyld_image_count();
	for (unsigned i = 0; i < n; i++) if (_dyld_get_image_header(i) == mh) {
		const char *nm = _dyld_get_image_name(i);
		if (nm && strstr(nm, "Chrome for Testing Framework") && !g_fw) {
			g_fw = (uintptr_t)mh;
			if (g_on) { out("UNWPROBE fw base=%p slide=%lx\n", mh, (long)slide); find_sections("add-image", g_fw + TRAP_OFF); }
		}
	}
}
struct bt { int n; };
static int bt_cb(void *ctx, void *arg)
{
	struct bt *s = arg; char w[160];
	uintptr_t ip = _Unwind_GetIP(ctx);
	if (s->n < 40) out("UNWPROBE bt#%d ip=%s cfa=%p start=%p lsda=%p\n", s->n, where(ip - 1, w, sizeof w), (void *)_Unwind_GetCFA(ctx),
	    (void *)_Unwind_GetRegionStart(ctx), (void *)_Unwind_GetLanguageSpecificData(ctx));
	s->n++; return 0;
}
static void do_bt(const char *tag)
{
	struct bt s = {0}; g_bt++;
	int r = _Unwind_Backtrace(bt_cb, &s);
	out("UNWPROBE bt-done tag=%s code=%d frames=%d\n", tag, r, s.n);
}
static void show_exc(const char *tag, void *e, void *ret)
{
	char w[160]; uexc *x = e;
	out("UNWPROBE %s caller=%s exc=%p class=%016llx cleanup=%p p1=%p p2=%p\n", tag, where((uintptr_t)ret, w, sizeof w), e,
	    x ? (unsigned long long)x->cls : 0ULL, x ? x->cleanup : 0, x ? (void *)x->p1 : 0, x ? (void *)x->p2 : 0);
}
static void my_resume(void *e)
{
	if (g_on) { g_resume++; show_exc("resume", e, __builtin_return_address(0)); do_bt("resume"); }
	_Unwind_Resume(e);
	if (g_on) out("UNWPROBE resume-RETURNED exc=%p\n", e);
	__builtin_trap();
}
static int my_raise(void *e)
{
	if (g_on) { g_raise++; show_exc("raise", e, __builtin_return_address(0)); }
	int r = _Unwind_RaiseException(e);
	if (g_on) out("UNWPROBE raise-RETURNED code=%d\n", r);
	return r;
}
static void on_trap(int sig, void *info, void *uc)
{
	char w[200];
	uintptr_t *mc = *(uintptr_t **)((char *)uc + 48);
	uintptr_t rip = mc[18], r15 = mc[17], rsp = mc[9];
	out("UNWPROBE trap rip=%s r15=%p rsp=%p counters resume=%d raise=%d bt=%d fw=%p\n", where(rip, w, sizeof w), (void *)r15, (void *)rsp, g_resume,
	    g_raise, g_bt, (void *)g_fw);
	if (g_fw) {
		uint8_t *st = (uint8_t *)(g_fw + STUB_OFF);
		out("UNWPROBE stub bytes=%02x%02x%02x%02x%02x%02x\n", st[0], st[1], st[2], st[3], st[4], st[5]);
		if (st[0] == 0xff && st[1] == 0x25) {
			uintptr_t slot = (uintptr_t)(st + 6) + *(int32_t *)(st + 2), tgt = *(uintptr_t *)slot;
			out("UNWPROBE stub slot=%p target=%s\n", (void *)slot, where(tgt, w, sizeof w));
			out("UNWPROBE interposed=%d my_resume=%p\n", tgt == (uintptr_t)my_resume, (void *)my_resume);
		}
		find_sections("trap", g_fw + TRAP_OFF);
	}
	out("UNWPROBE regs rax=%p rbx=%p rcx=%p rdx=%p rdi=%p rsi=%p r12=%p r13=%p r14=%p r15=%p rflags=%p\n", (void *)mc[2], (void *)mc[3], (void *)mc[4],
	    (void *)mc[5], (void *)mc[6], (void *)mc[7], (void *)mc[14], (void *)mc[15], (void *)mc[16], (void *)mc[17], (void *)mc[19]);
	/* function at fw+0x11a0c40: r12 = 2nd arg (object), r14 = lookup result of fw+0xdf6470(r12,0,0), 12(rbx) = existing id */
	if (g_fw && mc[3] > 4096) out("UNWPROBE this 12(rbx)=%d 16(rbx)=%p 24(rbx)=%p\n", *(int *)(mc[3] + 12), *(void **)(mc[3] + 16), *(void **)(mc[3] + 24));
	if (g_fw && mc[3] > 4096) {
		unsigned char *sp = (unsigned char *)(mc[3] + 32); /* libc++ std::string at this+32: short form = flag byte at +23 (high bit clear) */
		if (!(sp[23] & 0x80)) out("UNWPROBE this name(short)=%.22s\n", (char *)sp);
		else out("UNWPROBE this name(long)=%.60s\n", *(char **)sp);
	}
	{
		void *(*og)(void *) = dlsym((void *)-2, "object_getClass");
		const char *(*cn)(void *) = dlsym((void *)-2, "class_getName");
		if (og && cn && mc[14] > 65536) out("UNWPROBE r12 class=%s\n", cn(og((void *)mc[14])));
	}
	{ struct my_sa sa = { 0, 0, 0 }; sigaction(5, &sa, 0); }
}
static void out_chain_note(void *h)
{
	char w[160];
	if (g_on) out("UNWPROBE app installs SIGTRAP handler %s (kept as chain)\n", where((uintptr_t)h, w, sizeof w));
}
__attribute__((constructor)) static void init(void)
{
	g_on = getenv("DARLING_UNWIND_PROBE") != 0;
	if (!g_on) return;
	struct my_sa sa = { on_trap, 0, 0x40 };
	sigaction(5, &sa, 0);
	g_ours_installed = 1;
	_dyld_register_func_for_add_image(add_image);
	out("UNWPROBE init pid=%d find_sections_sym=%p register_frame=%p add_dynamic_fde=%p\n", (int)getpid(),
	    dlsym((void *)-2, "_dyld_find_unwind_sections"), dlsym((void *)-2, "__register_frame"), dlsym((void *)-2, "__unw_add_dynamic_fde"));
}
struct interpose { void *replacement, *replacee; };
__attribute__((used, section("__DATA,__interpose"))) static struct interpose interposers[] = {
	{ (void *)my_resume, (void *)_Unwind_Resume },
	{ (void *)my_raise, (void *)_Unwind_RaiseException },
	{ (void *)my_sigaction, (void *)sigaction },
};
