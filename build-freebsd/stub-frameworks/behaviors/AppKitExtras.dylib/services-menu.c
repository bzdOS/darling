/* AppKitExtras behaviour: the private services-menu classes Chromium swizzles.
 *
 * Chromium's services-menu code looks up _NSServiceEntry and _NSServicesMenuUpdater with
 * NSClassFromString and CHECKs that their instances respond to bundleIdentifier,
 * populateMenu:withServiceEntries:forDisplay: and plugMenu:intoMenu: before exchanging
 * implementations; a missing class or selector reaches the CHECK trap. This AppKit has
 * neither class. They are registered at load time as plain NSObject subclasses whose
 * methods do nothing (no services are offered), so the check passes and the swizzled
 * methods are never asked for anything. Classes that already exist are left alone. */
#include "../objc-proto.h"
extern Class objc_allocateClassPair(Class super, const char *name, unsigned long extra);
extern void objc_registerClassPair(Class cls);
extern unsigned char class_addMethod(Class cls, SEL name, IMP imp, const char *types);

static void *ret_nil(void *self, SEL cmd) { (void)self; (void)cmd; return 0; }
static void nop3(void *self, SEL cmd, void *a, void *b, void *c) { (void)self; (void)cmd; (void)a; (void)b; (void)c; }
static void nop2(void *self, SEL cmd, void *a, void *b) { (void)self; (void)cmd; (void)a; (void)b; }

static void add_class(const char *name, const char *sel, IMP imp, const char *types)
{
    Class c;
    if (objc_getClass(name))
        return;
    c = objc_allocateClassPair(objc_getClass("NSObject"), name, 0);
    if (!c)
        return;
    class_addMethod(c, sel_registerName(sel), imp, types);
    objc_registerClassPair(c);
}

__attribute__((constructor)) static void appkit_extras_services_menu(void)
{
    add_class("_NSServiceEntry", "bundleIdentifier", (IMP)ret_nil, "@16@0:8");
    add_class("_NSServicesMenuUpdater", "populateMenu:withServiceEntries:forDisplay:", (IMP)nop3, "v40@0:8@16@24@32");
    {
        Class u = objc_getClass("_NSServicesMenuUpdater");
        if (u)
            class_addMethod(u, sel_registerName("plugMenu:intoMenu:"), (IMP)nop2, "v32@0:8@16@24");
    }
}
