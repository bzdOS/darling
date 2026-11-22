/* AppKitExtras behaviour: the NSScreen API that Chromium asks for and this AppKit does not implement
 * (each came back through the forward handler as OBJC-FWD-MISS and a zero, which for a CGFloat reply
 * is the wrong value: a backing scale of 0 divides by zero in the display code).
 *
 * Added to NSScreen once AppKit is mapped (add-image retry as in workspace.c); existing methods are not replaced:
 *   backingScaleFactor 1.0, maximumFramesPerSecond 60, minimumRefreshInterval 1/60,
 *   maximumExtendedDynamicRangeColorComponentValue 1.0, maximumPotentialExtendedDynamicRangeColorComponentValue 1.0
 *   (no HiDPI, no EDR: SDR display at 60 Hz), colorSpace (sRGB), localizedName ("Built-in Display").
 *
 * Screen identity (Chromium's display::ScreenMac matches screens by object and by deviceDescription[NSScreenNumber]):
 *   +[NSScreen screens] returns a cached array whose NSScreen objects stay the same while the screen count and frames
 *   are unchanged (the stock one builds new objects on every call); +mainScreen is screens[0];
 *   -[NSWindow screen] is the cached screen whose frame intersects the window frame (else screens[0]);
 *   -[NSScreen deviceDescription] has NSScreenNumber (the CGDirectDisplayID, = index+1 as CGMainDisplayID gives for
 *   screens[0] = 1), NSDeviceSize, NSDeviceResolution 72x72, NSDeviceIsScreen YES;
 *   -[NSWindow backingScaleFactor] is the screen's (1.0), -[NSWindow occlusionState] is NSWindowOcclusionStateVisible (2). */
#include "../objc-proto.h"
extern unsigned char class_addMethod(Class cls, SEL name, IMP imp, const char *types);
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

typedef struct { double x, y, w, h; } Rc;
typedef struct { double w, h; } Sz;
extern Class object_getClass(id obj);
extern IMP class_replaceMethod(Class cls, SEL name, IMP imp, const char *types);
extern IMP class_getMethodImplementation(Class cls, SEL name);

static IMP orig_screens;
static id cache_arr;
static int cache_n;
static Rc cache_fr[16];

static Rc frame_of(id o, const char *sel)
{
    return ((Rc (*)(id, SEL))class_getMethodImplementation(object_getClass(o), S(sel)))(o, S(sel));
}

static id screens_cached(id self, SEL cmd)
{
    id fresh = ((id (*)(id, SEL))orig_screens)(self, cmd);
    long n = fresh ? MSG(long, id, SEL)(fresh, S("count")) : 0, i;
    int same = cache_arr && n == cache_n && n <= 16;
    Rc fr[16];
    for (i = 0; i < n && i < 16; i++) {
        id sc = MSG(id, id, SEL, long)(fresh, S("objectAtIndex:"), i);
        fr[i] = frame_of(sc, "frame");
        if (same && (fr[i].x != cache_fr[i].x || fr[i].y != cache_fr[i].y || fr[i].w != cache_fr[i].w || fr[i].h != cache_fr[i].h))
            same = 0;
    }
    if (same || !fresh || n == 0 || n > 16)
        return cache_arr ? cache_arr : fresh;
    MSG(id, id, SEL)(fresh, S("retain"));
    for (i = 0; i < n; i++) {
        id sc = MSG(id, id, SEL, long)(fresh, S("objectAtIndex:"), i);
        MSG(void, id, SEL, unsigned)(sc, S("setCgDirectDisplayID:"), (unsigned)(i + 1));
        cache_fr[i] = fr[i];
    }
    if (cache_arr)
        MSG(void, id, SEL)(cache_arr, S("release"));
    cache_arr = fresh;
    cache_n = (int)n;
    return fresh;
}

static id main_screen(id self, SEL cmd)
{
    id a = MSG(id, id, SEL)(self, S("screens"));
    (void)cmd;
    return a && MSG(long, id, SEL)(a, S("count")) ? MSG(id, id, SEL, long)(a, S("objectAtIndex:"), 0) : 0;
}

static id window_screen(id self, SEL cmd)
{
    id a = MSG(id, id, SEL)((id)objc_getClass("NSScreen"), S("screens")), first;
    long n, i;
    Rc w;
    (void)cmd;
    if (!a || !(n = MSG(long, id, SEL)(a, S("count"))))
        return 0;
    first = MSG(id, id, SEL, long)(a, S("objectAtIndex:"), 0);
    w = frame_of(self, "frame");
    for (i = 0; i < n; i++) {
        id sc = MSG(id, id, SEL, long)(a, S("objectAtIndex:"), i);
        Rc f = frame_of(sc, "frame");
        if (w.x < f.x + f.w && f.x < w.x + w.w && w.y < f.y + f.h && f.y < w.y + w.h)
            return sc;
    }
    return first;
}

static id nsstr(const char *c)
{
    return MSG(id, id, SEL, const char *)((id)objc_getClass("NSString"), S("stringWithUTF8String:"), c);
}

static id device_description(id self, SEL cmd)
{
    id d = MSG(id, id, SEL)((id)objc_getClass("NSMutableDictionary"), S("dictionary"));
    id nsv = (id)objc_getClass("NSValue");
    Rc f = frame_of(self, "frame");
    Sz sz = { f.w, f.h }, res = { 72.0, 72.0 };
    unsigned idn = MSG(unsigned, id, SEL)(self, S("cgDirectDisplayID"));
    (void)cmd;
    if (!idn)
        idn = 1;
    MSG(void, id, SEL, id, id)(d, S("setObject:forKey:"), MSG(id, id, SEL, unsigned)((id)objc_getClass("NSNumber"), S("numberWithUnsignedInt:"), idn), nsstr("NSScreenNumber"));
    MSG(void, id, SEL, id, id)(d, S("setObject:forKey:"), MSG(id, id, SEL, Sz)(nsv, S("valueWithSize:"), sz), nsstr("NSDeviceSize"));
    MSG(void, id, SEL, id, id)(d, S("setObject:forKey:"), MSG(id, id, SEL, Sz)(nsv, S("valueWithSize:"), res), nsstr("NSDeviceResolution"));
    MSG(void, id, SEL, id, id)(d, S("setObject:forKey:"), nsstr("YES"), nsstr("NSDeviceIsScreen"));
    return d;
}

static double window_backing_scale(id self, SEL cmd)
{
    id sc = MSG(id, id, SEL)(self, S("screen"));
    (void)cmd;
    return sc ? MSG(double, id, SEL)(sc, S("backingScaleFactor")) : 1.0;
}
static unsigned long window_occlusion(id self, SEL cmd) { (void)self; (void)cmd; return 2; }

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
    {
        Class meta = object_getClass((id)c), w = objc_getClass("NSWindow");
        if (!w || !class_respondsToSelector(c, sel_registerName("setCgDirectDisplayID:")))
            return;
        orig_screens = class_replaceMethod(meta, sel_registerName("screens"), (IMP)screens_cached, "@16@0:8");
        class_replaceMethod(meta, sel_registerName("mainScreen"), (IMP)main_screen, "@16@0:8");
        class_replaceMethod(c, sel_registerName("deviceDescription"), (IMP)device_description, "@16@0:8");
        class_replaceMethod(w, sel_registerName("screen"), (IMP)window_screen, "@16@0:8");
        class_replaceMethod(w, sel_registerName("backingScaleFactor"), (IMP)window_backing_scale, "d16@0:8");
        class_replaceMethod(w, sel_registerName("occlusionState"), (IMP)window_occlusion, "Q16@0:8");
    }
    done = 1;
}

static void on_image(const struct mach_header *mh, long slide) { (void)mh; (void)slide; try_add(); }

__attribute__((constructor)) static void appkit_extras_screen(void)
{
    try_add();
    if (!done)
        _dyld_register_func_for_add_image(on_image);
}
