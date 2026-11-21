/* FoundationExtras behaviour: -[NSProcessInfo thermalState].
 *
 * The Foundation in this tree has no thermalState. Chromium's thermal monitor reads it
 * inside a notify block and switches over the four defined values; the selector goes to
 * the forwarding path, which hands back the receiver (the forwarding code calls
 * doesNotRecognizeSelector: and returns the target when that comes back), so the
 * "value" is an object pointer, the switch has no case for it and the code reaches its
 * unreachable-default trap. Nominal (0) is the right answer for a machine without a
 * thermal sensor. The method is added at load time to the real class, so
 * respondsToSelector: agrees with it. */
#include "../objc-proto.h"
extern unsigned char class_addMethod(Class cls, SEL name, IMP imp, const char *types);

static long thermal_state(void *self, SEL cmd)
{
    (void)self; (void)cmd;
    return 0; /* NSProcessInfoThermalStateNominal */
}

__attribute__((constructor)) static void foundation_extras_process_info(void)
{
    Class c = objc_getClass("NSProcessInfo");
    if (c)
        class_addMethod(c, sel_registerName("thermalState"), (IMP)thermal_state, "q16@0:8");
}
