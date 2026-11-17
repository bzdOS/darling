/* AppKitExtras behaviour: NSAlert -runModal probe.
 *
 * Chromium reached -[NSAlert runModal] at start-up and the main thread stayed in it with no window. This logs, once per
 * call, the message text, the informative text, the button titles and the caller (image + offset) as
 *   ALERT-PROBE runModal message="..." informative="..." buttons=[...] caller=<image>+0x<off>
 * and then calls the original implementation. Replaced once AppKit is mapped (add-image retry as in screen.c). */
typedef void *Class;
typedef void *SEL;
typedef void *IMP;
typedef void *id;
typedef struct { const char *dli_fname; void *dli_fbase; const char *dli_sname; void *dli_saddr; } Dl_info_x;
extern Class objc_getClass(const char *name);
extern SEL sel_registerName(const char *name);
extern IMP class_replaceMethod(Class cls, SEL name, IMP imp, const char *types);
extern id objc_msgSend(id, SEL, ...);
extern int dladdr(const void *, Dl_info_x *);
extern int snprintf(char *, unsigned long, const char *, ...);
extern long write(int, const void *, unsigned long);
extern char *strstr(const char *a, const char *b);
struct mach_header;
extern void _dyld_register_func_for_add_image(void (*func)(const struct mach_header *, long));

#define S(x) sel_registerName(x)
static IMP orig_run_modal;

static const char *utf8(id s)
{
    const char *r;
    if (!s)
        return "(nil)";
    r = ((const char *(*)(id, SEL))objc_msgSend)(s, S("UTF8String"));
    return r ? r : "(null)";
}

static long alert_run_modal(id self, SEL cmd)
{
    char line[2048];
    char btn[512];
    void *ra = __builtin_return_address(0);
    Dl_info_x di;
    id buttons = ((id (*)(id, SEL))objc_msgSend)(self, S("buttons"));
    long n = buttons ? ((long (*)(id, SEL))objc_msgSend)(buttons, S("count")) : 0, i, pos = 0;
    const char *img = "?";
    long off = 0;
    btn[0] = 0;
    for (i = 0; i < n && pos < (long)sizeof btn - 64; i++) {
        id b = ((id (*)(id, SEL, long))objc_msgSend)(buttons, S("objectAtIndex:"), i);
        id t = b ? ((id (*)(id, SEL))objc_msgSend)(b, S("title")) : 0;
        pos += snprintf(btn + pos, sizeof btn - pos, "%s\"%s\"", i ? "," : "", utf8(t));
    }
    if (dladdr(ra, &di) && di.dli_fname) {
        img = di.dli_fname;
        off = (long)((char *)ra - (char *)di.dli_fbase);
    }
    i = snprintf(line, sizeof line, "ALERT-PROBE runModal message=\"%s\" informative=\"%s\" buttons=[%s] caller=%s+0x%lx\n",
                 utf8(((id (*)(id, SEL))objc_msgSend)(self, S("messageText"))),
                 utf8(((id (*)(id, SEL))objc_msgSend)(self, S("informativeText"))), btn, img, off);
    if (i > 0)
        write(2, line, (unsigned long)(i < (long)sizeof line ? i : (long)sizeof line - 1));
    return ((long (*)(id, SEL))orig_run_modal)(self, cmd);
}

static int done;

static void try_add(void)
{
    Class c;
    if (done)
        return;
    c = objc_getClass("NSAlert");
    if (!c)
        return;
    orig_run_modal = class_replaceMethod(c, S("runModal"), (IMP)alert_run_modal, "q16@0:8");
    done = orig_run_modal != 0;
}

/* Only AppKit itself can make the class appear: do nothing for the other images (the callback runs for every image a
 * thread loads). */
static void on_image(const struct mach_header *mh, long slide)
{
    Dl_info_x di;
    (void)slide;
    static int armed; /* set once the image itself was seen: the class may be registered a moment later, retry then */
    if (done)
        return;
    if (!armed) {
        if (!dladdr(mh, &di) || !di.dli_fname || !strstr(di.dli_fname, "AppKit.framework/Versions/C/AppKit"))
            return;
        armed = 1;
    }
    try_add();
}

__attribute__((constructor)) static void appkit_extras_alert(void)
{
    try_add();
    if (!done)
        _dyld_register_func_for_add_image(on_image);
}
