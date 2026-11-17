/* AppKitExtras behaviour: NSFont answers real fonts and metrics.
 *
 * The AppKit stub class NSFont had no methods: +systemFontOfSize: and +fontWithName:size: answered nil, and a browser's font
 * code then went on with a missing font (ascender, descender, capHeight, leading read from nothing; the cast to CTFontRef of
 * a nil font fed the CoreText calls NULL). Here an NSFont instance wraps a CoreText CTFont (CoreText behaviour: a CFDictionary
 * holding name and size) kept in the bytes after the instance; the CoreText functions of this tree unwrap an NSFont given
 * where a CTFontRef is expected. Installed (replacing the stub's no-op answer when it has one):
 *   +systemFontOfSize: boldSystemFontOfSize: messageFontOfSize: menuFontOfSize: labelFontOfSize: userFontOfSize:
 *   userFixedPitchFontOfSize: controlContentFontOfSize: toolTipsFontOfSize: titleBarFontOfSize: paletteFontOfSize:
 *   systemFontOfSize:weight: monospacedDigitSystemFontOfSize:weight: fontWithName:size: fontWithDescriptor:size:
 *   +systemFontSize smallSystemFontSize labelFontSize buttonFontSize
 *   -ascender descender capHeight xHeight leading pointSize italicAngle underlinePosition underlineThickness numberOfGlyphs
 *   -fontName familyName displayName fontDescriptor isFixedPitch isVertical screenFont printerFont copyWithZone:
 * The Wayland AppKit backend bundle (loaded later, class WaylandDisplay) does not override NSDisplay -allFontFamilyNames and
 * -fontTypefacesForFamilyName:, so the first NSFontFamily build raised "only defined for abstract class". Once WaylandDisplay
 * exists (checked at every image load) both are installed on it: the family set comes from CTFontManagerCopyAvailableFontFamilyNames
 * (Helvetica when empty), one NSFontTypeface "Regular" per family. */
typedef void *Class;
typedef void *SEL;
typedef void *IMP;
typedef void *id;
extern Class objc_getClass(const char *name);
extern Class object_getClass(id obj);
extern SEL sel_registerName(const char *name);
extern IMP class_replaceMethod(Class cls, SEL name, IMP imp, const char *types);
extern unsigned char class_addMethod(Class cls, SEL name, IMP imp, const char *types);
extern id class_createInstance(Class cls, unsigned long extra);
extern unsigned long class_getInstanceSize(Class cls);
extern id objc_msgSend(id, SEL, ...);
extern void *dlsym(void *, const char *);
extern const void *CFRetain(const void *);
extern unsigned char CFStringGetCString(const void *, char *, long, unsigned);
extern const void *CFStringCreateWithCString(const void *, const char *, unsigned);
struct mach_header;
extern void _dyld_register_func_for_add_image(void (*func)(const struct mach_header *, long));
#define S(x) sel_registerName(x)
#define RTLD_DEFAULT ((void *)-2)
#define UTF8 0x08000100

static Class nsfont;

static void *ct(const char *sym) { return dlsym(RTLD_DEFAULT, sym); }
static const void *cfs(const char *s) { return CFStringCreateWithCString(0, s, UTF8); }
static const void *newct(const void *name, double size)
{
    const void *(*mk)(const void *, double, const void *) = ct("CTFontCreateWithName");
    return mk ? mk(name, size > 0 ? size : 13.0, 0) : 0;
}
static id wrap(const void *font)
{
    id o;
    if (!font || !nsfont) return 0;
    o = class_createInstance(nsfont, sizeof(void *));
    if (o) *(const void **)((char *)o + class_getInstanceSize(nsfont)) = font;
    return o;
}
static const void *inner(id self) { return *(const void **)((char *)self + class_getInstanceSize(nsfont)); }
static double metric(id self, const char *sym, double dflt, double scale)
{
    double (*f)(const void *) = ct(sym);
    return f ? f(inner(self)) * scale : dflt;
}

static id by_name(const char *name, double size) { return wrap(newct(cfs(name), size)); }
static id c_system(id s, SEL c, double sz) { return by_name("Helvetica", sz); }
static id c_bold(id s, SEL c, double sz) { return by_name("Helvetica-Bold", sz); }
static id c_fixed(id s, SEL c, double sz) { return by_name("Menlo-Regular", sz); }
static id c_system_w(id s, SEL c, double sz, double w) { return by_name(w >= 0.4 ? "Helvetica-Bold" : "Helvetica", sz); }
static id c_name(id s, SEL c, id name, double sz)
{
    char b[256];
    if (name && ct("CFStringGetCString") && CFStringGetCString(name, b, sizeof b, UTF8) && b[0]) return by_name(b, sz);
    return by_name("Helvetica", sz);
}
static id c_desc(id s, SEL c, id desc, double sz)
{
    const void *(*mk)(const void *, double, const void *) = ct("CTFontCreateWithFontDescriptor");
    return wrap(mk && desc ? mk(desc, sz > 0 ? sz : 13.0, 0) : newct(cfs("Helvetica"), sz));
}
static double sz13(id s, SEL c) { return 13.0; }
static double sz11(id s, SEL c) { return 11.0; }
static double sz10(id s, SEL c) { return 10.0; }

static double i_asc(id s, SEL c) { return metric(s, "CTFontGetAscent", 12.0, 1.0); }
static double i_desc(id s, SEL c) { return -metric(s, "CTFontGetDescent", 3.0, 1.0); }
static double i_cap(id s, SEL c) { return metric(s, "CTFontGetCapHeight", 9.0, 1.0); }
static double i_xh(id s, SEL c) { return metric(s, "CTFontGetXHeight", 6.0, 1.0); }
static double i_lead(id s, SEL c) { return metric(s, "CTFontGetLeading", 0.0, 1.0); }
static double i_size(id s, SEL c) { return metric(s, "CTFontGetSize", 13.0, 1.0); }
static double i_ital(id s, SEL c) { return -metric(s, "CTFontGetSlantAngle", 0.0, 1.0); }
static double i_ulp(id s, SEL c) { return metric(s, "CTFontGetUnderlinePosition", -1.0, 1.0); }
static double i_ult(id s, SEL c) { return metric(s, "CTFontGetUnderlineThickness", 1.0, 1.0); }
static unsigned long i_glyphs(id s, SEL c)
{
    long (*f)(const void *) = ct("CTFontGetGlyphCount");
    return f ? (unsigned long)f(inner(s)) : 256;
}
static id i_copy_str(id s, const char *sym)
{
    const void *(*f)(const void *) = ct(sym);
    return f ? (id)f(inner(s)) : (id)cfs("Helvetica");
}
static id i_fontName(id s, SEL c) { return i_copy_str(s, "CTFontCopyPostScriptName"); }
static id i_family(id s, SEL c) { return i_copy_str(s, "CTFontCopyFamilyName"); }
static id i_display(id s, SEL c) { return i_copy_str(s, "CTFontCopyFullName"); }
static id i_desc_obj(id s, SEL c) { return i_copy_str(s, "CTFontCopyFontDescriptor"); }
static unsigned char i_fixed(id s, SEL c)
{
    char b[128];
    return CFStringGetCString(i_fontName(s, c), b, sizeof b, UTF8) &&
           (__builtin_strstr(b, "Menlo") || __builtin_strstr(b, "Courier") || __builtin_strstr(b, "Monaco") || __builtin_strstr(b, "Mono"));
}
static unsigned char i_no(id s, SEL c) { return 0; }
static id i_self(id s, SEL c) { return s; }
static id i_copy(id s, SEL c, void *zone) { return objc_msgSend(s, S("retain")); }

#define CM(sel, fn, types) class_replaceMethod(object_getClass(nsfont), S(sel), (IMP)fn, types)
#define IM(sel, fn, types) class_replaceMethod(nsfont, S(sel), (IMP)fn, types)
static int done;
static void try_add(void)
{
    if (done) return;
    nsfont = objc_getClass("NSFont");
    if (!nsfont) return;
    CM("systemFontOfSize:", c_system, "@24@0:8d16");
    CM("boldSystemFontOfSize:", c_bold, "@24@0:8d16");
    CM("messageFontOfSize:", c_system, "@24@0:8d16");
    CM("menuFontOfSize:", c_system, "@24@0:8d16");
    CM("menuBarFontOfSize:", c_system, "@24@0:8d16");
    CM("labelFontOfSize:", c_system, "@24@0:8d16");
    CM("userFontOfSize:", c_system, "@24@0:8d16");
    CM("controlContentFontOfSize:", c_system, "@24@0:8d16");
    CM("toolTipsFontOfSize:", c_system, "@24@0:8d16");
    CM("titleBarFontOfSize:", c_system, "@24@0:8d16");
    CM("paletteFontOfSize:", c_system, "@24@0:8d16");
    CM("userFixedPitchFontOfSize:", c_fixed, "@24@0:8d16");
    CM("systemFontOfSize:weight:", c_system_w, "@32@0:8d16d24");
    CM("monospacedDigitSystemFontOfSize:weight:", c_system_w, "@32@0:8d16d24");
    CM("fontWithName:size:", c_name, "@32@0:8@16d24");
    CM("fontWithDescriptor:size:", c_desc, "@32@0:8@16d24");
    CM("systemFontSize", sz13, "d16@0:8");
    CM("buttonFontSize", sz13, "d16@0:8");
    CM("smallSystemFontSize", sz11, "d16@0:8");
    CM("labelFontSize", sz10, "d16@0:8");
    IM("ascender", i_asc, "d16@0:8");
    IM("descender", i_desc, "d16@0:8");
    IM("capHeight", i_cap, "d16@0:8");
    IM("xHeight", i_xh, "d16@0:8");
    IM("leading", i_lead, "d16@0:8");
    IM("pointSize", i_size, "d16@0:8");
    IM("italicAngle", i_ital, "d16@0:8");
    IM("underlinePosition", i_ulp, "d16@0:8");
    IM("underlineThickness", i_ult, "d16@0:8");
    IM("numberOfGlyphs", i_glyphs, "Q16@0:8");
    IM("fontName", i_fontName, "@16@0:8");
    IM("familyName", i_family, "@16@0:8");
    IM("displayName", i_display, "@16@0:8");
    IM("fontDescriptor", i_desc_obj, "@16@0:8");
    IM("isFixedPitch", i_fixed, "c16@0:8");
    IM("isVertical", i_no, "c16@0:8");
    IM("screenFont", i_self, "@16@0:8");
    IM("printerFont", i_self, "@16@0:8");
    IM("copyWithZone:", i_copy, "@24@0:8^v16");
    done = 1;
}
static id msg0(id o, const char *s) { return objc_msgSend(o, S(s)); }
static id c_families(id self, SEL c)
{
    id set = msg0(msg0((id)objc_getClass("NSMutableSet"), "alloc"), "init");
    const void *(*copy)(void) = ct("CTFontManagerCopyAvailableFontFamilyNames");
    long (*cnt)(const void *) = ct("CFArrayGetCount");
    const void *(*at)(const void *, long) = ct("CFArrayGetValueAtIndex");
    void (*rel)(const void *) = ct("CFRelease");
    const void *arr = copy && cnt && at ? copy() : 0;
    if (arr) {
        long i, n = cnt(arr);
        Class sc = objc_getClass("NSString");
        for (i = 0; i < n; i++) {
            id name = (id)at(arr, i);
            if (name && ((unsigned char (*)(id, SEL, Class))objc_msgSend)(name, S("isKindOfClass:"), sc))
                objc_msgSend(set, S("addObject:"), name);
        }
        if (rel) rel(arr);
    }
    if (((unsigned long (*)(id, SEL))objc_msgSend)(set, S("count")) == 0) objc_msgSend(set, S("addObject:"), (id)cfs("Helvetica"));
    return msg0(set, "autorelease");
}
static id c_typefaces(id self, SEL c, id name)
{
    id f = objc_msgSend((id)objc_getClass("NSFontTypeface"), S("alloc"));
    f = ((id (*)(id, SEL, id, id, unsigned))objc_msgSend)(f, S("initWithName:traitName:traits:"), name, (id)cfs("Regular"), 0);
    return objc_msgSend((id)objc_getClass("NSArray"), S("arrayWithObject:"), msg0(f, "autorelease"));
}
static int wl_done;
static void try_wayland(void)
{
    Class wd;
    if (wl_done) return;
    wd = objc_getClass("WaylandDisplay");
    if (!wd) return;
    class_replaceMethod(wd, S("allFontFamilyNames"), (IMP)c_families, "@16@0:8");
    class_replaceMethod(wd, S("fontTypefacesForFamilyName:"), (IMP)c_typefaces, "@24@0:8@16");
    wl_done = 1;
}
static void on_image(const struct mach_header *mh, long slide) { (void)mh; (void)slide; try_add(); try_wayland(); }

__attribute__((constructor)) static void appkit_extras_font(void)
{
    try_add();
    try_wayland();
    _dyld_register_func_for_add_image(on_image);
}
