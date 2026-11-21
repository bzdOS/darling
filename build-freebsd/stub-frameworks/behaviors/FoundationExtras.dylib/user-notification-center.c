/* FoundationExtras behaviour: -[NSUserNotificationCenter deliveredNotifications].
 *
 * Chromium reads the delivered notifications at browser start. This Foundation's NSUserNotificationCenter has
 * no such method, so the forward handler answered nil (OBJC-FWD-MISS in fix54/fix55, a few lines before the
 * CHECK at ChromeFW+0x1e3a378). macOS answers an array, empty when nothing was delivered: that is returned here
 * (a fresh autoreleased empty NSArray). The method is added once the class exists (add-image hook). */
#include "../objc-proto.h"
extern unsigned char class_addMethod(Class cls, SEL name, IMP imp, const char *types);
struct mach_header;
extern void _dyld_register_func_for_add_image(void (*func)(const struct mach_header *, long));

static int done;

static id delivered_notifications(id self, SEL cmd)
{
    Class a = objc_getClass("NSArray");
    (void)self; (void)cmd;
    return a ? MSG(id, id, SEL)((id)a, S("array")) : (id)0;
}

static void try_add(void)
{
    Class c;
    if (done)
        return;
    c = objc_getClass("NSUserNotificationCenter");
    if (!c || !objc_getClass("NSArray"))
        return;
    class_addMethod(c, S("deliveredNotifications"), (IMP)delivered_notifications, "@16@0:8");
    done = 1;
}

static void on_image(const struct mach_header *mh, long slide) { (void)mh; (void)slide; try_add(); }

__attribute__((constructor)) static void foundation_extras_user_notification_center(void)
{
    try_add();
    if (!done)
        _dyld_register_func_for_add_image(on_image);
}
