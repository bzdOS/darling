/* font-resolve-probe: which image answers CTFontCreateWithName for a flat lookup (dlsym RTLD_DEFAULT, what AppKitExtras font.c does), and
 * do CTFontCreateWithName("Helvetica",12) and +[NSFont systemFontOfSize:12] give an object. Run with darling-extras.dylib injected
 * (DARLING_INSERT_EXTRA), the way Chrome runs: the injected image comes first in the flat lookup order.
 * Output: "FONTRES-R ct=<nonnil|NIL> nsfont=<nonnil|NIL> img=<image of the resolved symbol> extras-loaded=<0|1>". */
#include <stdio.h>
#include <string.h>

typedef const void *R;
typedef struct { const char *dli_fname; void *dli_fbase; const char *dli_sname; void *dli_saddr; } dlinfo;
extern int dladdr(const void *, dlinfo *);
extern void *dlsym(void *, const char *);
extern unsigned _dyld_image_count(void);
extern const char *_dyld_get_image_name(unsigned);
extern void *objc_getClass(const char *);
extern void *sel_registerName(const char *);
extern void *objc_msgSend(void *, void *, ...);
extern R CFStringCreateWithCString(R, const char *, unsigned);

int main(void)
{
	void *sym = dlsym((void *)-2, "CTFontCreateWithName");
	dlinfo d;
	const char *img = "?";
	const char *s;
	R ct = 0, nf = 0;
	int extras = 0;
	unsigned i;
	if (sym && dladdr(sym, &d) && d.dli_fname) { s = strrchr(d.dli_fname, '/'); img = s ? s + 1 : d.dli_fname; }
	for (i = 0; i < _dyld_image_count(); i++)
		if (strstr(_dyld_get_image_name(i), "darling-extras")) extras = 1;
	if (sym) ct = ((R (*)(R, double, const void *))sym)(CFStringCreateWithCString(0, "Helvetica", 0x08000100), 12.0, 0);
	void *cls = objc_getClass("NSFont");
	if (cls) nf = ((R (*)(void *, void *, double))objc_msgSend)(cls, sel_registerName("systemFontOfSize:"), 12.0);
	printf("FONTRES-R ct=%s nsfont=%s img=%s extras-loaded=%d\n", ct ? "nonnil" : "NIL", nf ? "nonnil" : "NIL", img, extras);
	printf("FONTRES-DONE\n");
	fflush(stdout);
	return 0;
}
