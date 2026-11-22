/* fwdmiss-probe: API that Chrome's startup uses and that was missing or empty here, each checked outside Chrome.
 * Prints FWDM <name> ok|FAIL. Linked against Foundation and the extras images (they carry the behaviours).
 *   dock          -[NSUserDefaults persistentDomainForName:@"com.apple.dock"] is an NSDictionary (chrome/browser/mac/dock.mm)
 *   ubiquity      -[NSFileManager ubiquityIdentityToken] is implemented and answers nil
 *   utype         +[UTType typeWithMIMEType:] is implemented
 *   constdict-enc -[NSConstantDictionary _encodingCantBeStoredInEightBitCFString] is implemented (NO)
 *   constdict-chr -[NSConstantDictionary getCharacters:range:] is implemented and fills the buffer (zero characters) */
typedef void *id; typedef void *SEL; typedef void *Class;
extern int printf(const char *, ...);
extern int fflush(void *);
extern void *calloc(unsigned long, unsigned long);
extern id objc_getClass(const char *);
extern SEL sel_registerName(const char *);
extern id objc_msgSend(id, SEL, ...);
extern Class object_getClass(id);
extern unsigned char class_respondsToSelector(Class, SEL);
extern id CFStringCreateWithCString(void *, const char *, unsigned);
static int bad;
static void res(const char *n, int ok) { printf("FWDM %s %s\n", n, ok ? "ok" : "FAIL"); fflush(0); if (!ok) bad++; }
#define S(x) sel_registerName(x)
#define MSG(r, ...) ((r (*)(id, SEL, ...))objc_msgSend)
int main(void)
{
	id str = CFStringCreateWithCString(0, "com.apple.dock", 0x08000100);
	id ud = objc_msgSend(objc_getClass("NSUserDefaults"), S("standardUserDefaults"));
	id dom = ud ? objc_msgSend(ud, S("persistentDomainForName:"), str) : 0;
	printf("FWDM info ud=%p responds=%d dom=%p\n", ud, ud ? class_respondsToSelector(object_getClass(ud), S("persistentDomainForName:")) : -1, dom);
	res("dock", dom && (unsigned char)(long)objc_msgSend(dom, S("isKindOfClass:"), objc_getClass("NSDictionary")));

	{
		id k = CFStringCreateWithCString(0, "persistent-apps", 0x08000100), k2 = CFStringCreateWithCString(0, "persistent-others", 0x08000100);
		id a = dom ? objc_msgSend(dom, S("objectForKey:"), k) : 0, b = dom ? objc_msgSend(dom, S("objectForKey:"), k2) : 0;
		res("dock-arrays", a && b && (unsigned char)(long)objc_msgSend(a, S("isKindOfClass:"), objc_getClass("NSArray")) &&
			(unsigned char)(long)objc_msgSend(b, S("isKindOfClass:"), objc_getClass("NSArray")));
	}
	id fm = objc_msgSend(objc_getClass("NSFileManager"), S("defaultManager"));
	res("ubiquity", fm && class_respondsToSelector(object_getClass(fm), S("ubiquityIdentityToken")) && !objc_msgSend(fm, S("ubiquityIdentityToken")));

	id ut = objc_getClass("UTType");
	id mime = CFStringCreateWithCString(0, "text/plain", 0x08000100);
	res("utype", ut && class_respondsToSelector(object_getClass(ut), S("typeWithMIMEType:")));
	if (ut) objc_msgSend(ut, S("typeWithMIMEType:"), mime);

	id cd = objc_getClass("NSConstantDictionary");
	void **rec = calloc(5, sizeof(void *));
	rec[0] = cd;
	res("constdict-enc", cd && class_respondsToSelector(cd, S("_encodingCantBeStoredInEightBitCFString")) ? 1 : 0);
	res("constdict-enc-answer", cd && !(unsigned char)(long)objc_msgSend((id)rec, S("_encodingCantBeStoredInEightBitCFString")));
	unsigned short buf[4] = { 0x5a5a, 0x5a5a, 0x5a5a, 0x5a5a };
	struct { long loc, len; } rg = { 0, 3 };
	res("constdict-chr", cd && class_respondsToSelector(cd, S("getCharacters:range:")) ? 1 : 0);
	if (cd) ((void (*)(id, SEL, unsigned short *, long, long))objc_msgSend)((id)rec, S("getCharacters:range:"), buf, rg.loc, rg.len);
	res("constdict-chr-filled", buf[0] == 0 && buf[2] == 0 && buf[3] == 0x5a5a);
	printf("FWDM DONE fail=%d\n", bad); fflush(0);
	return bad != 0;
}
