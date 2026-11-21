/* AuthenticationServicesExtras behaviour: +[ASWebAuthenticationSessionWebBrowserSessionManager sharedManager].
 *
 * Chromium asks this class for its shared manager at browser start and sets its session handler. The stub
 * class had no methods, so the forward handler answered nil (OBJC-FWD-MISS line in the log). The answer here
 * is one long-lived object of DarlingASWebBrowserSessionManager (an NSObject subclass made at first use) with
 * the properties Chromium touches: sessionHandler (strong, kept for the process lifetime) and
 * wasLaunchedByAuthenticationServices (NO). No authentication session is ever offered.
 * fix55 showed the nil answer was not the cause of the CHECK at ChromeFW+0x1e3a378; this is kept because it is
 * closer to macOS (coordinator decision). */
#include "../objc-proto.h"
extern unsigned char class_addMethod(Class cls, SEL name, IMP imp, const char *types);
extern Class object_getClass(id obj);
extern Class objc_allocateClassPair(Class superclass, const char *name, unsigned long extraBytes);
extern void objc_registerClassPair(Class cls);
extern id objc_retain(id obj);
extern void objc_release(id obj);
struct mach_header;
extern void _dyld_register_func_for_add_image(void (*func)(const struct mach_header *, long));

static id shared;
static id handler;
static int done;

static void set_session_handler(id self, SEL cmd, id h)
{
    (void)self; (void)cmd;
    if (h)
        objc_retain(h);
    if (handler)
        objc_release(handler);
    handler = h;
}
static id session_handler(id self, SEL cmd) { (void)self; (void)cmd; return handler; }
static signed char launched_by_as(id self, SEL cmd) { (void)self; (void)cmd; return 0; }

static id shared_manager(id self, SEL cmd)
{
    (void)self; (void)cmd;
    if (!shared) {
        Class ns = objc_getClass("NSObject");
        Class c = objc_getClass("DarlingASWebBrowserSessionManager");
        if (!c && ns) {
            c = objc_allocateClassPair(ns, "DarlingASWebBrowserSessionManager", 0);
            if (c) {
                class_addMethod(c, S("setSessionHandler:"), (IMP)set_session_handler, "v24@0:8@16");
                class_addMethod(c, S("sessionHandler"), (IMP)session_handler, "@16@0:8");
                class_addMethod(c, S("wasLaunchedByAuthenticationServices"), (IMP)launched_by_as, "c16@0:8");
                objc_registerClassPair(c);
            }
        }
        if (c) {
            id o = MSG(id, id, SEL)((id)c, S("alloc"));
            shared = MSG(id, id, SEL)(o, S("init"));
        }
    }
    return shared;
}

static void try_add(void)
{
    Class c;
    if (done)
        return;
    c = objc_getClass("ASWebAuthenticationSessionWebBrowserSessionManager");
    if (!c)
        return;
    /* class method: added to the metaclass */
    class_addMethod(object_getClass((id)c), S("sharedManager"), (IMP)shared_manager, "@16@0:8");
    done = 1;
}

static void on_image(const struct mach_header *mh, long slide) { (void)mh; (void)slide; try_add(); }

__attribute__((constructor)) static void authservices_extras_web_browser_session(void)
{
    try_add();
    if (!done)
        _dyld_register_func_for_add_image(on_image);
}
