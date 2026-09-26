/* objc_isAuto: GC query from libobjc. darling has no GC; always NO.
 * Declaration comes from <objc/objc-internal.h> (BOOL objc_isAuto(id)). */
#include <objc/objc.h>

BOOL objc_isAuto(id object) {
    (void)object;
    return NO;
}
