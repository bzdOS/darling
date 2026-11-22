/* nsscreen-probe: NSScreen identity must be stable and match CGMainDisplayID; NSWindow backingScaleFactor/occlusionState exist.
 * Prints NSS <step> ok|FAIL. */
typedef void *id; typedef void *SEL; typedef void *Class;
extern int printf(const char *, ...);
extern int fflush(void *);
extern id objc_getClass(const char *);
extern SEL sel_registerName(const char *);
extern id objc_msgSend(id, SEL, ...);
extern unsigned CGMainDisplayID(void);
#define S(x) sel_registerName(x)
static int bad;
static void res(const char *n, int ok) { printf("NSS %s %s\n", n, ok ? "ok" : "FAIL"); fflush(0); if (!ok) bad++; }
typedef struct { double x, y, w, h; } Rc;
int main(void)
{
	id cls = objc_getClass("NSScreen");
	id a = objc_msgSend(cls, S("screens"));
	id s0 = objc_msgSend(a, S("objectAtIndex:"), 0);
	id b = objc_msgSend(cls, S("screens"));
	id s0b = objc_msgSend(b, S("objectAtIndex:"), 0);
	res("screens-stable", s0 && s0 == s0b);
	res("mainScreen", objc_msgSend(cls, S("mainScreen")) == s0);
	id dd = objc_msgSend(s0, S("deviceDescription"));
	id num = dd ? objc_msgSend(dd, S("objectForKey:"), objc_msgSend(objc_getClass("NSString"), S("stringWithUTF8String:"), "NSScreenNumber")) : 0;
	unsigned n = num ? (unsigned)(long)objc_msgSend(num, S("unsignedIntValue")) : 0;
	printf("NSS info screenNumber=%u CGMainDisplayID=%u\n", n, CGMainDisplayID()); fflush(0);
	res("screenNumber", n != 0 && n == CGMainDisplayID());
	id win = objc_msgSend(objc_msgSend(objc_getClass("NSWindow"), S("alloc")), S("self"));
	res("window-screen", objc_msgSend(win, S("screen")) == s0);
	double (*dbl)(id, SEL) = (double (*)(id, SEL))objc_msgSend;
	res("window-backingScaleFactor", dbl(win, S("backingScaleFactor")) == 1.0);
	res("window-occlusionState", ((unsigned long)objc_msgSend(win, S("occlusionState")) & 2) == 2);
	printf("NSS DONE fail=%d\n", bad); fflush(0);
	return bad != 0;
}
