/* AppKitExtras behaviour: the NSAppearance API of macOS 10.14+ that Chromium uses.
 *
 * Chromium asks [NSApp effectiveAppearance] (and the view/window variants) for the dark-mode state,
 * builds @[NSAppearanceNameAqua, NSAppearanceNameDarkAqua] and sends bestMatchFromAppearancesWithNames:
 * to the answer. This AppKit has NSAppearance only as a stub (+appearanceNamed: returns an
 * uninitialised instance and prints "STUB") and no effectiveAppearance anywhere; the missing selector
 * went to the forward path and came back as a heap pointer that was then used as a receiver
 * ("Attempt to use unknown class", abort).
 *
 * Added here, to the real classes once AppKit is mapped (add-image retry as in workspace.c):
 *   NSAppearance            +appearanceNamed: (replaced: named, cached instances), -name,
 *                           +currentAppearance, +currentDrawingAppearance, +setCurrentAppearance:,
 *                           -bestMatchFromAppearancesWithNames:, -performAsCurrentDrawingAppearance:,
 *                           -allowsVibrancy
 *   NSApplication/NSView/NSWindow/NSMenu  -effectiveAppearance (the light "Aqua" appearance: there is no
 *                           system dark mode here), NSApplication and NSWindow also -appearance and
 *                           -setAppearance: (NSView has them)
 *   NSWorkspace             accessibilityDisplayShould{IncreaseContrast,InvertColors,ReduceMotion,
 *                           ReduceTransparency}: NO
 * Existing methods are never replaced except +[NSAppearance appearanceNamed:]. */
#include "../objc-proto.h"
extern Class object_getClass(id o);
extern unsigned char class_addMethod(Class cls, SEL name, IMP imp, const char *types);
extern IMP class_replaceMethod(Class cls, SEL name, IMP imp, const char *types);
extern void objc_setAssociatedObject(id obj, const void *key, id value, unsigned long policy);
extern id objc_getAssociatedObject(id obj, const void *key);
struct mach_header;
extern void _dyld_register_func_for_add_image(void (*func)(const struct mach_header *, long));

typedef id (*msg_id)(id, SEL, ...);
typedef unsigned long (*msg_ul)(id, SEL, ...);
typedef signed char (*msg_bool)(id, SEL, ...);
#define SEND_ID ((msg_id)objc_msgSend)
#define SEND_UL ((msg_ul)objc_msgSend)
#define SEND_BOOL ((msg_bool)objc_msgSend)

static char name_key;

static id nsstring(const char *s)
{
    return SEND_ID(objc_getClass("NSString"), S("stringWithUTF8String:"), s);
}

/* NSRange is returned in RAX:RDX; read it through a struct-returning cast */
struct range { unsigned long loc, len; };
static int contains(id str, const char *needle)
{
    struct range r = ((struct range (*)(id, SEL, id))objc_msgSend)(str, S("rangeOfString:"), nsstring(needle));
    return r.len != 0 && r.loc != (unsigned long)-1 && r.loc != 0x7fffffffffffffffUL;
}

static id appearances[16];
static id appearance_named(void *cls, SEL cmd, id name)
{
    int i;
    (void)cmd;
    if (!name)
        return 0;
    for (i = 0; i < 16 && appearances[i]; i++)
        if (SEND_BOOL(name, S("isEqualToString:"), objc_getAssociatedObject(appearances[i], &name_key)))
            return appearances[i];
    {
        id a = SEND_ID(SEND_ID(cls, S("alloc")), S("init"));
        if (!a)
            return 0;
        objc_setAssociatedObject(a, &name_key, SEND_ID(name, S("copy")), 1 /* retain */);
        if (i < 16)
            appearances[i] = a;
        return a;
    }
}

static id aqua(void)
{
    return appearance_named(objc_getClass("NSAppearance"), 0, nsstring("NSAppearanceNameAqua"));
}

static id appearance_name(id self, SEL cmd) { (void)cmd; return objc_getAssociatedObject(self, &name_key); }
static id current_appearance(void *cls, SEL cmd) { (void)cls; (void)cmd; return aqua(); }
static void set_current_appearance(void *cls, SEL cmd, id a) { (void)cls; (void)cmd; (void)a; }
static signed char no_bool(id self, SEL cmd) { (void)self; (void)cmd; return 0; }
static id effective_appearance(id self, SEL cmd) { (void)self; (void)cmd; return aqua(); }
static id nil_appearance(id self, SEL cmd) { (void)self; (void)cmd; return 0; }
static void nop_set(id self, SEL cmd, id a) { (void)self; (void)cmd; (void)a; }

struct block { void *isa; int flags, reserved; void (*invoke)(struct block *); };
static void perform_as_current(id self, SEL cmd, struct block *b)
{
    (void)self; (void)cmd;
    if (b && b->invoke)
        b->invoke(b);
}

static id best_match(id self, SEL cmd, id names)
{
    unsigned long i, n;
    id mine = objc_getAssociatedObject(self, &name_key);
    id aqua_hit = 0, dark_hit = 0;
    (void)cmd;
    if (!names)
        return 0;
    n = SEND_UL(names, S("count"));
    for (i = 0; i < n; i++) {
        id e = SEND_ID(names, S("objectAtIndex:"), i);
        if (mine && SEND_BOOL(e, S("isEqualToString:"), mine))
            return e;
        if (contains(e, "DarkAqua") || contains(e, "VibrantDark")) {
            if (!dark_hit)
                dark_hit = e;
        } else if (contains(e, "Aqua") || contains(e, "VibrantLight") || contains(e, "System")) {
            if (!aqua_hit)
                aqua_hit = e;
        }
    }
    if (mine && (contains(mine, "Dark")))
        return dark_hit ? dark_hit : aqua_hit;
    return aqua_hit ? aqua_hit : dark_hit;
}

static int done;

static void add(const char *cls, const char *sel, IMP imp, const char *types)
{
    Class c = objc_getClass(cls);
    if (c)
        class_addMethod(c, S(sel), imp, types);
}

static void try_add(void)
{
    Class a;
    if (done)
        return;
    a = objc_getClass("NSAppearance");
    if (!a || !objc_getClass("NSApplication") || !objc_getClass("NSWorkspace"))
        return;
    class_replaceMethod(object_getClass(a), S("appearanceNamed:"), (IMP)appearance_named, "@24@0:8@16");
    class_addMethod(object_getClass(a), S("currentAppearance"), (IMP)current_appearance, "@16@0:8");
    class_addMethod(object_getClass(a), S("currentDrawingAppearance"), (IMP)current_appearance, "@16@0:8");
    class_addMethod(object_getClass(a), S("setCurrentAppearance:"), (IMP)set_current_appearance, "v24@0:8@16");
    add("NSAppearance", "name", (IMP)appearance_name, "@16@0:8");
    add("NSAppearance", "bestMatchFromAppearancesWithNames:", (IMP)best_match, "@24@0:8@16");
    add("NSAppearance", "performAsCurrentDrawingAppearance:", (IMP)perform_as_current, "v24@0:8@?16");
    add("NSAppearance", "allowsVibrancy", (IMP)no_bool, "c16@0:8");
    add("NSAppearance", "effectiveAppearance", (IMP)effective_appearance, "@16@0:8");
    add("NSApplication", "effectiveAppearance", (IMP)effective_appearance, "@16@0:8");
    add("NSApplication", "appearance", (IMP)nil_appearance, "@16@0:8");
    add("NSApplication", "setAppearance:", (IMP)nop_set, "v24@0:8@16");
    add("NSView", "effectiveAppearance", (IMP)effective_appearance, "@16@0:8");
    add("NSWindow", "effectiveAppearance", (IMP)effective_appearance, "@16@0:8");
    add("NSWindow", "appearance", (IMP)nil_appearance, "@16@0:8");
    add("NSWindow", "setAppearance:", (IMP)nop_set, "v24@0:8@16");
    add("NSMenu", "effectiveAppearance", (IMP)effective_appearance, "@16@0:8");
    add("NSWorkspace", "accessibilityDisplayShouldIncreaseContrast", (IMP)no_bool, "c16@0:8");
    add("NSWorkspace", "accessibilityDisplayShouldInvertColors", (IMP)no_bool, "c16@0:8");
    add("NSWorkspace", "accessibilityDisplayShouldReduceMotion", (IMP)no_bool, "c16@0:8");
    add("NSWorkspace", "accessibilityDisplayShouldReduceTransparency", (IMP)no_bool, "c16@0:8");
    done = 1;
}

static void on_image(const struct mach_header *mh, long slide) { (void)mh; (void)slide; try_add(); }

__attribute__((constructor)) static void appkit_extras_appearance(void)
{
    try_add();
    if (!done)
        _dyld_register_func_for_add_image(on_image);
}
