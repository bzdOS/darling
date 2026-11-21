/* AppKitExtras behaviour: -symbolicTraits on the dictionary that stands for a font descriptor.
 * The CoreText stub models a CTFontDescriptor as a CFDictionary (behaviors/CoreText/coretext.c), and -[NSFont fontDescriptor]
 * (font.c) hands that dictionary out, so Chromium's `font.fontDescriptor.symbolicTraits` lands on __NSCFDictionary and used to come
 * back as OBJC-FWD-MISS and a zero (measured in every run of the fix26 series).
 *
 * Added to __NSCFDictionary once the class exists (add-image retry as in window.c), only when the class does not answer the
 * selector already. The value is the NSCTFontSymbolicTrait number inside the NSCTFontTraitsAttribute dictionary of the descriptor
 * (the key strings of kCTFontTraitsAttribute / kCTFontSymbolicTrait in coretext.c), 0 when either is missing. */
/* START_APPKIT_FONTDESC_FILL */
typedef void *Class;
typedef void *SEL;
typedef void *IMP;
typedef void *id;
extern Class objc_getClass(const char *name);
extern SEL sel_registerName(const char *name);
extern unsigned char class_addMethod(Class cls, SEL name, IMP imp, const char *types);
extern unsigned char class_respondsToSelector(Class cls, SEL sel);
extern id objc_msgSend(id, SEL, ...);
struct mach_header;
extern void _dyld_register_func_for_add_image(void (*func)(const struct mach_header *, long));

#define MSG(ret, ...) ((ret (*)(__VA_ARGS__))objc_msgSend)

static id cfstr(const char *s)
{
    return MSG(id, id, SEL, const char *)(objc_getClass("NSString"), sel_registerName("stringWithUTF8String:"), s);
}

static unsigned int dict_symbolic_traits(id self, SEL cmd)
{
    id traits, num;
    (void)cmd;
    traits = MSG(id, id, SEL, id)(self, sel_registerName("objectForKey:"), cfstr("NSCTFontTraitsAttribute"));
    if (!traits || !MSG(unsigned char, id, SEL, Class)(traits, sel_registerName("respondsToSelector:"), (Class)sel_registerName("objectForKey:")))
        return 0;
    num = MSG(id, id, SEL, id)(traits, sel_registerName("objectForKey:"), cfstr("NSCTFontSymbolicTrait"));
    if (!num || !MSG(unsigned char, id, SEL, Class)(num, sel_registerName("respondsToSelector:"), (Class)sel_registerName("unsignedIntValue")))
        return 0;
    return MSG(unsigned int, id, SEL)(num, sel_registerName("unsignedIntValue"));
}

static int done;

static void try_add(void)
{
    Class c;
    if (done)
        return;
    c = objc_getClass("__NSCFDictionary");
    if (!c)
        return;
    if (!class_respondsToSelector(c, sel_registerName("symbolicTraits")))
        class_addMethod(c, sel_registerName("symbolicTraits"), (IMP)dict_symbolic_traits, "I16@0:8");
    done = 1;
}

static void on_image(const struct mach_header *mh, long slide) { (void)mh; (void)slide; try_add(); }

__attribute__((constructor)) static void appkit_extras_fontdesc(void)
{
    try_add();
    if (!done)
        _dyld_register_func_for_add_image(on_image);
}
/* END_APPKIT_FONTDESC_FILL */
