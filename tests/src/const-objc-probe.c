/* const-objc-probe: every string constant exported by a stub image (const-objc-table.h, generated from the overlay)
 * must be a valid string object: CFGetTypeID == CFStringGetTypeID, [obj isKindOfClass:[NSString class]], [obj length]
 * == CFStringGetLength, without a crash. A constant that lives in the image itself (a compiled-in CFString) must also
 * have the class of a compiler emitted constant and an immortal retain count (CF and runtime retain/release, balanced
 * or not, never move it); a string a constructor made on the heap only has to be a valid string.
 * Three controls (a CFNumber, a zero filled block, a block whose first word is a heap address) must be rejected,
 * or the probe cannot tell and fails. Prints CONSTOBJC BAD lines and one CONST-OBJC-VERDICT line. */
#include <dlfcn.h>
#include <setjmp.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include "const-objc-table.h"

typedef unsigned long (*tid_fn)(void);
typedef unsigned long (*gtid_fn)(const void *);
typedef long (*len_fn)(const void *);
typedef long (*msgl_fn)(void *, void *);
typedef signed char (*msgb_fn)(void *, void *, void *);
typedef const void *(*rr_fn)(const void *);
typedef void (*rel_fn)(const void *);
typedef long (*rc_fn)(const void *);
typedef const void *(*mknum_fn)(const void *, long, const void *);
extern void *objc_getClass(const char *);
extern void *sel_registerName(const char *);
extern void *objc_msgSend(void *, void *);
extern void *object_getClass(void *);
extern const char *class_getName(void *);
extern void *objc_retain(void *);
extern void objc_release(void *);
extern void _exit(int);

static sigjmp_buf jb;
static volatile int armed;
static volatile int sigcaught;
static void onsig(int s) { if (armed) { sigcaught = s; siglongjmp(jb, 1); } _exit(128 + s); }

static gtid_fn gettid; static tid_fn strtid; static len_fn cflen;
static rr_fn cfretain; static rel_fn cfrelease; static rc_fn cfrc;
static void *nsstring, *s_kind, *s_len;
static const void *ref; static void *refcls;
static char badbuf[1024];
static int bad, n, zero, nbadbuf;

/* NULL when o is a valid string; else the reason. isstatic: o lives in the image that exports it. */
static const char *check(void *o, int isstatic, char *cn, int cnl)
{
	const char *why = 0;
	void *cls = object_getClass(o);
	snprintf(cn, cnl, "%s", cls ? class_getName(cls) : "(null)");
	if (gettid(o) != strtid()) return "typeid";
	if (!((msgb_fn)objc_msgSend)(o, s_kind, nsstring)) return "notkind";
	if (((msgl_fn)objc_msgSend)(o, s_len) != cflen(o)) return "length";
	if (isstatic) {
		if (cls != refcls) return "class-differs-from-compiler-constant";
		long rc0 = cfrc(o);
		cfretain(o); cfretain(o); cfrelease(o); cfrelease(o);
		objc_retain(o); objc_release(o);
		for (int k = 0; k < 4; k++) { cfrelease(o); objc_release(o); }
		if (cfrc(o) != rc0 || cfrc(o) != cfrc(ref)) why = "retain-count-moves";
		else if (((msgl_fn)objc_msgSend)(o, s_len) != cflen(o)) why = "length-after-release";
	}
	return why;
}

/* run check under the signal guard: a crash is a rejection too */
static const char *guarded(void *o, int isstatic, char *cn, int cnl)
{
	const char *why;
	armed = 1;
	if (sigsetjmp(jb, 1)) { armed = 0; return sigcaught == SIGABRT ? "abort" : "signal"; }
	why = check(o, isstatic, cn, cnl);
	armed = 0;
	return why;
}

static void fail(const char *img, const char *sym, const char *why)
{
	bad++;
	printf("CONSTOBJC BAD %s %s %s\n", img, sym, why); fflush(stdout);
	if (nbadbuf < 6) { int l = strlen(badbuf); snprintf(badbuf + l, sizeof badbuf - l, "%s%s:%s", l ? " " : "", sym, why); nbadbuf++; }
}

int main(void)
{
	signal(SIGSEGV, onsig); signal(SIGBUS, onsig); signal(SIGILL, onsig); signal(SIGABRT, onsig);
	{ extern char __CFInitialized; extern void __CFInitialize(void); if (!__CFInitialized) __CFInitialize(); }
	void *fnd = dlopen("/System/Library/Frameworks/Foundation.framework/Versions/C/Foundation", RTLD_NOW);
	if (!fnd) fnd = dlopen("/System/Library/Frameworks/Foundation.framework/Foundation", RTLD_NOW);
	void *cf = dlopen("/System/Library/Frameworks/CoreFoundation.framework/Versions/A/CoreFoundation", RTLD_NOW);
	if (cf) {
		gettid = (gtid_fn)dlsym(cf, "CFGetTypeID"); strtid = (tid_fn)dlsym(cf, "CFStringGetTypeID"); cflen = (len_fn)dlsym(cf, "CFStringGetLength");
		cfretain = (rr_fn)dlsym(cf, "CFRetain"); cfrelease = (rel_fn)dlsym(cf, "CFRelease"); cfrc = (rc_fn)dlsym(cf, "CFGetRetainCount");
	}
	nsstring = objc_getClass("NSString");
	s_kind = sel_registerName("isKindOfClass:"); s_len = sel_registerName("length");
	ref = (const void *)__builtin___CFStringMakeConstantString("const-objc-probe-reference");   /* what the compiler emits */
	refcls = object_getClass((void *)ref);
	printf("CONSTOBJC start foundation=%p cf=%p NSString=%p reference-class=%s\n", fnd, cf, nsstring, class_getName(refcls)); fflush(stdout);
	if (!gettid || !strtid || !cflen || !cfretain || !cfrelease || !cfrc || !nsstring) {
		printf("CONST-OBJC-VERDICT FAIL n=0 bad=1 zero=0 [setup:foundation=%d cf=%d NSString=%d]\n", fnd != 0, cf != 0, nsstring != 0);
		printf("CONSTOBJC DONE\n"); return 1;
	}
	/* controls: each must be rejected */
	{
		char cn[128]; int ctl = 0;
		static void *zeroblk[8], *heapblk[8];
		heapblk[0] = (void *)0x13800100b00UL;
		mknum_fn mknum = (mknum_fn)dlsym(cf, "CFNumberCreate"); int one = 1;
		void *num = (void *)mknum(0, 9, &one);
		void *ctls[3] = { num, zeroblk, heapblk };
		for (int i = 0; i < 3; i++) { const char *w = guarded(ctls[i], 0, cn, sizeof cn); printf("CONSTOBJC control %d %s\n", i, w ? w : "ACCEPTED"); if (w) ctl++; }
		if (ctl != 3) { printf("CONST-OBJC-VERDICT FAIL n=0 bad=1 zero=0 [control-not-rejected:%d/3]\n", ctl); printf("CONSTOBJC DONE\n"); return 1; }
	}
	void *lasth = 0; const char *lastimg = "";
	for (int i = 0; tab[i].image; i++) {
		const char *img = tab[i].image, *sym = tab[i].sym;
		if (!lasth || strcmp(lastimg, img)) { lasth = dlopen(img, RTLD_NOW); lastimg = img; if (!lasth) printf("CONSTOBJC dlopen %s: %s\n", img, dlerror()); }
		if (!lasth) { if (tab[i].nonzero) { n++; fail(img, sym, "dlopen"); } continue; }
		void **p = dlsym(lasth, sym);
		if (!p) { if (tab[i].nonzero) { n++; fail(img, sym, "dlsym"); } continue; }
		void *o = *p;
		if (!o) { if (tab[i].nonzero) { n++; fail(img, sym, "null"); } else zero++; continue; }
		n++;
		printf("CONSTOBJC chk %s %s\n", img, sym); fflush(stdout);
		Dl_info di; int isstatic = dladdr(o, &di) && di.dli_fname && !strcmp(di.dli_fname, img);
		char cn[128] = "?";
		const char *why = guarded(o, isstatic, cn, sizeof cn);
		if (why) {
			void *isa = *(void **)o; const char *isym = "-", *iimg = "-";
			if (dladdr(isa, &di)) { if (di.dli_sname) isym = di.dli_sname; if (di.dli_fname) iimg = di.dli_fname; }
			char w[400]; snprintf(w, sizeof w, "%s(class=%s isa=%s+%s static=%d)", why, cn, iimg, isym, isstatic); fail(img, sym, w);
		}
	}
	printf("CONST-OBJC-VERDICT %s n=%d bad=%d zero=%d [%s]\n", bad ? "FAIL" : "pass", n, bad, zero, badbuf);
	printf("CONSTOBJC DONE\n"); fflush(stdout);
	return bad ? 1 : 0;
}
