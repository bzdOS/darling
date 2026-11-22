/* FoundationExtras behaviour: -[NSFileManager ubiquityIdentityToken] and the com.apple.dock defaults domain.
 *
 * ubiquityIdentityToken: the Foundation in this tree declares it without an implementation. Chromium reads it to learn
 * whether iCloud is signed in; nil is the documented answer for "not signed in". Without the method the message went to
 * the forward handler (an OBJC-FWD-MISS line).
 *
 * persistentDomainForName: Chromium's dock code (chrome/browser/mac/dock.mm) reads
 * [[NSUserDefaults standardUserDefaults] persistentDomainForName:@"com.apple.dock"] and logs "dock_plist is not an
 * NSDictionary" when the answer is nil, which it is here: no Dock, no preferences daemon. The method is wrapped: the original
 * answer is kept; only for the com.apple.dock domain, and only when the original gives nil, a dictionary with empty NSArrays under persistent-apps and persistent-others (a Dock
 * without persistent apps) is returned. Every other domain keeps its original behaviour.
 * The methods are installed once their classes exist (add-image hook, as the other behaviours of this image). */
#include "../objc-proto.h"
extern unsigned char class_addMethod(Class cls, SEL name, IMP imp, const char *types);
extern IMP class_replaceMethod(Class cls, SEL name, IMP imp, const char *types);
struct mach_header;
extern void _dyld_register_func_for_add_image(void (*func)(const struct mach_header *, long));

static int fm_done, ud_done;
static id (*orig_persistent_domain)(id, SEL, id);

static id ubiquity_identity_token(id self, SEL cmd)
{
    (void)self; (void)cmd;
    return 0;
}

static id persistent_domain_for_name(id self, SEL cmd, id name)
{
    id r = orig_persistent_domain ? orig_persistent_domain(self, cmd, name) : 0;
    if (!r && name) {
        id dock = objc_getClass("NSString") ?
            MSG(id, id, SEL, const char *)((id)objc_getClass("NSString"), S("stringWithUTF8String:"), "com.apple.dock") : (id)0;
        Class d = objc_getClass("NSDictionary");
        if (dock && d && (unsigned char)(long)MSG(long, id, SEL, id)(name, S("isEqualToString:"), dock))
        {
            id arr = MSG(id, id, SEL)((id)objc_getClass("NSArray"), S("array"));
            id k1 = MSG(id, id, SEL, const char *)((id)objc_getClass("NSString"), S("stringWithUTF8String:"), "persistent-apps");
            id k2 = MSG(id, id, SEL, const char *)((id)objc_getClass("NSString"), S("stringWithUTF8String:"), "persistent-others");
            return MSG(id, id, SEL, id, id, id, id, id)((id)d, S("dictionaryWithObjectsAndKeys:"), arr, k1, arr, k2, (id)0);
        }
    }
    return r;
}

static void try_add(void)
{
    Class c;
    if (!fm_done && (c = objc_getClass("NSFileManager"))) {
        class_addMethod(c, S("ubiquityIdentityToken"), (IMP)ubiquity_identity_token, "@16@0:8");
        fm_done = 1;
    }
    if (!ud_done && (c = objc_getClass("NSUserDefaults"))) {
        orig_persistent_domain = (id (*)(id, SEL, id))class_replaceMethod(c, S("persistentDomainForName:"),
                                                                          (IMP)persistent_domain_for_name, "@24@0:8@16");
        ud_done = 1;
    }
}

static void on_image(const struct mach_header *mh, long slide) { (void)mh; (void)slide; try_add(); }

__attribute__((constructor)) static void foundation_extras_filemanager(void)
{
    try_add();
    if (!fm_done || !ud_done)
        _dyld_register_func_for_add_image(on_image);
}
