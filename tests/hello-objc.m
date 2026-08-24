/*
 * hello-objc.m — minimal Objective-C program with no Foundation dependency.
 *
 * purpose:     Prove the loader can run code that touches the Objective-C
 *              runtime (libobjc.A.dylib): class definition, ivars, a
 *              method, and message dispatch (objc_msgSend) through the
 *              REAL Apple runtime — not a stand-in. No NSObject/Foundation
 *              is used, since that pulls in CoreFoundation and a much
 *              larger, not-yet-attempted dependency surface.
 * input:       None.
 * output:      "hello-objc 42\n" on stdout.
 * sideEffects: None.
 *
 * Build: see build-freebsd/build-real-macho-tests.sh.
 */
#include <objc/objc.h>
#include <objc/runtime.h>

extern int printf(const char *, ...);

/* A minimal root class — no NSObject. Needs its own +alloc/-init and an
 * empty root metaclass isa chain, which the compiler + runtime handle via
 * OBJC_ROOT_CLASS. */
__attribute__((objc_root_class))
@interface Thing
{
    Class isa;
    int value;
}
- (void)setValue:(int)v;
- (int)doubled;
@end

@implementation Thing
- (void)setValue:(int)v {
    value = v;
}
- (int)doubled {
    return value * 2;
}
@end

int main(void) {
    Thing *t = class_createInstance(objc_getClass("Thing"), 0);
    ((void (*)(id, SEL, int))objc_msgSend)(t, @selector(setValue:), 21);
    int result = ((int (*)(id, SEL))objc_msgSend)(t, @selector(doubled));
    printf("hello-objc %d\n", result);
    return 0;
}
