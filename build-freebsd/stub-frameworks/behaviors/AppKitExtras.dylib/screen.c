/* AppKitExtras behaviour: the NSScreen API that Chromium asks for and this AppKit does not implement
 * (each came back through the forward handler as OBJC-FWD-MISS and a zero, which for a CGFloat reply
 * is the wrong value: a backing scale of 0 divides by zero in the display code).
 *
 * Added to NSScreen once AppKit is mapped (add-image retry as in workspace.c); existing methods are not replaced:
 *   backingScaleFactor 1.0, maximumFramesPerSecond 60, minimumRefreshInterval 1/60,
 *   maximumExtendedDynamicRangeColorComponentValue 1.0, maximumPotentialExtendedDynamicRangeColorComponentValue 1.0
 *   (no HiDPI, no EDR: SDR display at 60 Hz), colorSpace (sRGB), localizedName ("Built-in Display"). */
typedef void *Class;
typedef void *SEL;
typedef void *IMP;
typedef void *id;
extern Class objc_getClass(const char *name);
extern SEL sel_registerName(const char *name);
extern unsigned char class_addMethod(Class cls, SEL name, IMP imp, const char *types);
extern id objc_msgSend(id, SEL, ...);
extern unsigned char class_respondsToSelector(Class cls, SEL sel);
struct mach_header;
extern void _dyld_register_func_for_add_image(void (*func)(const struct mach_header *, long));

static double one(id self, SEL cmd) { (void)self; (void)cmd; return 1.0; }
static double refresh_interval(id self, SEL cmd) { (void)self; (void)cmd; return 1.0 / 60.0; }
static long fps60(id self, SEL cmd) { (void)self; (void)cmd; return 60; }
static id local_name(id self, SEL cmd)
{
    (void)self; (void)cmd;
    return ((id (*)(id, SEL, const char *))objc_msgSend)(objc_getClass("NSString"), sel_registerName("stringWithUTF8String:"), "Built-in Display");
}
static id color_space(id self, SEL cmd)
{
    id cls = objc_getClass("NSColorSpace");
    (void)self; (void)cmd;
    if (!cls)
        return 0;
    if (class_respondsToSelector(*(Class *)cls /* metaclass */, sel_registerName("sRGBColorSpace")))
        return ((id (*)(id, SEL))objc_msgSend)(cls, sel_registerName("sRGBColorSpace"));
    return ((id (*)(id, SEL))objc_msgSend)(cls, sel_registerName("genericRGBColorSpace"));
}

static int done;

static void try_add(void)
{
    Class c;
    if (done)
        return;
    c = objc_getClass("NSScreen");
    if (!c)
        return;
    class_addMethod(c, sel_registerName("backingScaleFactor"), (IMP)one, "d16@0:8");
    class_addMethod(c, sel_registerName("maximumExtendedDynamicRangeColorComponentValue"), (IMP)one, "d16@0:8");
    class_addMethod(c, sel_registerName("maximumPotentialExtendedDynamicRangeColorComponentValue"), (IMP)one, "d16@0:8");
    class_addMethod(c, sel_registerName("minimumRefreshInterval"), (IMP)refresh_interval, "d16@0:8");
    class_addMethod(c, sel_registerName("maximumFramesPerSecond"), (IMP)fps60, "q16@0:8");
    class_addMethod(c, sel_registerName("localizedName"), (IMP)local_name, "@16@0:8");
    class_addMethod(c, sel_registerName("colorSpace"), (IMP)color_space, "@16@0:8");
    done = 1;
}

static void on_image(const struct mach_header *mh, long slide) { (void)mh; (void)slide; try_add(); }

__attribute__((constructor)) static void appkit_extras_screen(void)
{
    try_add();
    if (!done)
        _dyld_register_func_for_add_image(on_image);
}
