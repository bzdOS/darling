/* AppKitExtras behaviour: -[NSColor colorUsingColorSpace:] (the NSColorSpace form, macOS 10.6+).
 *
 * Chromium converts a colour to sRGB with [color colorUsingColorSpace:[NSColorSpace sRGBColorSpace]]. When the
 * answer is nil it draws the colour into a 1x1 bitmap, reads the pixel back, builds a new colour from it and
 * converts that one the same way: the recursion ends only when the conversion answers a colour. This AppKit has
 * only -colorUsingColorSpaceName: (a NSColor_CGColor/NSColor_catalog colour forwarded the selector to nothing, so the
 * answer was always nil and the recursion ran until the stack overflowed).
 *
 * Added to NSColor (inherited by its subclasses) when absent: the colour in the calibrated RGB name space, which
 * is the RGB model of every colour space this AppKit offers; nil colour space or no conversion gives nil. */
typedef void *Class;
typedef void *SEL;
typedef void *IMP;
typedef void *id;
extern Class objc_getClass(const char *name);
extern SEL sel_registerName(const char *name);
extern unsigned char class_addMethod(Class cls, SEL name, IMP imp, const char *types);
extern id objc_msgSend(id, SEL, ...);
struct mach_header;
extern void _dyld_register_func_for_add_image(void (*func)(const struct mach_header *, long));

#define S(x) sel_registerName(x)

static id use_color_space(id self, SEL cmd, id space)
{
    id name;
    (void)cmd;
    if (!space)
        return 0;
    name = ((id (*)(id, SEL, const char *))objc_msgSend)(objc_getClass("NSString"), S("stringWithUTF8String:"),
                                                          "NSCalibratedRGBColorSpace");
    /* the two argument form: the one argument form of NSColor_CGColor is the abstract NSColor one that raises */
    return ((id (*)(id, SEL, id, id))objc_msgSend)(self, S("colorUsingColorSpaceName:device:"), name, 0);
}

static int done;

static void try_add(void)
{
    Class c;
    if (done)
        return;
    c = objc_getClass("NSColor");
    if (!c)
        return;
    class_addMethod(c, S("colorUsingColorSpace:"), (IMP)use_color_space, "@24@0:8@16");
    done = 1;
}

static void on_image(const struct mach_header *mh, long slide) { (void)mh; (void)slide; try_add(); }

__attribute__((constructor)) static void appkit_extras_color(void)
{
    try_add();
    if (!done)
        _dyld_register_func_for_add_image(on_image);
}
