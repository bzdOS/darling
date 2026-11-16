/* AppKitExtras behaviour: -[NSColor colorUsingColorSpace:] (the NSColorSpace form, macOS 10.6+).
 *
 * Chromium converts a colour to sRGB with [color colorUsingColorSpace:[NSColorSpace sRGBColorSpace]]. When the
 * answer is nil it draws the colour into a 1x1 bitmap, reads the pixel back, builds a new colour from it and
 * converts that one the same way: the recursion ends only when the conversion answers a colour. This AppKit has
 * only -colorUsingColorSpaceName: (a NSColor_CGColor/NSColor_catalog colour forwarded the selector to nothing, so the
 * answer was always nil and the recursion ran until the stack overflowed).
 *
 * Added to NSColor (inherited by its subclasses) when absent: the colour in the calibrated RGB name space, which
 * is the RGB model of every colour space this AppKit offers; nil colour space or no conversion gives nil. */
typedef void *Class;
typedef void *SEL;
typedef void *IMP;
typedef void *id;
extern Class objc_getClass(const char *name);
extern SEL sel_registerName(const char *name);
extern unsigned char class_addMethod(Class cls, SEL name, IMP imp, const char *types);
extern id objc_msgSend(id, SEL, ...);
struct mach_header;
extern void _dyld_register_func_for_add_image(void (*func)(const struct mach_header *, long));

#define S(x) sel_registerName(x)

static id use_color_space(id self, SEL cmd, id space)
{
    id name;
    (void)cmd;
    if (!space)
        return 0;
    name = ((id (*)(id, SEL, const char *))objc_msgSend)(objc_getClass("NSString"), S("stringWithUTF8String:"),
                                                          "NSCalibratedRGBColorSpace");
    /* the two argument form: the one argument form of NSColor_CGColor is the abstract NSColor one that raises */
    return ((id (*)(id, SEL, id, id))objc_msgSend)(self, S("colorUsingColorSpaceName:device:"), name, 0);
}


/* ---- -CGColor for every NSColor subclass, and the system colours the window-server-less backend leaves empty ----
 *
 * The Wayland display answers nil for -colorWithName:, so an NSColor_catalog colour ("System" catalogue) was made with
 * a nil inner colour and its -CGColor answered nil; the abstract NSColor -CGColor raised and answered nil as well. Chrome
 * hands that to CGColorGetNumberOfComponents. Here:
 *   - NSColor_catalog -initWithCatalogName:colorName:color: gets a built-in colour (light appearance values) when none
 *     is given, so every selector of the colour (CGColor, components, conversion) has something to answer;
 *   - NSColor_catalog -colorUsingColorSpaceName:device: converts the inner colour and leaves it in place (the stock one
 *     stored the converted colour, unretained, in the ivar);
 *   - -CGColor of NSColor_catalog, NSColor_CGColor and the abstract NSColor never answers NULL: inner colour, converted
 *     RGB colour, or an opaque black as the last resort;
 *   - the +[NSColor xxxColor] selectors of newer systems (controlAccentColor, the label levels, system* colours, ...)
 *     are added as catalogue colours when the class lacks them. */
typedef void *Method;
extern IMP class_replaceMethod(Class cls, SEL name, IMP imp, const char *types);
extern IMP class_getMethodImplementation(Class cls, SEL name);
extern void *class_getInstanceVariable(Class cls, const char *name);
extern long ivar_getOffset(void *ivar);
extern Class object_getClass(id obj);
extern const char *sel_getName(SEL sel);
extern int class_respondsToSelector(Class cls, SEL sel);
extern Class objc_getMetaClass(const char *name);

struct sys_color { const char *name; double r, g, b, a; };
static const struct sys_color sys_colors[] = {
    {"controlAccentColor", 0.0, 0.478, 1.0, 1},
    {"systemBlueColor", 0.0, 0.478, 1.0, 1},
    {"systemRedColor", 1.0, 0.231, 0.188, 1},
    {"systemGreenColor", 0.204, 0.78, 0.349, 1},
    {"systemOrangeColor", 1.0, 0.584, 0.0, 1},
    {"systemYellowColor", 1.0, 0.8, 0.0, 1},
    {"systemPinkColor", 1.0, 0.176, 0.333, 1},
    {"systemPurpleColor", 0.686, 0.322, 0.871, 1},
    {"systemBrownColor", 0.635, 0.518, 0.368, 1},
    {"systemTealColor", 0.188, 0.69, 0.78, 1},
    {"systemIndigoColor", 0.345, 0.337, 0.839, 1},
    {"systemGrayColor", 0.557, 0.557, 0.576, 1},
    {"alternateSelectedControlColor", 0.0, 0.478, 1.0, 1},
    {"alternateSelectedControlTextColor", 1, 1, 1, 1},
    {"selectedContentBackgroundColor", 0.0, 0.478, 1.0, 1},
    {"selectedControlColor", 0.702, 0.835, 1.0, 1},
    {"secondarySelectedControlColor", 0.863, 0.863, 0.863, 1},
    {"unemphasizedSelectedContentBackgroundColor", 0.863, 0.863, 0.863, 1},
    {"selectedTextBackgroundColor", 0.698, 0.843, 1.0, 1},
    {"unemphasizedSelectedTextBackgroundColor", 0.863, 0.863, 0.863, 1},
    {"keyboardFocusIndicatorColor", 0.0, 0.478, 1.0, 0.25},
    {"linkColor", 0.0, 0.4, 0.8, 1},
    {"findHighlightColor", 1.0, 1.0, 0.0, 1},
    {"selectedMenuItemColor", 0.0, 0.478, 1.0, 1},
    {"selectedMenuItemTextColor", 1, 1, 1, 1},
    {"selectedControlTextColor", 0, 0, 0, 1},
    {"selectedTextColor", 0, 0, 0, 1},
    {"unemphasizedSelectedTextColor", 0, 0, 0, 1},
    {"disabledControlTextColor", 0, 0, 0, 0.25},
    {"labelColor", 0, 0, 0, 0.85},
    {"secondaryLabelColor", 0, 0, 0, 0.5},
    {"tertiaryLabelColor", 0, 0, 0, 0.26},
    {"quaternaryLabelColor", 0, 0, 0, 0.1},
    {"placeholderTextColor", 0, 0, 0, 0.25},
    {"separatorColor", 0, 0, 0, 0.1},
    {"gridColor", 0.902, 0.902, 0.902, 1},
    {"shadowColor", 0, 0, 0, 1},
    {"highlightColor", 1, 1, 1, 1},
    {"textColor", 0, 0, 0, 1},
    {"controlTextColor", 0, 0, 0, 0.85},
    {"menuItemTextColor", 0, 0, 0, 1},
    {"headerTextColor", 0, 0, 0, 0.85},
    {"textBackgroundColor", 1, 1, 1, 1},
    {"controlBackgroundColor", 1, 1, 1, 1},
    {"controlColor", 1, 1, 1, 1},
    {"controlHighlightColor", 0.863, 0.863, 0.863, 1},
    {"controlLightHighlightColor", 1, 1, 1, 1},
    {"controlShadowColor", 0.667, 0.667, 0.667, 1},
    {"controlDarkShadowColor", 0.404, 0.404, 0.404, 1},
    {"scrollBarColor", 0.925, 0.925, 0.925, 1},
    {"knobColor", 0.667, 0.667, 0.667, 1},
    {"selectedKnobColor", 0.5, 0.5, 0.5, 1},
    {"headerColor", 0.925, 0.925, 0.925, 1},
    {"windowBackgroundColor", 0.925, 0.925, 0.925, 1},
    {"windowFrameColor", 0.667, 0.667, 0.667, 1},
    {"underPageBackgroundColor", 0.588, 0.588, 0.588, 1},
    {"menuBackgroundColor", 0.925, 0.925, 0.925, 1},
    {"mainMenuBarColor", 0.925, 0.925, 0.925, 1},
    {"alternatingContentBackgroundColor", 0.957, 0.957, 0.957, 1},
    {0, 0, 0, 0, 0}};

static id msg_color4(const char *cls, const char *sel, double r, double g, double b, double a)
{
    return ((id (*)(id, SEL, double, double, double, double))objc_msgSend)(objc_getClass(cls), S(sel), r, g, b, a);
}

/* the built-in colour for a catalogue name: the table, else white-ish for backgrounds, black for text */
static id builtin_color(const char *name)
{
    const struct sys_color *t;
    int n, i;
    if (!name)
        name = "";
    for (t = sys_colors; t->name; t++) {
        const char *a = t->name, *b = name;
        while (*a && *a == *b) { a++; b++; }
        if (!*a && !*b)
            return msg_color4("NSColor", "colorWithCalibratedRed:green:blue:alpha:", t->r, t->g, t->b, t->a);
    }
    for (n = 0; name[n]; n++) ;
    if (n >= 9 && name[n - 9] == 'T' && name[n - 8] == 'e' && name[n - 7] == 'x' && name[n - 6] == 't')
        return msg_color4("NSColor", "colorWithCalibratedRed:green:blue:alpha:", 0, 0, 0, 1);
    (void)i;
    return msg_color4("NSColor", "colorWithCalibratedRed:green:blue:alpha:", 0.925, 0.925, 0.925, 1);
}

static IMP orig_catalog_init, orig_cgcolor_cgcolor;
static void *fallback_ref;

static void *fallback_cgcolor(void)
{
    if (!fallback_ref) {
        id c = msg_color4("NSColor", "colorWithCalibratedRed:green:blue:alpha:", 0, 0, 0, 1);
        id r;
        if (!c)
            return 0;
        r = ((id (*)(id, SEL))objc_msgSend)(c, S("retain"));
        fallback_ref = ((void *(*)(id, SEL))objc_msgSend)(r, S("CGColor"));
    }
    return fallback_ref;
}

static id ivar_object(id self, const char *name)
{
    void *iv = class_getInstanceVariable(object_getClass(self), name);
    return iv ? *(id *)((char *)self + ivar_getOffset(iv)) : 0;
}

static id catalog_init(id self, SEL cmd, id catalog, id name, id color)
{
    if (!color) {
        const char *n = name ? ((const char *(*)(id, SEL))objc_msgSend)(name, S("UTF8String")) : 0;
        color = builtin_color(n);
    }
    return ((id (*)(id, SEL, id, id, id))orig_catalog_init)(self, cmd, catalog, name, color);
}

static id catalog_use_space(id self, SEL cmd, id space, id device)
{
    id inner = ivar_object(self, "_color");
    (void)cmd;
    if (space && ((char (*)(id, SEL, id))objc_msgSend)(
                     space, S("isEqualToString:"),
                     ((id (*)(id, SEL, const char *))objc_msgSend)(objc_getClass("NSString"),
                                                                    S("stringWithUTF8String:"), "NSNamedColorSpace")))
        return self;
    if (!inner)
        return 0;
    return ((id (*)(id, SEL, id, id))objc_msgSend)(inner, S("colorUsingColorSpaceName:device:"), space, device);
}

static void *catalog_cgcolor(id self, SEL cmd)
{
    id inner = ivar_object(self, "_color");
    void *r = 0;
    (void)cmd;
    if (inner)
        r = ((void *(*)(id, SEL))objc_msgSend)(inner, S("CGColor"));
    return r ? r : fallback_cgcolor();
}

static void *wrapper_cgcolor(id self, SEL cmd)
{
    void *r = ((void *(*)(id, SEL))orig_cgcolor_cgcolor)(self, cmd);
    return r ? r : fallback_cgcolor();
}

/* the abstract -[NSColor CGColor]: a subclass that converts to RGB is asked; otherwise the last resort */
static void *abstract_cgcolor(id self, SEL cmd)
{
    Class base = objc_getClass("NSColor");
    SEL conv = S("colorUsingColorSpaceName:device:");
    (void)cmd;
    if (class_getMethodImplementation(object_getClass(self), conv) != class_getMethodImplementation(base, conv)) {
        id name = ((id (*)(id, SEL, const char *))objc_msgSend)(objc_getClass("NSString"), S("stringWithUTF8String:"),
                                                                "NSCalibratedRGBColorSpace");
        id rgb = ((id (*)(id, SEL, id, id))objc_msgSend)(self, conv, name, 0);
        if (rgb && rgb != self) {
            void *r = ((void *(*)(id, SEL))objc_msgSend)(rgb, S("CGColor"));
            if (r)
                return r;
        }
    }
    return fallback_cgcolor();
}

static id catalogue_class_color(id self, SEL cmd)
{
    id sys = ((id (*)(id, SEL, const char *))objc_msgSend)(objc_getClass("NSString"), S("stringWithUTF8String:"), "System");
    id name = ((id (*)(id, SEL, const char *))objc_msgSend)(objc_getClass("NSString"), S("stringWithUTF8String:"),
                                                             sel_getName(cmd));
    return ((id (*)(id, SEL, id, id))objc_msgSend)(self, S("colorWithCatalogName:colorName:"), sys, name);
}

static int cgcolor_done;

static void try_add_cgcolor(void)
{
    Class nc = objc_getClass("NSColor"), cat = objc_getClass("NSColor_catalog"), wrap = objc_getClass("NSColor_CGColor");
    Class meta;
    const struct sys_color *t;
    if (cgcolor_done || !nc || !cat || !wrap)
        return;
    meta = objc_getMetaClass("NSColor");
    orig_catalog_init = class_replaceMethod(cat, S("initWithCatalogName:colorName:color:"), (IMP)catalog_init, "@40@0:8@16@24@32");
    class_replaceMethod(cat, S("colorUsingColorSpaceName:device:"), (IMP)catalog_use_space, "@32@0:8@16@24");
    class_replaceMethod(cat, S("CGColor"), (IMP)catalog_cgcolor, "^{CGColor=}16@0:8");
    orig_cgcolor_cgcolor = class_replaceMethod(wrap, S("CGColor"), (IMP)wrapper_cgcolor, "^{CGColor=}16@0:8");
    class_replaceMethod(nc, S("CGColor"), (IMP)abstract_cgcolor, "^{CGColor=}16@0:8");
    for (t = sys_colors; t->name; t++)
        if (!class_respondsToSelector(meta, S(t->name)))
            class_addMethod(meta, S(t->name), (IMP)catalogue_class_color, "@16@0:8");
    cgcolor_done = 1;
}

static int done;

static void try_add(void)
{
    Class c;
    if (done) {
        try_add_cgcolor();
        return;
    }
    c = objc_getClass("NSColor");
    if (!c)
        return;
    class_addMethod(c, S("colorUsingColorSpace:"), (IMP)use_color_space, "@24@0:8@16");
    done = 1;
    try_add_cgcolor();
}

static void on_image(const struct mach_header *mh, long slide) { (void)mh; (void)slide; try_add(); }

__attribute__((constructor)) static void appkit_extras_color(void)
{
    try_add();
    if (!done || !cgcolor_done)
        _dyld_register_func_for_add_image(on_image);
}
