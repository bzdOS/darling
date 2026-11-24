/* NSStart probe: why does the browser main thread not reach NSWindow creation?
 * Prints on stderr (once the hooks are in, which happens when AppKit's classes are registered):
 *   NSSTART-PROBE installed                          - all three replacements took
 *   NSSTART-PROBE: finishLaunching enter/exit        - -[NSApplication finishLaunching] ran
 *   NSSTART-PROBE: platformWindow first call         - the Darling NSWindow category getter (the seam that
 *                                                      creates the platform window on the main thread) was asked
 *   NSSTART-PROBE: _createPlatformWindowOnMainThread enter/exit - the actual window creation
 *                                                      ([[NSDisplay currentDisplay] newWindowWithDelegate:])
 * Silence on a line means that step never ran. Remove together with the startup fix. */
/* START_APPKIT_NSSTART_PROBE */
#include "../objc-proto.h"
extern IMP class_replaceMethod(Class cls, SEL name, IMP imp, const char *types);
extern Class object_getClass(id obj);
extern long write(int, const void *, unsigned long);
extern char *strstr(const char *a, const char *b);
struct mach_header;
extern void _dyld_register_func_for_add_image(void (*func)(const struct mach_header *, long));
typedef struct { const char *dli_fname; void *dli_fbase; const char *dli_sname; void *dli_saddr; } Dl_info_x;
extern int dladdr(const void *, Dl_info_x *);
extern int dprintf(int, const char *, ...);

static IMP orig_finish, orig_pwin, orig_create, orig_run, orig_modal;
static IMP orig_isvis, orig_occ, orig_orderfront, orig_ofr, orig_mkaf, orig_owr, orig_setvis;

static void wl_install(Class dispClass);
static id my_curdisp(id self, SEL cmd);
static IMP orig_curdisp, orig_nww, orig_wwinit, orig_wwsurface;
static int wl_diag, wl_hooks_done;
static int probe_done;

static void say(const char *m)
{
    const char *p = m;
    while (*p)
        p++;
    write(2, m, p - m);
}

static void visprobe_caller(const char *sel, id self)
{
    void *ra = __builtin_return_address(1);
    Dl_info_x di;
    const char *img = "?";
    unsigned long off = (unsigned long)ra;
    if (dladdr(ra, &di) && di.dli_fname) {
        img = di.dli_fname;
        off = (unsigned long)ra - (unsigned long)di.dli_fbase;
    }
    dprintf(2, "VISPROBE: %s self=%p caller=%s+0x%lx\n", sel, self, img, off);
}

static unsigned long window_count(void)
{
    id app = MSG(id, id, SEL)((id)objc_getClass("NSApplication"), S("sharedApplication"));
    id wins;
    if (!app)
        return 0;
    wins = MSG(id, id, SEL)(app, S("windows"));
    return wins ? MSG(unsigned long, id, SEL)(wins, S("count")) : 0;
}

static void my_finish(id self, SEL cmd)
{
    say("NSSTART-PROBE: finishLaunching enter\n");
    ((void (*)(id, SEL))orig_finish)(self, cmd);
    say("NSSTART-PROBE: finishLaunching exit\n");
}

static id my_pwin(id self, SEL cmd)
{
    static int said;
    if (!said) {
        said = 1;
        say("NSSTART-PROBE: platformWindow first call\n");
    }
    return ((id (*)(id, SEL))orig_pwin)(self, cmd);
}

/* Order-front probe: forcing makeKeyAndOrderFront/orderFrontRegardless from this
 * hook crashes a Chrome Framework notification observer (SIGSEGV addr=0xe0, rdi=0x0,
 * null deref at +0xe0 in ChromeFW during NSWindowDidUpdateNotification posted by
 * -[NSWindow update]). Report window state without forcing the crashing path. */
static int wl_diag;

static void my_create(id self, SEL cmd)
{
    signed char visible;
    unsigned long occ;
    say("NSSTART-PROBE: _createPlatformWindowOnMainThread enter\n");
    if (!wl_diag) {
        wl_diag = 1;
        dprintf(2, "WLPROBE-DIAG nsdisplay=%p probe_done=%d curdisp=%p nww=%p waylandwindow=%p\n",
                objc_getClass("NSDisplay"), probe_done, (void *)orig_curdisp,
                (void *)orig_nww, (void *)objc_getClass("WaylandWindow"));
    }
    wl_install(0);
    ((void (*)(id, SEL))orig_create)(self, cmd);
    say("NSSTART-PROBE: _createPlatformWindowOnMainThread exit\n");
    wl_install(0);
    visible = orig_isvis ? ((signed char (*)(id, SEL))orig_isvis)(self, S("isVisible"))
                         : MSG(signed char, id, SEL)(self, S("isVisible"));
    occ = orig_occ ? ((unsigned long (*)(id, SEL))orig_occ)(self, S("occlusionState"))
                   : MSG(unsigned long, id, SEL)(self, S("occlusionState"));
    dprintf(2, "ORDERFRONT-PROBE: visible=%d occlusion=%lu windows=%lu\n", (int)visible, occ, window_count());
}

static signed char my_isvis(id self, SEL cmd)
{
    signed char r = orig_isvis ? ((signed char (*)(id, SEL))orig_isvis)(self, cmd) : 0;
    visprobe_caller("isVisible", self);
    dprintf(2, "VISPROBE: isVisible self=%p -> %d\n", self, (int)r);
    return r;
}

static unsigned long my_occ(id self, SEL cmd)
{
    unsigned long r = orig_occ ? ((unsigned long (*)(id, SEL))orig_occ)(self, cmd) : 0;
    visprobe_caller("occlusionState", self);
    dprintf(2, "VISPROBE: occlusionState self=%p -> %lu\n", self, r);
    return r;
}

static void my_orderfront(id self, SEL cmd, id sender)
{
    visprobe_caller("orderFront:", self);
    dprintf(2, "VISPROBE: orderFront: self=%p sender=%p windows=%lu\n", self, sender, window_count());
    if (orig_orderfront)
        ((void (*)(id, SEL, id))orig_orderfront)(self, cmd, sender);
    if (orig_isvis)
        dprintf(2, "VISPROBE: orderFront: done visible=%d\n", (int)((signed char (*)(id, SEL))orig_isvis)(self, S("isVisible")));
}

static void my_ofr(id self, SEL cmd)
{
    visprobe_caller("orderFrontRegardless", self);
    dprintf(2, "VISPROBE: orderFrontRegardless self=%p windows=%lu\n", self, window_count());
    if (orig_ofr)
        ((void (*)(id, SEL))orig_ofr)(self, cmd);
    if (orig_isvis)
        dprintf(2, "VISPROBE: orderFrontRegardless done visible=%d\n", (int)((signed char (*)(id, SEL))orig_isvis)(self, S("isVisible")));
}

static void my_mkaf(id self, SEL cmd, id sender)
{
    visprobe_caller("makeKeyAndOrderFront:", self);
    dprintf(2, "VISPROBE: makeKeyAndOrderFront: self=%p sender=%p windows=%lu\n", self, sender, window_count());
    if (orig_mkaf)
        ((void (*)(id, SEL, id))orig_mkaf)(self, cmd, sender);
    if (orig_isvis)
        dprintf(2, "VISPROBE: makeKeyAndOrderFront: done visible=%d\n", (int)((signed char (*)(id, SEL))orig_isvis)(self, S("isVisible")));
}

static void my_owr(id self, SEL cmd, long place, long other)
{
    visprobe_caller("orderWindow:relativeTo:", self);
    dprintf(2, "VISPROBE: orderWindow:relativeTo: self=%p place=%ld other=%ld\n", self, place, other);
    if (orig_owr)
        ((void (*)(id, SEL, long, long))orig_owr)(self, cmd, place, other);
    if (orig_isvis)
        dprintf(2, "VISPROBE: orderWindow:relativeTo: done visible=%d\n", (int)((signed char (*)(id, SEL))orig_isvis)(self, S("isVisible")));
}

static void my_setvis(id self, SEL cmd, signed char v)
{
    visprobe_caller("setIsVisible:", self);
    dprintf(2, "VISPROBE: setIsVisible: self=%p v=%d\n", self, (int)v);
    if (orig_setvis)
        ((void (*)(id, SEL, signed char))orig_setvis)(self, cmd, v);
}

/* Wayland backend probe: which backend class actually serves the windows, whether
 * its window object is created, and whether a surface pointer is ever handed out.
 * The deployed Wayland.backend bundle predates this tree's sources, so only the
 * running image can answer what the source tree cannot. */

extern const char *class_getName(Class cls);

static id my_nww(id self, SEL cmd, id delegate)
{
    id w = orig_nww ? ((id (*)(id, SEL, id))orig_nww)(self, cmd, delegate) : 0;
    dprintf(2, "WLPROBE: newWindowWithDelegate self=%p delegate=%p ret=%p\n", self, delegate, w);
    return w;
}

static id my_wwinit(id self, SEL cmd, id delegate)
{
    id w = orig_wwinit ? ((id (*)(id, SEL, id))orig_wwinit)(self, cmd, delegate) : 0;
    dprintf(2, "WLPROBE: waylandWindowInit self=%p delegate=%p ret=%p\n", self, delegate, w);
    return w;
}

static void *my_wwsurface(id self, SEL cmd)
{
    void *s = orig_wwsurface ? ((void *(*)(id, SEL))orig_wwsurface)(self, cmd) : 0;
    visprobe_caller("surface", self);
    dprintf(2, "WLPROBE: surface self=%p -> %p\n", self, s);
    return s;
}

static void wl_install(Class dispClass)
{
    Class nd, ww;
    if (wl_hooks_done)
        return;
    nd = objc_getClass("NSDisplay");
    if (nd && orig_curdisp == 0)
        orig_curdisp = class_replaceMethod(object_getClass(nd), S("currentDisplay"),
                                           (IMP)my_curdisp, "@16@0:8");
    if (dispClass && orig_nww == 0) {
        orig_nww = class_replaceMethod(dispClass, S("newWindowWithDelegate:"),
                                       (IMP)my_nww, "@24@0:8@16");
        dprintf(2, "WLPROBE: newWindowWithDelegate hooked on %s prev=%p\n",
                class_getName(dispClass), (void *)orig_nww);
    }
    ww = objc_getClass("WaylandWindow");
    if (ww && orig_wwinit == 0) {
        orig_wwinit = class_replaceMethod(ww, S("initWithDelegate:"), (IMP)my_wwinit, "@24@0:8@16");
        orig_wwsurface = class_replaceMethod(ww, S("surface"), (IMP)my_wwsurface, "^v16@0:8");
        dprintf(2, "WLPROBE: WaylandWindow init+surface hooked\n");
    }
    if (orig_nww && orig_wwinit)
        wl_hooks_done = 1;
}

static id my_curdisp(id self, SEL cmd)
{
    id d = orig_curdisp ? ((id (*)(id, SEL))orig_curdisp)(self, cmd) : 0;
    if (d) {
        dprintf(2, "WLPROBE: currentDisplay class=%s\n", class_getName(object_getClass(d)));
        wl_install(object_getClass(d));
    }
    return d;
}

static void my_run(id self, SEL cmd)
{
    say("NSSTART-PROBE: NSApplication run enter\n");
    ((void (*)(id, SEL))orig_run)(self, cmd);
    say("NSSTART-PROBE: NSApplication run exit\n");
}

static long my_modal(id self, SEL cmd)
{
    say("NSSTART-PROBE: NSAlert runModal enter\n");
    return ((long (*)(id, SEL))orig_modal)(self, cmd);
}

static void probe_add(void)
{
    Class ca, cw;
    if (probe_done)
        return;
    ca = objc_getClass("NSApplication");
    cw = objc_getClass("NSWindow");
    if (!ca || !cw)
        return;
    orig_finish = class_replaceMethod(ca, S("finishLaunching"), (IMP)my_finish, "v16@0:8");
    orig_run = class_replaceMethod(ca, S("run"), (IMP)my_run, "v16@0:8");
    orig_pwin = class_replaceMethod(cw, S("platformWindow"), (IMP)my_pwin, "@16@0:8");
    orig_create = class_replaceMethod(cw, S("_createPlatformWindowOnMainThread"), (IMP)my_create, "v16@0:8");
    orig_isvis = class_replaceMethod(cw, S("isVisible"), (IMP)my_isvis, "c16@0:8");
    orig_occ = class_replaceMethod(cw, S("occlusionState"), (IMP)my_occ, "Q16@0:8");
    orig_orderfront = class_replaceMethod(cw, S("orderFront:"), (IMP)my_orderfront, "v24@0:8@16");
    orig_ofr = class_replaceMethod(cw, S("orderFrontRegardless"), (IMP)my_ofr, "v16@0:8");
    orig_mkaf = class_replaceMethod(cw, S("makeKeyAndOrderFront:"), (IMP)my_mkaf, "v24@0:8@16");
    orig_owr = class_replaceMethod(cw, S("orderWindow:relativeTo:"), (IMP)my_owr, "v32@0:8q16q24");
    orig_setvis = class_replaceMethod(cw, S("setIsVisible:"), (IMP)my_setvis, "v20@0:8c16");
    {
        Class na = objc_getClass("NSAlert");
        if (na)
            orig_modal = class_replaceMethod(na, S("runModal"), (IMP)my_modal, "q16@0:8");
    }
    {
        wl_install(0);
    }
    probe_done = 1;
    if (orig_finish && orig_pwin && orig_create)
        say("NSSTART-PROBE installed\n");
    else
        say("NSSTART-PROBE: a selector missing, probe partial\n");
}

/* The classes are registered only once AppKit itself is loaded: same retry pattern as the Wayland
 * event shim - try now, then on every image, arming on the AppKit image. */
static void probe_on_image(const struct mach_header *mh, long slide)
{
    Dl_info_x di;
    static int armed;
    (void)slide;
    if (!wl_hooks_done)
        wl_install(0);
    if (probe_done)
        return;
    if (!armed) {
        if (!dladdr(mh, &di) || !di.dli_fname || !strstr(di.dli_fname, "AppKit.framework/Versions/C/AppKit"))
            return;
        armed = 1;
    }
    probe_add();
}

__attribute__((constructor)) static void nsstart_probe_install(void)
{
    probe_add();
    if (!probe_done)
        _dyld_register_func_for_add_image(probe_on_image);
}
/* END_APPKIT_NSSTART_PROBE */
