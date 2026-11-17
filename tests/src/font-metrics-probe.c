/* font-metrics-probe: the font metric accessors a browser's font code calls (NSFont and CoreText) must answer without
 * raising, never NULL, and with sane values (finite, > 0; leading may be 0 but not negative).
 * One line per check "FONTM-T <name> ok|BAD [detail]", then "FONTM-SUMMARY n=<checks> bad=<names or ->".
 * An ObjC exception that nobody catches is reported as the check it came from, with class and reason, then the summary. */
#include <stdio.h>
#include <string.h>
#include <setjmp.h>
#include <signal.h>
#include <math.h>
#include <stdlib.h>
#include <unistd.h>

typedef const void *R;
typedef long CFIndex;
extern void *objc_getClass(const char *);
extern void *sel_registerName(const char *);
extern void *objc_msgSend(void *, void *, ...);
extern int class_respondsToSelector(void *, void *);
extern void *object_getClass(void *);
extern const char *class_getName(void *);
extern void objc_setUncaughtExceptionHandler(void (*)(void *));
extern R CFStringCreateWithCString(R, const char *, unsigned);
extern long CFStringGetLength(R);
extern unsigned long CFGetTypeID(R), CFStringGetTypeID(void), CFDictionaryGetTypeID(void);
extern void CFRelease(R);
extern R CTFontCreateWithName(R, double, const void *);
extern R CTFontCopyFamilyName(R), CTFontCopyFullName(R), CTFontCopyPostScriptName(R);
extern R CTFontCopyTraits(R), CTFontCopyFontDescriptor(R);
extern double CTFontGetSize(R), CTFontGetAscent(R), CTFontGetDescent(R), CTFontGetCapHeight(R), CTFontGetLeading(R), CTFontGetXHeight(R);
extern unsigned CTFontGetUnitsPerEm(R);
#define S(x) sel_registerName(x)

static int n, bad;
static char names[800];
static const char *cur = "start";
static sigjmp_buf jb;
static volatile int armed;

static void add_bad(const char *name)
{
	bad++;
	if (strlen(names) + strlen(name) + 2 < sizeof names) {
		if (names[0]) strcat(names, ",");
		strcat(names, name);
	}
}
static void rep(const char *name, int ok, const char *detail)
{
	n++;
	printf("FONTM-T %s %s%s%s\n", name, ok ? "ok" : "BAD", detail && *detail ? " " : "", detail ? detail : "");
	if (!ok) add_bad(name);
	fflush(stdout);
}
static void summary(void)
{
	printf("FONTM-SUMMARY n=%d bad=%s\n", n, names[0] ? names : "-");
	fflush(stdout);
}
static void on_exc(void *exc)
{
	void *reason = 0;
	const char *cn = "?", *rs = "?";
	if (exc) {
		cn = class_getName(object_getClass(exc));
		if (class_respondsToSelector(object_getClass(exc), S("reason"))) reason = objc_msgSend(exc, S("reason"));
		if (reason && class_respondsToSelector(object_getClass(reason), S("UTF8String")))
			rs = (const char *)objc_msgSend(reason, S("UTF8String"));
	}
	printf("FONTM-EXC in=%s class=%s reason=%s\n", cur, cn, rs);
	n++; add_bad(cur);
	summary();
	_exit(1);
}
static void on_sig(int s) { if (armed) siglongjmp(jb, s); _exit(2); }

static int sane(double v, int allow_zero) { return isfinite(v) && (allow_zero ? v >= 0 : v > 0) && v < 1000.0; }
static int isstr(R o) { return o && CFGetTypeID(o) == CFStringGetTypeID() && CFStringGetLength(o) > 0; }

typedef double (*dfn)(void *, void *);
typedef void *(*ofn)(void *, void *);
typedef void *(*cfn_d)(void *, void *, double);
typedef void *(*cfn_nd)(void *, void *, void *, double);
typedef void *(*cfn_dd)(void *, void *, double, double);

static void check_font_obj(const char *tag, void *f)
{
	char nm[96], dt[96];
	static const char *dsel[] = { "ascender", "descender", "capHeight", "leading", "xHeight", "pointSize" };
	static const char *osel[] = { "fontName", "familyName", "displayName" };
	int i;
	snprintf(nm, sizeof nm, "%s.obj", tag);
	rep(nm, f != 0, "");
	if (!f) return;
	for (i = 0; i < 6; i++) {
		snprintf(nm, sizeof nm, "%s.%s", tag, dsel[i]);
		cur = nm; armed = 1;
		if (sigsetjmp(jb, 1)) { armed = 0; rep(nm, 0, "signal"); continue; }
		double v = ((dfn)objc_msgSend)(f, S(dsel[i]));
		armed = 0;
		snprintf(dt, sizeof dt, "v=%g", v);
		rep(nm, !strcmp(dsel[i], "descender") ? (v < 0 && sane(-v, 0)) : sane(v, !strcmp(dsel[i], "leading")), dt);
	}
	for (i = 0; i < 3; i++) {
		snprintf(nm, sizeof nm, "%s.%s", tag, osel[i]);
		cur = nm; armed = 1;
		if (sigsetjmp(jb, 1)) { armed = 0; rep(nm, 0, "signal"); continue; }
		void *o = ((ofn)objc_msgSend)(f, S(osel[i]));
		armed = 0;
		rep(nm, o != 0 && isstr(o), "");
	}
	snprintf(nm, sizeof nm, "%s.fontDescriptor", tag);
	cur = nm; armed = 1;
	if (sigsetjmp(jb, 1)) { armed = 0; rep(nm, 0, "signal"); }
	else { void *o = ((ofn)objc_msgSend)(f, S("fontDescriptor")); armed = 0; rep(nm, o != 0, ""); }
}

static void check_ct(const char *tag, const char *name, double size)
{
	char nm[96], dt[96];
	R s = CFStringCreateWithCString(0, name, 0x08000100);
	snprintf(nm, sizeof nm, "ct.%s.create", tag);
	cur = nm; armed = 1;
	if (sigsetjmp(jb, 1)) { armed = 0; rep(nm, 0, "signal"); return; }
	R f = CTFontCreateWithName(s, size, 0);
	armed = 0;
	rep(nm, f != 0, "");
	if (!f) return;
	struct { const char *n; double v; int z; } m[] = {
		{ "Size", CTFontGetSize(f), 0 }, { "Ascent", CTFontGetAscent(f), 0 }, { "Descent", CTFontGetDescent(f), 0 },
		{ "CapHeight", CTFontGetCapHeight(f), 0 }, { "Leading", CTFontGetLeading(f), 1 },
		{ "XHeight", CTFontGetXHeight(f), 0 }, { "UnitsPerEm", (double)CTFontGetUnitsPerEm(f), 0 } };
	int i;
	for (i = 0; i < 7; i++) {
		snprintf(nm, sizeof nm, "ct.%s.%s", tag, m[i].n);
		snprintf(dt, sizeof dt, "v=%g", m[i].v);
		rep(nm, sane(m[i].v, m[i].z) || (i == 6 && m[i].v >= 16), dt);
	}
	R (*cp[])(R) = { CTFontCopyFamilyName, CTFontCopyFullName, CTFontCopyPostScriptName };
	const char *cn[] = { "FamilyName", "FullName", "PostScriptName" };
	for (i = 0; i < 3; i++) {
		snprintf(nm, sizeof nm, "ct.%s.Copy%s", tag, cn[i]);
		cur = nm; armed = 1;
		if (sigsetjmp(jb, 1)) { armed = 0; rep(nm, 0, "signal"); continue; }
		R o = cp[i](f);
		armed = 0;
		rep(nm, isstr(o), "");
	}
	snprintf(nm, sizeof nm, "ct.%s.CopyTraits", tag);
	cur = nm; armed = 1;
	if (sigsetjmp(jb, 1)) { armed = 0; rep(nm, 0, "signal"); }
	else { R o = CTFontCopyTraits(f); armed = 0; rep(nm, o && CFGetTypeID(o) == CFDictionaryGetTypeID(), ""); }
	snprintf(nm, sizeof nm, "ct.%s.CopyFontDescriptor", tag);
	cur = nm; armed = 1;
	if (sigsetjmp(jb, 1)) { armed = 0; rep(nm, 0, "signal"); }
	else { R o = CTFontCopyFontDescriptor(f); armed = 0; rep(nm, o != 0, ""); }
}

int main(void)
{
	void *nsfont;
	signal(SIGSEGV, on_sig); signal(SIGBUS, on_sig); signal(SIGILL, on_sig); signal(SIGTRAP, on_sig);
	objc_setUncaughtExceptionHandler(on_exc);
	nsfont = objc_getClass("NSFont");
	if (getenv("FONTM_THROW")) { cur = "throw-selftest"; objc_msgSend(nsfont, S("noSuchSelectorXyz")); }
	rep("NSFont.class", nsfont != 0, "");
	if (nsfont) { char d[160]; snprintf(d,sizeof d,"meta-responds=%d inst-responds=%d cls=%p", class_respondsToSelector(object_getClass(nsfont), S("systemFontOfSize:")), class_respondsToSelector(nsfont, S("ascender")), nsfont); rep("NSFont.selectors", 1, d); }
	if (nsfont) {
		void *f;
		cur = "NSFont.systemFontOfSize:"; f = ((cfn_d)objc_msgSend)(nsfont, S("systemFontOfSize:"), 13.0);
		check_font_obj("system13", f);
		cur = "NSFont.boldSystemFontOfSize:"; f = ((cfn_d)objc_msgSend)(nsfont, S("boldSystemFontOfSize:"), 13.0);
		check_font_obj("bold13", f);
		cur = "NSFont.messageFontOfSize:"; f = ((cfn_d)objc_msgSend)(nsfont, S("messageFontOfSize:"), 12.0);
		check_font_obj("message12", f);
		cur = "NSFont.menuFontOfSize:"; f = ((cfn_d)objc_msgSend)(nsfont, S("menuFontOfSize:"), 14.0);
		check_font_obj("menu14", f);
		cur = "NSFont.userFixedPitchFontOfSize:"; f = ((cfn_d)objc_msgSend)(nsfont, S("userFixedPitchFontOfSize:"), 12.0);
		check_font_obj("fixed12", f);
		R nm = CFStringCreateWithCString(0, "Helvetica", 0x08000100);
		cur = "NSFont.fontWithName:size:"; f = ((cfn_nd)objc_msgSend)(nsfont, S("fontWithName:size:"), (void *)nm, 16.0);
		check_font_obj("helv16", f);
		nm = CFStringCreateWithCString(0, "Times", 0x08000100);
		cur = "NSFont.fontWithName:size:"; f = ((cfn_nd)objc_msgSend)(nsfont, S("fontWithName:size:"), (void *)nm, 11.0);
		check_font_obj("times11", f);
	}
	check_ct("helv13", "Helvetica", 13.0);
	check_ct("sys12", ".AppleSystemUIFont", 12.0);
	check_ct("times16", "Times", 16.0);
	check_ct("menlo11", "Menlo", 11.0);
	summary();
	printf("FONTM-PROBE DONE\n"); fflush(stdout);
	return 0;
}
