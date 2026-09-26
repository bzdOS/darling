/* Declarations missing from the flat-SDK headers, force-included into every
 * CoreFoundation translation unit (see build-freebsd/build-cf-only.sh).
 * Recreated after the original was lost; extend as new gaps appear. */
#ifndef CF_DERITEM_FORCE_H
#define CF_DERITEM_FORCE_H

#include <stdint.h>
#include <stdbool.h>

/* mach-o/arch.h uses enum NXByteOrder but the flat SDK's architecture.h never
 * defines it. */
enum NXByteOrder {
    NX_UnknownByteOrder,
    NX_LittleEndian,
    NX_BigEndian
};

/* objc_isAuto is declared by <objc/objc-internal.h>, which CFInternal.h
 * includes when DARLING is defined; the definition lives in cf-gc-stub.c. */

/* Runtime-gated, unbuffered diagnostic print. Enabled by setting CFDBG=1 in
 * the guest environment (no rebuild needed); output goes through write(2) so
 * it survives crashes. Defined in cf-forwarding-stub.c. */
void _CFDBG(const char *fmt, ...);

#endif
