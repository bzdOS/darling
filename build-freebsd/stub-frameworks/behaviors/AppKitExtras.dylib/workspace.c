/* AppKitExtras behaviour: -[NSWorkspace voiceOverEnabled].
 *
 * Chromium observes the key path "voiceOverEnabled" on the shared workspace to notice
 * VoiceOver. This AppKit's NSWorkspace has no such property, so key-value coding
 * raises "This class is not key value coding-compliant for the key voiceOverEnabled"
 * and the uncaught exception ends the process. NO (VoiceOver is not running) is the
 * right answer. The method is added to the real class once it is loaded: AppKit may
 * be mapped after this image, so the add-image hook retries until the class exists. */
#include "../objc-proto.h"
extern unsigned char class_addMethod(Class cls, SEL name, IMP imp, const char *types);
struct mach_header;
extern void _dyld_register_func_for_add_image(void (*func)(const struct mach_header *, long));

static signed char voice_over_enabled(void *self, SEL cmd) { (void)self; (void)cmd; return 0; }
static int done;

static void try_add(void)
{
    Class c;
    if (done)
        return;
    c = objc_getClass("NSWorkspace");
    if (!c)
        return;
    class_addMethod(c, sel_registerName("voiceOverEnabled"), (IMP)voice_over_enabled, "c16@0:8");
    class_addMethod(c, sel_registerName("isVoiceOverEnabled"), (IMP)voice_over_enabled, "c16@0:8");
    done = 1;
}

static void on_image(const struct mach_header *mh, long slide) { (void)mh; (void)slide; try_add(); }

__attribute__((constructor)) static void appkit_extras_workspace(void)
{
    try_add();
    if (!done)
        _dyld_register_func_for_add_image(on_image);
}
