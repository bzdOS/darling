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
typedef void *Class;
typedef void *SEL;
typedef void *IMP;
typedef void *id;
struct qc_point { double x, y; };
extern Class objc_getClass(const char *name);
extern Class object_getClass(id obj);
extern SEL sel_registerName(const char *name);
extern unsigned char class_addMethod(Class cls, SEL name, IMP imp, const char *types);
extern id objc_msgSend(id, SEL, ...);
extern void objc_setAssociatedObject(id object, const void *key, id value, unsigned long policy);
struct mach_header;
extern void _dyld_register_func_for_add_image(void (*func)(const struct mach_header *, long));

#define MSG(ret, ...) ((ret (*)(__VA_ARGS__))objc_msgSend)
#define ASSOC_RETAIN 01401

static char key_ax, key_ay, key_mask, key_flip, key_layer, key_sub;

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
