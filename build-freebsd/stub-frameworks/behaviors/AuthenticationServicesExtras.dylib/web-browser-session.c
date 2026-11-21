/* AuthenticationServicesExtras behaviour: +[ASWebAuthenticationSessionWebBrowserSessionManager sharedManager].
 *
 * Chromium asks this class for its shared manager at browser start. The stub class had no methods, so the
 * forward handler answered nil (OBJC-FWD-MISS line in the log), and every Chromium run from fix24 on stopped
 * right after it at a CHECK, SIGTRAP at ChromeFW+0x1e3a378. The answer here is one long-lived object; any other
 * message to it (setSessionHandler:, wasLaunchedByAuthenticationServices, ...) goes to the forward handler of
 * FoundationExtras and gets nil / NO, i.e. no authentication session is ever offered.
 * The object is a plain NSObject instance so retain/release/autorelease behave; it is created once and never freed. */
#include "../objc-proto.h"
extern unsigned char class_addMethod(Class cls, SEL name, IMP imp, const char *types);
extern Class object_getClass(id obj);
struct mach_header;
extern void _dyld_register_func_for_add_image(void (*func)(const struct mach_header *, long));

static id shared;
static int done;

static id shared_manager(id self, SEL cmd)
{
    (void)self; (void)cmd;
    if (!shared) {
        Class ns = objc_getClass("NSObject");
        if (ns) {
            id o = MSG(id, id, SEL)((id)ns, S("alloc"));
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
    class_addMethod(object_getClass((id)c), sel_registerName("sharedManager"), (IMP)shared_manager, "@16@0:8");
    done = 1;
}

static void on_image(const struct mach_header *mh, long slide) { (void)mh; (void)slide; try_add(); }

__attribute__((constructor)) static void authservices_extras_web_browser_session(void)
{
    try_add();
    if (!done)
        _dyld_register_func_for_add_image(on_image);
}
