/* AppKitExtras behaviour: the NSWindow / NSScroller / NSView methods that this AppKit declares but implements as
 * NSUnimplementedMethod() (a log line, then a zero or nothing), replaced here with working answers.
 *   -[NSWindow isOnActiveSpace]                   YES (there are no Spaces: the window is always on the active one)
 *   -[NSWindow collectionBehavior] / -setCollectionBehavior:   the value is stored (associated object), default 0
 *   +[NSScroller preferredScrollerStyle]          NSScrollerStyleLegacy (0): classic always-visible scrollers
 *   -[NSView sortSubviewsUsingFunction:context:]  the subviews array is put in the order the function gives
 *                                                  (stable sort of the array's own sortedArrayUsingFunction:context:)
 * Installed with class_replaceMethod once AppKit is mapped (add-image retry as in alert.c). */
#include "../objc-proto.h"
#include <stddef.h>
extern Class object_getClass(id obj);
extern IMP class_replaceMethod(Class cls, SEL name, IMP imp, const char *types);
extern void *class_getInstanceVariable(Class cls, const char *name);
extern long ivar_getOffset(void *iv);
extern void objc_setAssociatedObject(id object, const void *key, id value, unsigned long policy);
extern id objc_getAssociatedObject(id object, const void *key);
struct mach_header;
extern void _dyld_register_func_for_add_image(void (*func)(const struct mach_header *, long));
typedef struct { const char *dli_fname; void *dli_fbase; const char *dli_sname; void *dli_saddr; } Dl_info_c;
extern int dladdr(const void *, Dl_info_c *);
extern char *strstr(const char *, const char *);

#define ASSOC_RETAIN 01401
static char key_collection;
static int done;

static signed char win_on_active_space(id self, SEL cmd) { (void)self; (void)cmd; return 1; }

static unsigned long win_collection_behavior(id self, SEL cmd)
{
    id n = objc_getAssociatedObject(self, &key_collection);
    (void)cmd;
    return n ? MSG(unsigned long, id, SEL)(n, S("unsignedIntegerValue")) : 0;
}

static void win_set_collection_behavior(id self, SEL cmd, unsigned long v)
{
    id n = MSG(id, id, SEL, unsigned long)((id)objc_getClass("NSNumber"), S("numberWithUnsignedInteger:"), v);
    (void)cmd;
    objc_setAssociatedObject(self, &key_collection, n, ASSOC_RETAIN);
}

static long scroller_preferred_style(id self, SEL cmd) { (void)self; (void)cmd; return 0; /* NSScrollerStyleLegacy */ }

static void view_sort_subviews(id self, SEL cmd, void *fn, void *ctx)
{
    id subs, sorted;
    void *iv;
    (void)cmd;
    if (!fn)
        return;
    subs = MSG(id, id, SEL)(self, S("subviews"));
    if (!subs)
        return;
    sorted = MSG(id, id, SEL, void *, void *)(subs, S("sortedArrayUsingFunction:context:"), fn, ctx);
    iv = class_getInstanceVariable(object_getClass(self), "_subviews");
    if (iv && sorted) {
        id *slot = (id *)((char *)self + ivar_getOffset(iv));
        if (*slot)
            MSG(void, id, SEL, id)(*slot, S("setArray:"), sorted);
    }
    MSG(void, id, SEL, signed char)(self, S("setNeedsDisplay:"), 1);
}

static void try_add(void)
{
    Class w = objc_getClass("NSWindow"), s = objc_getClass("NSScroller"), v = objc_getClass("NSView");
    if (done || !w || !s || !v)
        return;
    class_replaceMethod(w, S("isOnActiveSpace"), (IMP)win_on_active_space, "c16@0:8");
    class_replaceMethod(w, S("collectionBehavior"), (IMP)win_collection_behavior, "Q16@0:8");
    class_replaceMethod(w, S("setCollectionBehavior:"), (IMP)win_set_collection_behavior, "v24@0:8Q16");
    class_replaceMethod(object_getClass((id)s), S("preferredScrollerStyle"), (IMP)scroller_preferred_style, "q16@0:8");
    class_replaceMethod(v, S("sortSubviewsUsingFunction:context:"), (IMP)view_sort_subviews, "v32@0:8^?16^v24");
    done = 1;
}

static void on_image(const struct mach_header *mh, long slide)
{
    Dl_info_c di;
    static int armed;
    (void)slide;
    if (done)
        return;
    if (!armed) {
        if (!dladdr(mh, &di) || !di.dli_fname || !strstr(di.dli_fname, "AppKit.framework/Versions/C/AppKit"))
            return;
        armed = 1;
    }
    try_add();
}

__attribute__((constructor)) static void appkit_extras_census(void)
{
    try_add();
    if (!done)
        _dyld_register_func_for_add_image(on_image);
}
