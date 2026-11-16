/* nscgcolor-probe: every NSColor the AppKit hands out must answer -CGColor with a real CGColorRef.
 * Chrome's colour conversion does CGColorGetNumberOfComponents([color CGColor]); a nil colour ref crashed in the
 * graphics library. For each statically listed +[NSColor xxxColor] (the system catalogue and the plain ones) prints
 *   NSCGCOLOR name=<sel> class=<class> cg=<ok|nil|missing|crash> n=<components>
 * then the null-reference line and the summary:
 *   NSCGCOLOR-NULL survived=<yes|no>
 *   NSCGCOLOR-SUMMARY colors=<n> bad=<n> first=<name> null=<yes|no> */
#include <stdio.h>
#include <string.h>
#include <setjmp.h>
#include <signal.h>

extern void *objc_getClass(const char *);
extern void *sel_registerName(const char *);
extern void *objc_msgSend(void *, void *, ...);
extern int class_respondsToSelector(void *, void *);
extern void *object_getClass(void *);
extern const char *class_getName(void *);
extern long CGColorGetNumberOfComponents(void *);
extern void *objc_allocateClassPair(void *, const char *, unsigned long);
extern void objc_registerClassPair(void *);
extern unsigned char class_addMethod(void *, void *, void *, const char *);
extern void *class_getClassMethod(void *, void *);
extern void *method_setImplementation(void *, void *);
#define S(x) sel_registerName(x)

/* No window server in this process: the Wayland display answers nil for colorWithName: ("AppKit default applies").
 * A display of that behaviour stands in for it, so the catalogue colours are made the way they are inside Chrome. */
static void *the_display;
static void *no_name(void *self, void *cmd, void *name) { (void)self; (void)cmd; (void)name; return 0; }
static void *cur_display(void *self, void *cmd) { (void)self; (void)cmd; return the_display; }
static void install_display(void)
{
    void *nsd = objc_getClass("NSDisplay"), *sub;
    if (!nsd) return;
    sub = objc_allocateClassPair(nsd, "ProbeDisplay", 0);
    class_addMethod(sub, S("colorWithName:"), (void *)no_name, "@24@0:8@16");
    objc_registerClassPair(sub);
    the_display = objc_msgSend(objc_msgSend(sub, S("alloc")), S("init"));
    method_setImplementation(class_getClassMethod(nsd, S("currentDisplay")), (void *)cur_display);
}

static const char *names[] = {
    "alternateSelectedControlColor", "alternateSelectedControlTextColor", "keyboardFocusIndicatorColor",
    "highlightColor", "shadowColor", "gridColor", "controlColor", "selectedControlColor",
    "secondarySelectedControlColor", "controlTextColor", "selectedControlTextColor", "disabledControlTextColor",
    "controlBackgroundColor", "controlDarkShadowColor", "controlHighlightColor", "controlLightHighlightColor",
    "controlShadowColor", "textColor", "textBackgroundColor", "selectedTextColor", "selectedTextBackgroundColor",
    "headerColor", "headerTextColor", "scrollBarColor", "knobColor", "selectedKnobColor", "windowBackgroundColor",
    "windowFrameColor", "selectedMenuItemColor", "selectedMenuItemTextColor", "menuBackgroundColor",
    "mainMenuBarColor", "menuItemTextColor", "labelColor", "unemphasizedSelectedTextColor",
    "unemphasizedSelectedTextBackgroundColor", "linkColor", "selectedContentBackgroundColor",
    "unemphasizedSelectedContentBackgroundColor", "alternatingContentBackgroundColor", "controlAccentColor",
    "clearColor", "blackColor", "blueColor", "brownColor", "cyanColor", "darkGrayColor", "grayColor",
    "greenColor", "lightGrayColor", "magentaColor", "orangeColor", "purpleColor", "redColor", "whiteColor",
    "yellowColor", 0};

static sigjmp_buf jb;
static void on_sig(int s) { (void)s; siglongjmp(jb, 1); }

int main(void)
{
    void *nscolor = objc_getClass("NSColor");
    int total = 0, bad = 0, i;
    const char *first = "none";
    int null_ok = 0;
    struct sigaction sa;
    memset(&sa, 0, sizeof sa);
    sa.sa_handler = on_sig;
    sigaction(SIGSEGV, &sa, 0);
    sigaction(SIGBUS, &sa, 0);
    setvbuf(stdout, 0, _IOLBF, 0);
    install_display();
    if (!nscolor) { printf("NSCGCOLOR-SUMMARY colors=0 bad=0 first=none null=no\nNSCGCOLOR-PROBE DONE\n"); return 0; }
    for (i = 0; names[i]; i++) {
        const char *cg = "missing", *cls = "nil";
        long n = -1;
        total++;
        if (sigsetjmp(jb, 1) == 0) {
            if (class_respondsToSelector(object_getClass(nscolor), S(names[i]))) {
                void *c = objc_msgSend(nscolor, S(names[i]));
                if (c) {
                    void *r;
                    cls = class_getName(object_getClass(c));
                    r = objc_msgSend(c, S("CGColor"));
                    cg = r ? "ok" : "nil";
                    if (r) n = CGColorGetNumberOfComponents(r);
                }
            }
        } else
            cg = "crash";
        printf("NSCGCOLOR name=%s class=%s cg=%s n=%ld\n", names[i], cls, cg, n);
        if (strcmp(cg, "ok") || n <= 0) { if (!bad) first = names[i]; bad++; }
    }
    if (sigsetjmp(jb, 1) == 0) {
        CGColorGetNumberOfComponents(0);
        null_ok = 1;
    }
    printf("NSCGCOLOR-NULL survived=%s\n", null_ok ? "yes" : "no");
    printf("NSCGCOLOR-SUMMARY colors=%d bad=%d first=%s null=%s\n", total, bad, first, null_ok ? "yes" : "no");
    printf("NSCGCOLOR-PROBE DONE\n");
    fflush(stdout);
    return 0;
}
