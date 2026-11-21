/* darling-extras behaviour: the CALayer / CALayerContext selectors the Chrome framework sends and the QuartzCore stub does not answer.
 * Measured as OBJC-FWD-MISS in every run of the fix25 series: +[CALayer layer] (inherited by +[NSViewBackingLayer layer]),
 * -[CALayer setAnchorPoint:], -[CALayer setAutoresizingMask:], -[CALayer setGeometryFlipped:],
 * -[CALayerContext setLayer:], -[CALayerContext setSubwindow:].
 *
 * The QuartzCore image of the overlay has no generator source in this tree (the stub is hand written: -[CALayer delegate],
 * +[CATransaction begin] ...), and its classes carry no room for new ivars, so each value is kept with an associated object
 * on the receiver, as catransaction.c adds its methods once QuartzCore is mapped (add-image retry).
 *
 * Taken from the cocotron QuartzCore sources (src/external/cocotron/QuartzCore):
 *   CALayer.m:85    -setAnchorPoint: stores the point; the default of a new layer is (0.5, 0.5) (CALayer.m:224);
 *   CALayerContext.m:53 -setLayer: keeps the layer retained and releases the previous one (here: the RETAIN association);
 *   CALayerContext.m:62 -setSubwindow: keeps the subwindow retained and releases the previous one (same).
 * Not taken: the context hand-off (_setContext:, the renderer, CGL window, show, setFrame:), which needs the cocotron
 * CARenderer / CGL stack that the stub does not link.
 * Written new: +layer (cocotron has none; an initialised instance with the default anchor), -setAutoresizingMask:,
 * -setGeometryFlipped: (not in cocotron either).
 * An existing method of the same name in the image is left alone (class_addMethod does not replace). */
/* START_CALAYER_FILL */
#include "../objc-proto.h"
struct qc_point { double x, y; };
extern Class object_getClass(id obj);
extern unsigned char class_addMethod(Class cls, SEL name, IMP imp, const char *types);
extern void objc_setAssociatedObject(id object, const void *key, id value, unsigned long policy);
extern id objc_getAssociatedObject(id object, const void *key);
struct mach_header;
extern void _dyld_register_func_for_add_image(void (*func)(const struct mach_header *, long));

#define ASSOC_RETAIN 01401
#define ASSOC_ASSIGN 0

static char key_ax, key_ay, key_mask, key_flip, key_layer, key_sub;
static char key_sublayers, key_superlayer, key_bounds, key_position, key_bgcolor;
struct qc_rect { double x, y, w, h; };

static void keep_double(id self, const void *key, double v)
{
    id n = MSG(id, id, SEL, double)(objc_getClass("NSNumber"), sel_registerName("numberWithDouble:"), v);
    objc_setAssociatedObject(self, key, n, ASSOC_RETAIN);
}

static void layer_set_anchor(id self, SEL cmd, struct qc_point p)
{
    (void)cmd;
    keep_double(self, &key_ax, p.x);
    keep_double(self, &key_ay, p.y);
}
static void layer_set_mask(id self, SEL cmd, unsigned int mask)
{
    id n = MSG(id, id, SEL, unsigned int)(objc_getClass("NSNumber"), sel_registerName("numberWithUnsignedInt:"), mask);
    (void)cmd;
    objc_setAssociatedObject(self, &key_mask, n, ASSOC_RETAIN);
}
static void layer_set_flipped(id self, SEL cmd, signed char flag)
{
    id n = MSG(id, id, SEL, signed char)(objc_getClass("NSNumber"), sel_registerName("numberWithBool:"), flag);
    (void)cmd;
    objc_setAssociatedObject(self, &key_flip, n, ASSOC_RETAIN);
}
static id layer_new(id self, SEL cmd)
{
    struct qc_point p = { 0.5, 0.5 };
    id o = MSG(id, id, SEL)(self, sel_registerName("alloc"));
    (void)cmd;
    if (o)
        o = MSG(id, id, SEL)(o, sel_registerName("init"));
    if (o)
        layer_set_anchor(o, 0, p);
    return o;
}
static void context_set_layer(id self, SEL cmd, id layer)
{
    (void)cmd;
    objc_setAssociatedObject(self, &key_layer, layer, ASSOC_RETAIN);
}
static void context_set_subwindow(id self, SEL cmd, id sub)
{
    (void)cmd;
    objc_setAssociatedObject(self, &key_sub, sub, ASSOC_RETAIN);
}

/* Layer tree and geometry the Chrome framework sets (also reached through NSViewBackingLayer, which inherits CALayer).
 * The sublayer array is kept on the receiver (RETAIN, the array retains the children); the child's superlayer is an ASSIGN
 * association (no retain cycle; it reads back as the parent while the parent lives, as in cocotron CALayer.m -addSublayer:).
 * The background colour is a CGColorRef that this image cannot CFRetain (CoreGraphics is not linked), so only the pointer is kept,
 * boxed with NSValue; the caller keeps the colour alive. */
static void layer_add_sublayer(id self, SEL cmd, id child)
{
    id arr, old;
    (void)cmd;
    if (!child)
        return;
    old = objc_getAssociatedObject(child, &key_superlayer);
    if (old && old != self) {
        id oarr = objc_getAssociatedObject(old, &key_sublayers);
        if (oarr)
            MSG(void, id, SEL, id)(oarr, sel_registerName("removeObject:"), child);
    }
    arr = objc_getAssociatedObject(self, &key_sublayers);
    if (!arr) {
        arr = MSG(id, id, SEL)(objc_getClass("NSMutableArray"), sel_registerName("array"));
        objc_setAssociatedObject(self, &key_sublayers, arr, ASSOC_RETAIN);
    }
    if (old != self)
        MSG(void, id, SEL, id)(arr, sel_registerName("addObject:"), child);
    objc_setAssociatedObject(child, &key_superlayer, self, ASSOC_ASSIGN);
}
static id layer_superlayer(id self, SEL cmd)
{
    (void)cmd;
    return objc_getAssociatedObject(self, &key_superlayer);
}
static void layer_set_bounds(id self, SEL cmd, struct qc_rect r)
{
    id v = MSG(id, id, SEL, const void *, const char *)(objc_getClass("NSValue"), sel_registerName("valueWithBytes:objCType:"), &r, "{CGRect={CGPoint=dd}{CGSize=dd}}");
    (void)cmd;
    objc_setAssociatedObject(self, &key_bounds, v, ASSOC_RETAIN);
}
static void layer_set_position(id self, SEL cmd, struct qc_point p)
{
    id v = MSG(id, id, SEL, const void *, const char *)(objc_getClass("NSValue"), sel_registerName("valueWithBytes:objCType:"), &p, "{CGPoint=dd}");
    (void)cmd;
    objc_setAssociatedObject(self, &key_position, v, ASSOC_RETAIN);
}
static void layer_set_bgcolor(id self, SEL cmd, void *color)
{
    id v = MSG(id, id, SEL, void *)(objc_getClass("NSValue"), sel_registerName("valueWithPointer:"), color);
    (void)cmd;
    objc_setAssociatedObject(self, &key_bgcolor, v, ASSOC_RETAIN);
}

static int done;

static void try_add(void)
{
    Class layer, context;
    if (done)
        return;
    layer = objc_getClass("CALayer");
    context = objc_getClass("CALayerContext");
    if (!layer || !context)
        return;
    class_addMethod(object_getClass((id)layer), sel_registerName("layer"), (IMP)layer_new, "@16@0:8");
    class_addMethod(layer, sel_registerName("setAnchorPoint:"), (IMP)layer_set_anchor, "v32@0:8{CGPoint=dd}16");
    class_addMethod(layer, sel_registerName("setAutoresizingMask:"), (IMP)layer_set_mask, "v20@0:8I16");
    class_addMethod(layer, sel_registerName("setGeometryFlipped:"), (IMP)layer_set_flipped, "v20@0:8c16");
    class_addMethod(context, sel_registerName("setLayer:"), (IMP)context_set_layer, "v24@0:8@16");
    class_addMethod(context, sel_registerName("setSubwindow:"), (IMP)context_set_subwindow, "v24@0:8@16");
    class_addMethod(layer, sel_registerName("addSublayer:"), (IMP)layer_add_sublayer, "v24@0:8@16");
    class_addMethod(layer, sel_registerName("superlayer"), (IMP)layer_superlayer, "@16@0:8");
    class_addMethod(layer, sel_registerName("setBounds:"), (IMP)layer_set_bounds, "v48@0:8{CGRect={CGPoint=dd}{CGSize=dd}}16");
    class_addMethod(layer, sel_registerName("setPosition:"), (IMP)layer_set_position, "v32@0:8{CGPoint=dd}16");
    class_addMethod(layer, sel_registerName("setBackgroundColor:"), (IMP)layer_set_bgcolor, "v24@0:8^{CGColor=}16");
    done = 1;
}

static void on_image(const struct mach_header *mh, long slide) { (void)mh; (void)slide; try_add(); }

__attribute__((constructor)) static void darling_extras_calayer(void)
{
    try_add();
    if (!done)
        _dyld_register_func_for_add_image(on_image);
}
/* END_CALAYER_FILL */
