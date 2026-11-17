/* CoreText behaviour (CoreText and CoreTextExtras): minimal real objects instead of NULL.
 *
 * The generated stub image answered NULL from every Create/Copy function; a browser reads the answer as an object
 * (CFDictionaryGetValue(CTFontCopyTraits(font), key) crashed at address 0). Here a CTFont is a CFDictionary holding
 * the name and size, a CTFontDescriptor a CFDictionary of attributes (the real NS* attribute key strings), a font
 * collection a CFArray of descriptors. Every CTFont and CTFontDescriptor call of this file accepts those objects, and a
 * NULL or foreign argument gets the default font. Metrics are proportional to the size (a Helvetica-like face).
 * Not a text engine: glyph ids are the character codes, advances half the size.
 * Only CoreFoundation symbols are referenced directly (flat lookup); CoreGraphics ones through dlsym when present. */
typedef const void *CFTypeRef;
typedef const void *CFStringRef;
typedef const void *CFDictionaryRef;
typedef void *CFMutableDictionaryRef;
typedef const void *CFArrayRef;
typedef const void *CFAllocatorRef;
typedef const void *CFNumberRef;
typedef long CFIndex;
typedef unsigned char Boolean;
typedef unsigned int CFStringEncoding;
typedef unsigned short UniChar;
typedef unsigned short CGGlyph;
typedef double CGFloat;
typedef struct { CGFloat x, y, w, h; } Rect;
typedef struct { CFIndex location, length; } CFRange;

extern const void *kCFTypeDictionaryKeyCallBacks, *kCFTypeDictionaryValueCallBacks, *kCFTypeArrayCallBacks;
extern CFMutableDictionaryRef CFDictionaryCreateMutable(CFAllocatorRef, CFIndex, const void *, const void *);
extern CFDictionaryRef CFDictionaryCreateCopy(CFAllocatorRef, CFDictionaryRef);
extern CFMutableDictionaryRef CFDictionaryCreateMutableCopy(CFAllocatorRef, CFIndex, CFDictionaryRef);
extern void CFDictionarySetValue(CFMutableDictionaryRef, const void *, const void *);
extern const void *CFDictionaryGetValue(CFDictionaryRef, const void *);
extern unsigned long CFGetTypeID(CFTypeRef);
extern unsigned long CFDictionaryGetTypeID(void);
extern unsigned long CFStringGetTypeID(void);
extern CFTypeRef CFRetain(CFTypeRef);
extern CFNumberRef CFNumberCreate(CFAllocatorRef, long, const void *);
extern Boolean CFNumberGetValue(CFNumberRef, long, void *);
extern CFStringRef CFStringCreateWithCString(CFAllocatorRef, const char *, CFStringEncoding);
extern Boolean CFStringGetCString(CFStringRef, char *, CFIndex, CFStringEncoding);
extern CFArrayRef CFArrayCreate(CFAllocatorRef, const void **, CFIndex, const void *);
extern const void *CFCharacterSetCreateWithCharactersInRange(CFAllocatorRef, CFRange);
extern void *dlsym(void *, const char *);
#define UTF8 0x08000100
#define RTLD_DEFAULT ((void *)-2)
#define CFSTR(s) ((CFStringRef)__builtin___CFStringMakeConstantString(s))
#define K(var, text) CFStringRef var = CFSTR(text)

/* the attribute and trait keys (the values the real framework uses) */
K(kCTFontNameAttribute, "NSFontNameAttribute");
K(kCTFontDisplayNameAttribute, "NSFontVisibleNameAttribute");
K(kCTFontFamilyNameAttribute, "NSFontFamilyAttribute");
K(kCTFontStyleNameAttribute, "NSFontFaceAttribute");
K(kCTFontSizeAttribute, "NSFontSizeAttribute");
K(kCTFontTraitsAttribute, "NSCTFontTraitsAttribute");
K(kCTFontSymbolicTrait, "NSCTFontSymbolicTrait");
K(kCTFontWeightTrait, "NSCTFontWeightTrait");
K(kCTFontWidthTrait, "NSCTFontWidthTrait");
K(kCTFontSlantTrait, "NSCTFontSlantTrait");

static CFStringRef kFontName, kFontSize;   /* private keys of the CTFont dictionary */
static CFStringRef cstr(const char *s) { return CFStringCreateWithCString(0, s, UTF8); }
static void init(void)
{
    if (!kFontName) { kFontName = cstr("CTFakeFontName"); kFontSize = cstr("CTFakeFontSize"); }
}
static CFMutableDictionaryRef newdict(void)
{
    init();
    return CFDictionaryCreateMutable(0, 0, kCFTypeDictionaryKeyCallBacks, kCFTypeDictionaryValueCallBacks);
}
static CFNumberRef dnum(double v) { return CFNumberCreate(0, 6 /* kCFNumberDoubleType */, &v); }
static CFNumberRef inum(int v) { return CFNumberCreate(0, 3 /* kCFNumberSInt32Type */, &v); }
static int isdict(CFTypeRef o) { return o && CFGetTypeID(o) == CFDictionaryGetTypeID(); }

static CFStringRef font_name(CFTypeRef font)
{
    CFStringRef n = 0;
    init();
    if (isdict(font)) {
        n = CFDictionaryGetValue(font, kFontName);
        if (!n) n = CFDictionaryGetValue(font, kCTFontNameAttribute);
    }
    return n && CFGetTypeID(n) == CFStringGetTypeID() ? n : CFSTR("Helvetica");
}
static double font_size(CFTypeRef font)
{
    double v = 0;
    CFNumberRef n;
    init();
    if (isdict(font) && (n = CFDictionaryGetValue(font, kFontSize)) && CFNumberGetValue(n, 6, &v) && v > 0)
        return v;
    return 12.0;
}
static CFStringRef family_of(CFStringRef name)
{
    char b[256];
    char *d;
    if (!CFStringGetCString(name, b, sizeof b, UTF8)) return CFSTR("Helvetica");
    if ((d = __builtin_strchr(b, '-'))) *d = 0;
    return cstr(b);
}
static CFMutableDictionaryRef make_font(CFStringRef name, double size)
{
    CFMutableDictionaryRef f = newdict();
    CFNumberRef n = dnum(size > 0 ? size : 12.0);
    CFDictionarySetValue(f, kFontName, name ? name : CFSTR("Helvetica"));
    CFDictionarySetValue(f, kFontSize, n);
    return f;
}
static CFDictionaryRef make_traits(void)
{
    CFMutableDictionaryRef t = newdict();
    CFDictionarySetValue(t, kCTFontSymbolicTrait, inum(0));
    CFDictionarySetValue(t, kCTFontWeightTrait, dnum(0));
    CFDictionarySetValue(t, kCTFontWidthTrait, dnum(0));
    CFDictionarySetValue(t, kCTFontSlantTrait, dnum(0));
    return t;
}
static CFMutableDictionaryRef make_desc(CFStringRef name, double size)
{
    CFMutableDictionaryRef d = newdict();
    CFDictionarySetValue(d, kCTFontNameAttribute, name);
    CFDictionarySetValue(d, kCTFontDisplayNameAttribute, name);
    CFDictionarySetValue(d, kCTFontFamilyNameAttribute, family_of(name));
    CFDictionarySetValue(d, kCTFontStyleNameAttribute, CFSTR("Regular"));
    if (size > 0) CFDictionarySetValue(d, kCTFontSizeAttribute, dnum(size));
    CFDictionarySetValue(d, kCTFontTraitsAttribute, make_traits());
    return d;
}
static CFArrayRef empty_array(void) { return CFArrayCreate(0, 0, 0, kCFTypeArrayCallBacks); }
static CFArrayRef one_array(CFTypeRef o) { const void *v[1]; v[0] = o; return CFArrayCreate(0, v, 1, kCFTypeArrayCallBacks); }

/* ---- font creation ---- */
CFTypeRef CTFontCreateWithName(CFStringRef name, CGFloat size, const void *matrix)
{
    return make_font(name && CFGetTypeID(name) == CFStringGetTypeID() ? name : CFSTR("Helvetica"), size > 0 ? size : 12.0);
}
CFTypeRef CTFontCreateWithFontDescriptor(CFTypeRef desc, CGFloat size, const void *matrix)
{
    CFStringRef n = isdict(desc) ? CFDictionaryGetValue(desc, kCTFontNameAttribute) : 0;
    CFNumberRef sz = isdict(desc) ? CFDictionaryGetValue(desc, kCTFontSizeAttribute) : 0;
    double v = 0;
    if (size <= 0 && sz) CFNumberGetValue(sz, 6, &v); else v = size;
    return make_font(n && CFGetTypeID(n) == CFStringGetTypeID() ? n : CFSTR("Helvetica"), v);
}
CFTypeRef CTFontCreateUIFontForLanguage(int uiType, CGFloat size, CFStringRef language)
{
    return make_font(CFSTR("Helvetica"), size > 0 ? size : 13.0);
}
CFTypeRef CTFontCreateWithGraphicsFont(CFTypeRef gfont, CGFloat size, const void *matrix, CFTypeRef attrs)
{
    return make_font(CFSTR("Helvetica"), size > 0 ? size : 12.0);
}
CFTypeRef CTFontCreateCopyWithAttributes(CFTypeRef font, CGFloat size, const void *matrix, CFTypeRef attrs)
{
    return make_font(font_name(font), size > 0 ? size : font_size(font));
}
CFTypeRef CTFontCreateCopyWithSymbolicTraits(CFTypeRef font, CGFloat size, const void *matrix, unsigned traits, unsigned mask)
{
    return make_font(font_name(font), size > 0 ? size : font_size(font));
}
CFTypeRef CTFontCreateForString(CFTypeRef font, CFStringRef str, CFRange range)
{
    return make_font(font_name(font), font_size(font));
}
CFTypeRef CTFontCreateForStringWithLanguage(CFTypeRef font, CFStringRef str, CFRange range, CFStringRef lang)
{
    return make_font(font_name(font), font_size(font));
}
CFTypeRef CTFontCopyGraphicsFont(CFTypeRef font, CFTypeRef *attrs)
{
    char b[256];
    CFTypeRef (*mk)(CFStringRef) = dlsym(RTLD_DEFAULT, "CGFontCreateWithFontName");
    if (attrs) *attrs = 0;
    return mk ? mk(font_name(font)) : 0;
}
CFTypeRef CTFontCreatePathForGlyph(CFTypeRef font, CGGlyph glyph, const void *matrix)
{
    CFTypeRef (*mk)(void) = dlsym(RTLD_DEFAULT, "CGPathCreateMutable");
    return mk ? mk() : 0;
}

/* ---- font queries ---- */
CFDictionaryRef CTFontCopyTraits(CFTypeRef font) { return make_traits(); }
unsigned CTFontGetSymbolicTraits(CFTypeRef font) { return 0; }
CFTypeRef CTFontCopyFontDescriptor(CFTypeRef font) { return make_desc(font_name(font), font_size(font)); }
CFTypeRef CTFontCopyAttribute(CFTypeRef font, CFStringRef attr)
{
    CFMutableDictionaryRef d = make_desc(font_name(font), font_size(font));
    CFTypeRef v = attr ? CFDictionaryGetValue(d, attr) : 0;
    return v ? CFRetain(v) : 0;
}
CFStringRef CTFontCopyName(CFTypeRef font, CFStringRef key)
{
    if (key && CFGetTypeID(key) == CFStringGetTypeID()) {
        char b[64];
        if (CFStringGetCString(key, b, sizeof b, UTF8) && !__builtin_strcmp(b, "CTFontFamilyName")) return family_of(font_name(font));
        if (CFStringGetCString(key, b, sizeof b, UTF8) && !__builtin_strcmp(b, "CTFontStyleName")) return CFSTR("Regular");
    }
    return CFRetain(font_name(font));
}
CFStringRef CTFontCopyFamilyName(CFTypeRef font) { return family_of(font_name(font)); }
CFStringRef CTFontCopyPostScriptName(CFTypeRef font) { return CFRetain(font_name(font)); }
CFStringRef CTFontCopyFullName(CFTypeRef font) { return CFRetain(font_name(font)); }
CFStringRef CTFontCopyLocalizedName(CFTypeRef font, CFStringRef key, CFStringRef *language)
{
    if (language) *language = CFSTR("en");
    return CTFontCopyName(font, key);
}
CFArrayRef CTFontCopyAvailableTables(CFTypeRef font, unsigned options) { return empty_array(); }
CFTypeRef CTFontCopyTable(CFTypeRef font, unsigned tag, unsigned options) { return 0; }   /* no such table */
const void *CTFontCopyCharacterSet(CFTypeRef font)
{
    CFRange r; r.location = 0; r.length = 0x10000;
    return CFCharacterSetCreateWithCharactersInRange(0, r);
}
CFArrayRef CTFontCopyDefaultCascadeListForLanguages(CFTypeRef font, CFArrayRef languages) { return empty_array(); }
CFDictionaryRef CTFontCopyVariation(CFTypeRef font) { return newdict(); }
CFArrayRef CTFontCopyVariationAxes(CFTypeRef font) { return empty_array(); }
CFArrayRef CTFontCopyVariationAxesInternal(CFTypeRef font) { return empty_array(); }

/* ---- metrics ---- */
CGFloat CTFontGetSize(CFTypeRef font) { return font_size(font); }
CGFloat CTFontGetAscent(CFTypeRef font) { return font_size(font) * 0.93; }
CGFloat CTFontGetDescent(CFTypeRef font) { return font_size(font) * 0.22; }
CGFloat CTFontGetLeading(CFTypeRef font) { return 0.0; }
CGFloat CTFontGetCapHeight(CFTypeRef font) { return font_size(font) * 0.72; }
CGFloat CTFontGetXHeight(CFTypeRef font) { return font_size(font) * 0.52; }
CGFloat CTFontGetSlantAngle(CFTypeRef font) { return 0.0; }
CGFloat CTFontGetUnderlinePosition(CFTypeRef font) { return font_size(font) * -0.1; }
CGFloat CTFontGetUnderlineThickness(CFTypeRef font) { return font_size(font) * 0.05; }
unsigned CTFontGetUnitsPerEm(CFTypeRef font) { return 1000; }
CFIndex CTFontGetGlyphCount(CFTypeRef font) { return 256; }
Rect CTFontGetBoundingBox(CFTypeRef font)
{
    Rect r; double s = font_size(font);
    r.x = -0.1 * s; r.y = -0.22 * s; r.w = 1.2 * s; r.h = 1.15 * s;
    return r;
}
Boolean CTFontGetGlyphsForCharacters(CFTypeRef font, const UniChar *chars, CGGlyph *glyphs, CFIndex count)
{
    CFIndex i;
    for (i = 0; i < count; i++) glyphs[i] = chars[i];
    return 1;
}
double CTFontGetAdvancesForGlyphs(CFTypeRef font, int orientation, const CGGlyph *glyphs, Rect *advances, CFIndex count)
{
    CFIndex i; double s = font_size(font) * 0.5;
    CGFloat *a = (CGFloat *)advances;   /* CGSize array: width, height */
    for (i = 0; advances && i < count; i++) { a[2 * i] = s; a[2 * i + 1] = 0; }
    return s * count;
}
Rect CTFontGetBoundingRectsForGlyphs(CFTypeRef font, int orientation, const CGGlyph *glyphs, Rect *rects, CFIndex count)
{
    CFIndex i; double s = font_size(font);
    Rect r;
    r.x = 0; r.y = -0.22 * s; r.w = 0.5 * s; r.h = 0.93 * s + 0.22 * s;
    for (i = 0; rects && i < count; i++) rects[i] = r;
    return r;
}
void CTFontDrawGlyphs(CFTypeRef font, const CGGlyph *glyphs, const void *positions, CFIndex count, void *context) { }

/* ---- font descriptors ---- */
unsigned long CTFontDescriptorGetTypeID(void) { return CFDictionaryGetTypeID(); }
CFTypeRef CTFontDescriptorCreateWithAttributes(CFDictionaryRef attrs)
{
    if (!isdict(attrs)) return make_desc(CFSTR("Helvetica"), 0);
    return CFDictionaryCreateMutableCopy(0, 0, attrs);
}
CFTypeRef CTFontDescriptorCreateWithNameAndSize(CFStringRef name, CGFloat size)
{
    return make_desc(name && CFGetTypeID(name) == CFStringGetTypeID() ? name : CFSTR("Helvetica"), size);
}
CFDictionaryRef CTFontDescriptorCopyAttributes(CFTypeRef desc)
{
    return isdict(desc) ? CFDictionaryCreateCopy(0, desc) : make_desc(CFSTR("Helvetica"), 0);
}
CFTypeRef CTFontDescriptorCopyAttribute(CFTypeRef desc, CFStringRef attr)
{
    CFTypeRef v = isdict(desc) && attr ? CFDictionaryGetValue(desc, attr) : 0;
    return v ? CFRetain(v) : 0;
}
CFTypeRef CTFontDescriptorCopyLocalizedAttribute(CFTypeRef desc, CFStringRef attr, CFStringRef *language)
{
    if (language) *language = 0;
    return CTFontDescriptorCopyAttribute(desc, attr);
}
CFTypeRef CTFontDescriptorCreateMatchingFontDescriptor(CFTypeRef desc, CFTypeRef mandatory)
{
    return isdict(desc) ? CFRetain(desc) : make_desc(CFSTR("Helvetica"), 0);
}
CFArrayRef CTFontDescriptorCreateMatchingFontDescriptors(CFTypeRef desc, CFTypeRef mandatory)
{
    return one_array(isdict(desc) ? desc : make_desc(CFSTR("Helvetica"), 0));
}
Boolean CTFontDescriptorIsSystemUIFont(CFTypeRef desc) { return 0; }
CFTypeRef CTFontManagerCreateFontDescriptorFromData(CFTypeRef data) { return make_desc(CFSTR("Helvetica"), 0); }

/* ---- collections and the font manager ---- */
static CFArrayRef all_descriptors(void)
{
    const void *v[3];
    v[0] = make_desc(CFSTR("Helvetica"), 0);
    v[1] = make_desc(CFSTR("Arial"), 0);
    v[2] = make_desc(CFSTR("Times"), 0);
    return CFArrayCreate(0, v, 3, kCFTypeArrayCallBacks);
}
CFTypeRef CTFontCollectionCreateFromAvailableFonts(CFTypeRef options) { return all_descriptors(); }
CFArrayRef CTFontCollectionCreateMatchingFontDescriptors(CFTypeRef collection)
{
    return collection ? CFRetain(collection) : all_descriptors();
}
CFArrayRef CTFontCollectionCreateMatchingFontDescriptorsForFamily(CFTypeRef collection, CFStringRef family, CFTypeRef options)
{
    return one_array(make_desc(family && CFGetTypeID(family) == CFStringGetTypeID() ? family : CFSTR("Helvetica"), 0));
}
CFArrayRef CTFontManagerCopyAvailableFontFamilyNames(void)
{
    const void *v[3];
    v[0] = CFSTR("Helvetica"); v[1] = CFSTR("Arial"); v[2] = CFSTR("Times");
    return CFArrayCreate(0, v, 3, kCFTypeArrayCallBacks);
}
