/* Stubs for symbols normally provided by the skipped NSInvoke-x86.S /
 * CFForwardingPrep.S asm, plus a runtime helper absent from this tree.
 * NSInvocation forwarding is not needed for bundle loading, so these are
 * inert placeholders that satisfy the linker. */

/* Forwarding trampoline placeholders. */
void _CF_forwarding_prep_0(void) {}
void _CF_forwarding_prep_1(void) {}
void _CF_forwarding_prep_b(void) {}

/* ___invoke__ is referenced by NSInvocation with exactly three underscores
 * (asm-style symbol), so force the name with an asm label. */
void _invoke__(void) __asm("___invoke__");
void _invoke__(void) {}

/* ___CFConstantStringClassReference is provided as a real alias (same address
 * as the NSCFConstantString class) via cf-conststr-alias.s, so it is not
 * defined here. */

/* _CFIsCFObject: toll-free bridging probe. CFInternal.h defines an inline
 * version for CF's own use; this out-of-line definition satisfies Foundation,
 * which imports the symbol from CoreFoundation. */
unsigned char _CFIsCFObject(const void *cf) {
    (void)cf;
    return 1;
}

/* ___CFInitialize needs no help: CFRuntime.c declares it
 * __attribute__((constructor)), and dyld runs image constructors. */

/* Runtime-gated diagnostic print: enabled when CFDBG is set in the guest env,
 * unbuffered via write(2) so output survives a crash, single-line friendly.
 * Declared in cf-deritem-force.h, which is force-included into every CF TU. */
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <unistd.h>

void _CFDBG(const char *fmt, ...) {
    static int on = -1;
    if (on < 0) on = (getenv("CFDBG") != NULL);
    if (!on) return;
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    if (n > 0) {
        if (n >= (int)sizeof(buf)) n = sizeof(buf) - 1;
        write(2, buf, n);
    }
}
