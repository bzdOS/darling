/* AppKitExtras behaviour: the NSWindow / NSEvent / NSApplication / NSImage selectors the Chrome framework sends and this AppKit
 * does not implement (each came back through the forward handler as OBJC-FWD-MISS and a zero).
 *
 * Added once AppKit is mapped (add-image retry as in screen.c and calayer.c); an existing method of the same name is left alone
 * (class_addMethod does not replace). The classes carry no room for new ivars, so every stored value is an associated object
 * on the receiver.
 *   NSWindow: -setTitlebarAppearsTransparent: -setAnimationBehavior: -addTitlebarAccessoryViewController: (retained, kept in an
 *     array) -convertPointFromScreen: (through -convertRectFromScreen: when the window answers it, else point - frame.origin)
 *   NSTitlebarAccessoryViewController: -setFullScreenMinHeight: -setHidden: -setLayoutAttribute: (stored)
 *   +[NSEvent pressedMouseButtons] = 0 (no button held)
 *   -[NSApplication isFullKeyboardAccessEnabled] = NO (on the class, so the Chrome subclass inherits it)
 *   +[NSImage imageWithSystemSymbolName:accessibilityDescription:] = nil (no SF Symbols)
 *   +[NSAccessibilityRemoteUIElement remoteTokenForLocalUIElement:] = empty NSData (skipped when AppKit has no such class)
 * x86_64 ABI: an NSPoint (two doubles) is returned in xmm0/xmm1, hence a struct of two doubles as the C return type; an NSRect
 * (four doubles) is returned in memory, hence objc_msgSend_stret. */
/* START_APPKIT_WINDOW_FILL */
typedef void *Class;
typedef void *SEL;
typedef void *IMP;
typedef void *id;
struct ak_point { double x, y; };
struct ak_rect { double x, y, w, h; };
extern Class objc_getClass(const char *name);
extern Class object_getClass(id obj);
extern SEL sel_registerName(const char *name);
extern unsigned char class_addMethod(Class cls, SEL name, IMP imp, const char *types);
extern unsigned char class_respondsToSelector(Class cls, SEL sel);
extern id objc_msgSend(id, SEL, ...);
extern void objc_msgSend_stret(void *, id, SEL, ...);
extern void objc_setAssociatedObject(id object, const void *key, id value, unsigned long policy);
extern id objc_getAssociatedObject(id object, const void *key);
struct mach_header;
extern void _dyld_register_func_for_add_image(void (*func)(const struct mach_header *, long));

#define MSG(ret, ...) ((ret (*)(__VA_ARGS__))objc_msgSend)
#define ASSOC_RETAIN 01401

static char key_transparent, key_animation, key_accessories, key_minheight, key_hidden, key_layout;

static void keep_bool(id self, const void *key, signed char v)
{
    id n = MSG(id, id, SEL, signed char)(objc_getClass("NSNumber"), sel_registerName("numberWithBool:"), v);
    objc_setAssociatedObject(self, key, n, ASSOC_RETAIN);
}
static void keep_long(id self, const void *key, long v)
{
    id n = MSG(id, id, SEL, long)(objc_getClass("NSNumber"), sel_registerName("numberWithInteger:"), v);
    objc_setAssociatedObject(self, key, n, ASSOC_RETAIN);
}
static void keep_double(id self, const void *key, double v)
{
    id n = MSG(id, id, SEL, double)(objc_getClass("NSNumber"), sel_registerName("numberWithDouble:"), v);
    objc_setAssociatedObject(self, key, n, ASSOC_RETAIN);
}

static void win_set_transparent(id self, SEL cmd, signed char f) { (void)cmd; keep_bool(self, &key_transparent, f); }
static void win_set_animation(id self, SEL cmd, long v) { (void)cmd; keep_long(self, &key_animation, v); }
static void win_add_accessory(id self, SEL cmd, id vc)
{
    id arr = objc_getAssociatedObject(self, &key_accessories);
    (void)cmd;
    if (!arr) {
        arr = MSG(id, id, SEL)(objc_getClass("NSMutableArray"), sel_registerName("array"));
        objc_setAssociatedObject(self, &key_accessories, arr, ASSOC_RETAIN);
    }
    if (arr && vc)
        MSG(void, id, SEL, id)(arr, sel_registerName("addObject:"), vc);   /* the array retains the controller */
}
static struct ak_point win_convert_point_from_screen(id self, SEL cmd, struct ak_point p)
{
    struct ak_point r;
    struct ak_rect in, out;
    (void)cmd;
    in.x = p.x; in.y = p.y; in.w = 0; in.h = 0;
    if (class_respondsToSelector(object_getClass(self), sel_registerName("convertRectFromScreen:"))) {
        objc_msgSend_stret(&out, self, sel_registerName("convertRectFromScreen:"), in);
        r.x = out.x; r.y = out.y;
    } else {
        objc_msgSend_stret(&out, self, sel_registerName("frame"));
        r.x = p.x - out.x; r.y = p.y - out.y;
    }
    return r;
}

static void tb_set_minheight(id self, SEL cmd, double v) { (void)cmd; keep_double(self, &key_minheight, v); }
static void tb_set_hidden(id self, SEL cmd, signed char f) { (void)cmd; keep_bool(self, &key_hidden, f); }
static void tb_set_layout(id self, SEL cmd, long v) { (void)cmd; keep_long(self, &key_layout, v); }

static unsigned long ev_pressed_buttons(id self, SEL cmd) { (void)self; (void)cmd; return 0; }
static signed char app_full_keyboard(id self, SEL cmd) { (void)self; (void)cmd; return 0; }
static id img_symbol(id self, SEL cmd, id name, id desc) { (void)self; (void)cmd; (void)name; (void)desc; return 0; }
static id ax_remote_token(id self, SEL cmd, id elem)
{
    (void)self; (void)cmd; (void)elem;
    return MSG(id, id, SEL)(objc_getClass("NSData"), sel_registerName("data"));
}

static int done;

static void try_add(void)
{
    Class win, tb, ev, app, img, ax;
    if (done)
        return;
    win = objc_getClass("NSWindow");
    tb = objc_getClass("NSTitlebarAccessoryViewController");
    ev = objc_getClass("NSEvent");
    app = objc_getClass("NSApplication");
    img = objc_getClass("NSImage");
    ax = objc_getClass("NSAccessibilityRemoteUIElement");   /* absent: skipped, never created here */
    if (!win || !tb || !ev || !app || !img)
        return;
    class_addMethod(win, sel_registerName("setTitlebarAppearsTransparent:"), (IMP)win_set_transparent, "v20@0:8c16");
    class_addMethod(win, sel_registerName("setAnimationBehavior:"), (IMP)win_set_animation, "v24@0:8q16");
    class_addMethod(win, sel_registerName("addTitlebarAccessoryViewController:"), (IMP)win_add_accessory, "v24@0:8@16");
    class_addMethod(win, sel_registerName("convertPointFromScreen:"), (IMP)win_convert_point_from_screen, "{CGPoint=dd}32@0:8{CGPoint=dd}16");
    class_addMethod(tb, sel_registerName("setFullScreenMinHeight:"), (IMP)tb_set_minheight, "v24@0:8d16");
    class_addMethod(tb, sel_registerName("setHidden:"), (IMP)tb_set_hidden, "v20@0:8c16");
    class_addMethod(tb, sel_registerName("setLayoutAttribute:"), (IMP)tb_set_layout, "v24@0:8q16");
    class_addMethod(object_getClass((id)ev), sel_registerName("pressedMouseButtons"), (IMP)ev_pressed_buttons, "Q16@0:8");
    class_addMethod(app, sel_registerName("isFullKeyboardAccessEnabled"), (IMP)app_full_keyboard, "c16@0:8");
    class_addMethod(object_getClass((id)img), sel_registerName("imageWithSystemSymbolName:accessibilityDescription:"), (IMP)img_symbol, "@32@0:8@16@24");
    if (ax)
        class_addMethod(object_getClass((id)ax), sel_registerName("remoteTokenForLocalUIElement:"), (IMP)ax_remote_token, "@24@0:8@16");
    done = 1;
}

static void on_image(const struct mach_header *mh, long slide) { (void)mh; (void)slide; try_add(); }

__attribute__((constructor)) static void appkit_extras_window(void)
{
    try_add();
    if (!done)
        _dyld_register_func_for_add_image(on_image);
}
/* END_APPKIT_WINDOW_FILL */
