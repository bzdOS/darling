/* appkit-census-probe: -[NSWindow isOnActiveSpace], -[NSWindow collectionBehavior]/setCollectionBehavior:,
 * +[NSScroller preferredScrollerStyle] and -[NSView sortSubviewsUsingFunction:context:] must work (they were
 * NSUnimplementedMethod stubs). Prints AKC <step> ok|FAIL. */
typedef void *id; typedef void *SEL; typedef void *Class;
extern int printf(const char *, ...);
extern int fflush(void *);
extern id objc_getClass(const char *);
extern SEL sel_registerName(const char *);
extern id objc_msgSend(id, SEL, ...);
#define S(x) sel_registerName(x)
static int bad;
static void res(const char *n, int ok) { printf("AKC %s %s\n", n, ok ? "ok" : "FAIL"); fflush(0); if (!ok) bad++; }
static long desc(id a, id b, void *c) { (void)c; return (long)a < (long)b ? 1 : ((long)a > (long)b ? -1 : 0); }
int main(void)
{
	id win = objc_msgSend(objc_msgSend(objc_getClass("NSWindow"), S("alloc")), S("self"));
	res("isOnActiveSpace", (unsigned char)(long)objc_msgSend(win, S("isOnActiveSpace")) == 1);
	((void (*)(id, SEL, unsigned long))objc_msgSend)(win, S("setCollectionBehavior:"), 0x41);
	res("collectionBehavior", (unsigned long)objc_msgSend(win, S("collectionBehavior")) == 0x41);
	res("preferredScrollerStyle", (long)objc_msgSend(objc_getClass("NSScroller"), S("preferredScrollerStyle")) == 0);
	id v = objc_msgSend(objc_msgSend(objc_getClass("NSView"), S("alloc")), S("init"));
	id a = objc_msgSend(objc_msgSend(objc_getClass("NSView"), S("alloc")), S("init"));
	id b = objc_msgSend(objc_msgSend(objc_getClass("NSView"), S("alloc")), S("init"));
	id c = objc_msgSend(objc_msgSend(objc_getClass("NSView"), S("alloc")), S("init"));
	objc_msgSend(v, S("addSubview:"), a); objc_msgSend(v, S("addSubview:"), b); objc_msgSend(v, S("addSubview:"), c);
	((void (*)(id, SEL, void *, void *))objc_msgSend)(v, S("sortSubviewsUsingFunction:context:"), (void *)desc, 0);
	id subs = objc_msgSend(v, S("subviews"));
	int ok = 1; long prev = 0x7fffffffffffffffL, n = (long)objc_msgSend(subs, S("count"));
	for (long i = 0; i < n; i++) { long p = (long)objc_msgSend(subs, S("objectAtIndex:"), i); if (p > prev) ok = 0; prev = p; }
	res("sortSubviews", ok && n == 3);
	printf("AKC DONE fail=%d\n", bad); fflush(0);
	return bad != 0;
}
