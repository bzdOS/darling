/*
 * unwind-probe.m - guest probe for the exception/forwarding path.
 *
 * purpose:     Show what an Objective-C message to a selector the class does
 *              not implement does in this guest (return value, exception), and
 *              that a thrown ObjC exception and a C++ exception are unwound
 *              to their handlers.
 * input:       None.
 * output:      one "uw-probe: ..." line per case on stdout.
 * sideEffects: None.
 */
#import <Foundation/Foundation.h>
#include <stdio.h>
#include <stdint.h>

@interface NSProcessInfo (UwProbe)
- (NSInteger)thermalState;
- (NSInteger)uwNoSuchSelector;
@end

extern int uw_cxx_throw_catch(void);
#define cxx_throw_catch uw_cxx_throw_catch

int main(void) {
    NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];
    NSProcessInfo *pi = [NSProcessInfo processInfo];
    printf("uw-probe: class=%s obj=%p\n", object_getClassName(pi), (void *)pi);
    printf("uw-probe: responds thermalState=%d uwNoSuchSelector=%d\n",
           (int)[pi respondsToSelector:@selector(thermalState)],
           (int)[pi respondsToSelector:@selector(uwNoSuchSelector)]);
    fflush(stdout);

    @try {
        @throw [NSException exceptionWithName:@"UwProbe" reason:@"r" userInfo:nil];
    } @catch (NSException *e) {
        printf("uw-probe: objc throw caught: %s\n", [[e name] UTF8String]);
    }
    fflush(stdout);
    printf("uw-probe: cxx throw caught: %d\n", cxx_throw_catch());
    fflush(stdout);

    @try {
        NSInteger r = [pi uwNoSuchSelector];
        printf("uw-probe: unknown selector returned 0x%llx (receiver %p)\n",
               (unsigned long long)r, (void *)pi);
    } @catch (NSException *e) {
        printf("uw-probe: unknown selector raised: %s\n", [[e name] UTF8String]);
    }
    fflush(stdout);
    @try {
        NSInteger t = [pi thermalState];
        printf("uw-probe: thermalState returned 0x%llx\n", (unsigned long long)t);
    } @catch (NSException *e) {
        printf("uw-probe: thermalState raised: %s\n", [[e name] UTF8String]);
    }
    fflush(stdout);
    [pool release];
    return 0;
}
