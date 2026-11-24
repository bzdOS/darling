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

static IMP orig_finish, orig_pwin, orig_create;

static void say(const char *m)
{
    const char *p = m;
    while (*p)
        p++;
    write(2, m, p - m);
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

extern int dprintf(int, const char *, ...);

/* Order-front probe: forcing makeKeyAndOrderFront/orderFrontRegardless from this
 * hook crashes a Chrome Framework notification observer (SIGSEGV addr=0xe0, rdi=0x0,
 * null deref at +0xe0 in ChromeFW during NSWindowDidUpdateNotification posted by
 * -[NSWindow update]). Report window state without forcing the crashing path. */
static void my_create(id self, SEL cmd)
{
    signed char visible;
    unsigned long occ;
    say("NSSTART-PROBE: _createPlatformWindowOnMainThread enter\n");
    ((void (*)(id, SEL))orig_create)(self, cmd);
    say("NSSTART-PROBE: _createPlatformWindowOnMainThread exit\n");
    visible = MSG(signed char, id, SEL)(self, S("isVisible"));
    occ = MSG(unsigned long, id, SEL)(self, S("occlusionState"));
    dprintf(2, "ORDERFRONT-PROBE: visible=%d occlusion=%lu\n", (int)visible, occ);
}

static IMP orig_run, orig_modal;

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

static int probe_done;

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
    {
        Class na = objc_getClass("NSAlert");
        if (na)
            orig_modal = class_replaceMethod(na, S("runModal"), (IMP)my_modal, "q16@0:8");
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
