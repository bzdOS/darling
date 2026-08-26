/*
 * IOGraphicsTypes.h -- shim replacement.
 *
 * See src/freebsd-shims/missing-headers/README.md: `IOKit/graphics/
 * IOGraphicsTypes.h` is a dangling symlink into the uninitialized
 * `IOGraphics` submodule everywhere else in this repo.
 * CGDirectDisplay.m #imports this path directly (CGDirectDisplay.m:25),
 * and uses exactly five identifiers from it (verified: `grep -n
 * 'kDisplay' src/external/cocotron/CoreGraphics/CGDirectDisplay.m` --
 * see lines 381, 389, 420, 421, 423). This header provides only those
 * five, nothing else from the real (much larger) Apple/Darling
 * IOGraphicsTypes.h.
 *
 * RISK -- NOT VERIFIED: the string keys and sentinel values below are
 * reconstructed from general knowledge of the public macOS
 * IOGraphicsTypes.h API, not copied from any file in this repo (none
 * exists here to copy from -- see the README) and not checked against a
 * real SDK header. Practical impact if wrong is low: CGDisplayIOServicePort()
 * (CGDirectDisplay.m:393), the only caller in CoreGraphics, never reaches
 * the code that reads these -- iokit_shim.c's IOServiceGetMatchingServices()
 * always returns kIOReturnNoDevice first, so the while-loop that would use
 * kDisplayVendorID/kDisplayProductID/kDisplaySerialNumber never executes.
 * This only matters if some other, unaudited caller (AppKit -- out of
 * scope, see docs/SPEC-iokit-coregraphics-build.md) reads these directly.
 */
#ifndef BSDOS_SHIM_IOGRAPHICSTYPES_H
#define BSDOS_SHIM_IOGRAPHICSTYPES_H

#define kDisplayVendorID        "DisplayVendorID"
#define kDisplayProductID       "DisplayProductID"
#define kDisplaySerialNumber    "DisplaySerialNumber"

enum {
	kDisplayVendorIDUnknown  = 0xFFFFFFFF,
	kDisplayProductIDGeneric = 0xFFFFFFFF
};

#endif /* BSDOS_SHIM_IOGRAPHICSTYPES_H */
