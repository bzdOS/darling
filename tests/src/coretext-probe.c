/* coretext-probe: the CoreText Create/Copy functions a browser calls must answer objects of the right type, never NULL.
 * Used to crash in CFDictionaryGetValue(NULL, ..) after CTFontCopyTraits answered NULL (stub image).
 * One line per check "CORETEXT-T <name> ok|BAD", then "CORETEXT-VERDICT pass|FAIL n=<checks> bad=[names]".
 * Built twice by build-coretext-probe.sh: linked against CoreText, and against CoreTextExtras (CORETEXT_IMAGE names it). */
#include <stdio.h>
#include <string.h>

typedef const void *R;
typedef long CFIndex;
typedef struct { CFIndex location, length; } CFRange;
typedef struct { double x, y, w, h; } Rect;
extern unsigned long CFGetTypeID(R);
extern unsigned long CFDictionaryGetTypeID(void), CFStringGetTypeID(void), CFArrayGetTypeID(void), CFNumberGetTypeID(void);
extern CFIndex CFArrayGetCount(R);
extern R CFDictionaryGetValue(R, R);
extern R CFStringCreateWithCString(R, const char *, unsigned);
extern unsigned char CFNumberGetValue(R, long, void *);

extern R CTFontCreateWithName(R, double, const void *);
extern R CTFontCreateWithFontDescriptor(R, double, const void *);
extern R CTFontCreateUIFontForLanguage(int, double, R);
extern R CTFontCreateCopyWithAttributes(R, double, const void *, R);
extern R CTFontCreateCopyWithSymbolicTraits(R, double, const void *, unsigned, unsigned);
extern R CTFontCreateForString(R, R, CFRange);
extern R CTFontCopyTraits(R), CTFontCopyFontDescriptor(R), CTFontCopyFamilyName(R), CTFontCopyFullName(R), CTFontCopyPostScriptName(R);
extern R CTFontCopyAttribute(R, R), CTFontCopyName(R, R), CTFontCopyLocalizedName(R, R, R *);
extern R CTFontCopyAvailableTables(R, unsigned), CTFontCopyVariationAxes(R), CTFontCopyVariation(R), CTFontCopyCharacterSet(R);
extern R CTFontCopyDefaultCascadeListForLanguages(R, R);
extern double CTFontGetSize(R), CTFontGetAscent(R), CTFontGetDescent(R), CTFontGetCapHeight(R), CTFontGetXHeight(R);
extern double CTFontGetAdvancesForGlyphs(R, int, const unsigned short *, void *, CFIndex);
extern Rect CTFontGetBoundingBox(R);
extern unsigned CTFontGetUnitsPerEm(R), CTFontGetSymbolicTraits(R);
extern unsigned char CTFontGetGlyphsForCharacters(R, const unsigned short *, unsigned short *, CFIndex);
extern R CTFontDescriptorCreateWithAttributes(R), CTFontDescriptorCreateWithNameAndSize(R, double);
extern R CTFontDescriptorCopyAttributes(R), CTFontDescriptorCopyAttribute(R, R);
extern R CTFontDescriptorCreateMatchingFontDescriptor(R, R), CTFontDescriptorCreateMatchingFontDescriptors(R, R);
extern R CTFontCollectionCreateFromAvailableFonts(R), CTFontCollectionCreateMatchingFontDescriptors(R);
extern R CTFontCollectionCreateMatchingFontDescriptorsForFamily(R, R, R);
extern R CTFontManagerCopyAvailableFontFamilyNames(void);
extern R kCTFontNameAttribute, kCTFontFamilyNameAttribute, kCTFontSymbolicTrait, kCTFontWeightTrait, kCTFontWidthTrait, kCTFontSlantTrait;

static int n, bad;
static char names[600];
static void rep(const char *name, int ok)
{
	n++;
	printf("CORETEXT-T %s %s\n", name, ok ? "ok" : "BAD");
	if (!ok) {
		bad++;
		if (strlen(names) + strlen(name) + 2 < sizeof names) {
			if (names[0]) strcat(names, ",");
			strcat(names, name);
		}
	}
	fflush(stdout);
}
static int isdict(R o) { return o && CFGetTypeID(o) == CFDictionaryGetTypeID(); }
static int isstr(R o) { return o && CFGetTypeID(o) == CFStringGetTypeID(); }
static int isarr(R o) { return o && CFGetTypeID(o) == CFArrayGetTypeID(); }
static int isnum(R o) { return o && CFGetTypeID(o) == CFNumberGetTypeID(); }
#define OBJ(name, e) rep(name, (e) != 0)

int main(void)
{
	R name = CFStringCreateWithCString(0, "Helvetica", 0x08000100);
	R font, f2, desc, tr, d2, col, lang = 0;
	CFRange rng = { 0, 1 };
	unsigned short ch[2] = { 'A', 'b' }, gl[2] = { 0, 0 };
	double adv[4] = { 0, 0, 0, 0 };
	Rect bb;

	font = CTFontCreateWithName(name, 14.0, 0);
	OBJ("CreateWithName", font);
	tr = CTFontCopyTraits(font);
	rep("CopyTraits", isdict(tr));
	rep("traits-symbolic", tr && isnum(CFDictionaryGetValue(tr, kCTFontSymbolicTrait)));
	rep("traits-weight", tr && isnum(CFDictionaryGetValue(tr, kCTFontWeightTrait)));
	rep("traits-width", tr && isnum(CFDictionaryGetValue(tr, kCTFontWidthTrait)));
	rep("traits-slant", tr && isnum(CFDictionaryGetValue(tr, kCTFontSlantTrait)));
	rep("CopyTraits-NULLfont", isdict(CTFontCopyTraits(0)));
	rep("CopyAttribute-name", isstr(CTFontCopyAttribute(font, kCTFontNameAttribute)));
	rep("CopyFamilyName", isstr(CTFontCopyFamilyName(font)));
	rep("CopyFullName", isstr(CTFontCopyFullName(font)));
	rep("CopyPostScriptName", isstr(CTFontCopyPostScriptName(font)));
	rep("CopyName", isstr(CTFontCopyName(font, name)));
	rep("CopyLocalizedName", isstr(CTFontCopyLocalizedName(font, name, &lang)));
	desc = CTFontCopyFontDescriptor(font);
	rep("CopyFontDescriptor", isdict(desc));
	rep("DescriptorCopyAttributes", isdict(CTFontDescriptorCopyAttributes(desc)));
	rep("DescriptorCopyAttribute-family", isstr(CTFontDescriptorCopyAttribute(desc, kCTFontFamilyNameAttribute)));
	f2 = CTFontCreateWithFontDescriptor(desc, 20.0, 0);
	rep("CreateWithFontDescriptor", f2 && CTFontGetSize(f2) == 20.0);
	f2 = CTFontCreateCopyWithAttributes(font, 9.0, 0, 0);
	rep("CreateCopyWithAttributes", f2 && CTFontGetSize(f2) == 9.0);
	OBJ("CreateCopyWithSymbolicTraits", CTFontCreateCopyWithSymbolicTraits(font, 0, 0, 1, 1));
	OBJ("CreateForString", CTFontCreateForString(font, name, rng));
	f2 = CTFontCreateUIFontForLanguage(0, 0, 0);
	rep("CreateUIFontForLanguage", f2 && CTFontGetSize(f2) > 0);
	rep("CopyAvailableTables", isarr(CTFontCopyAvailableTables(font, 0)));
	rep("CopyVariationAxes", isarr(CTFontCopyVariationAxes(font)));
	rep("CopyVariation", isdict(CTFontCopyVariation(font)));
	rep("CopyDefaultCascadeList", isarr(CTFontCopyDefaultCascadeListForLanguages(font, 0)));
	OBJ("CopyCharacterSet", CTFontCopyCharacterSet(font));
	rep("size", CTFontGetSize(font) == 14.0);
	rep("ascent-descent", CTFontGetAscent(font) > 0 && CTFontGetDescent(font) > 0);
	rep("cap-x-height", CTFontGetCapHeight(font) > 0 && CTFontGetXHeight(font) > 0);
	rep("unitsPerEm", CTFontGetUnitsPerEm(font) > 0);
	rep("symbolicTraits", CTFontGetSymbolicTraits(font) == 0);
	bb = CTFontGetBoundingBox(font);
	rep("boundingBox", bb.w > 0 && bb.h > 0);
	rep("glyphsForCharacters", CTFontGetGlyphsForCharacters(font, ch, gl, 2) && gl[0] && gl[1]);
	rep("advances", CTFontGetAdvancesForGlyphs(font, 0, gl, adv, 2) > 0 && adv[0] > 0);
	d2 = CTFontDescriptorCreateWithNameAndSize(name, 12.0);
	rep("DescriptorCreateWithNameAndSize", isdict(d2));
	rep("DescriptorCreateWithAttributes", isdict(CTFontDescriptorCreateWithAttributes(d2)));
	OBJ("DescriptorCreateMatching", CTFontDescriptorCreateMatchingFontDescriptor(d2, 0));
	rep("DescriptorCreateMatchingAll", isarr(CTFontDescriptorCreateMatchingFontDescriptors(d2, 0)));
	col = CTFontCollectionCreateFromAvailableFonts(0);
	OBJ("CollectionCreate", col);
	rep("CollectionMatching", isarr(CTFontCollectionCreateMatchingFontDescriptors(col)) &&
	    CFArrayGetCount(CTFontCollectionCreateMatchingFontDescriptors(col)) > 0);
	rep("CollectionForFamily", isarr(CTFontCollectionCreateMatchingFontDescriptorsForFamily(col, name, 0)));
	rep("ManagerFamilyNames", isarr(CTFontManagerCopyAvailableFontFamilyNames()) &&
	    CFArrayGetCount(CTFontManagerCopyAvailableFontFamilyNames()) > 0);
	printf("CORETEXT-VERDICT %s n=%d bad=[%s]\n", bad ? "FAIL" : "pass", n, names);
	return bad ? 1 : 0;
}
