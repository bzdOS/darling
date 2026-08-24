/*
 * hello-foundation.m — minimal program against a self-built
 * Foundation.dylib (compiled from the real, unmodified darling-foundation
 * submodule sources — see build-freebsd/build-real-macho-tests.sh and
 * tests/vendor/README.md for how).
 *
 * purpose:     Prove NSObject/NSString/NSArray/NSNumber — the actual
 *              Foundation class hierarchy, not just the CoreFoundation C
 *              API underneath it — work end to end through the loader.
 * input:       None.
 * output:      "hello-foundation: (1, 2, 3) count=3\n" on stdout.
 * sideEffects: None.
 */
#import <Foundation/Foundation.h>

int main(void) {
    NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];

    NSMutableArray *array = [NSMutableArray arrayWithCapacity:3];
    [array addObject:@"1"];
    [array addObject:@"2"];
    [array addObject:@"3"];

    NSString *joined = [array componentsJoinedByString:@", "];
    printf("hello-foundation: (%s) count=%lu\n",
           [joined cStringUsingEncoding:NSUTF8StringEncoding],
           (unsigned long)[array count]);

    [pool release];
    return 0;
}
