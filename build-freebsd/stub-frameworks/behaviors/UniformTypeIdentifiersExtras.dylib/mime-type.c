/* UniformTypeIdentifiersExtras behaviour: +[UTType typeWithMIMEType:].
 *
 * The UTType of this image is a root class with only the allocation entry points. Chromium looks types up by MIME type
 * (net/base/mac); an unknown type is nil, the documented answer of +typeWithMIMEType:, so a type system with no registry
 * answers nil for every MIME type instead of sending the message to the forward handler (OBJC-FWD-MISS). The method is
 * added to the metaclass at load time so respondsToSelector: agrees. */
#include "../objc-proto.h"
extern Class object_getClass(id obj);
extern unsigned char class_addMethod(Class cls, SEL name, IMP imp, const char *types);

static id type_with_mime_type(id self, SEL cmd, id mime)
{
    (void)self; (void)cmd; (void)mime;
    return 0;
}

__attribute__((constructor)) static void uti_extras_mime(void)
{
    Class c = objc_getClass("UTType");
    if (c)
        class_addMethod(object_getClass((id)c), S("typeWithMIMEType:"), (IMP)type_with_mime_type, "@24@0:8@16");
}
