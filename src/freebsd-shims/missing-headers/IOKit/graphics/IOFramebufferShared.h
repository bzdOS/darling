/*
 * IOFramebufferShared.h -- shim replacement.
 *
 * See src/freebsd-shims/missing-headers/README.md for the full story: this
 * path is a dangling symlink into the uninitialized `IOGraphics` submodule
 * everywhere else in this repo. graphics.subproj/IOGraphicsLib.h #includes
 * this file unconditionally, but neither IOGraphicsLib.h's own body nor any
 * CoreGraphics source this shim targets (verified: `grep -rn
 * 'IOFramebuffer\|kIOFB' src/external/cocotron/CoreGraphics` finds nothing)
 * uses any identifier this header would define. Left intentionally empty
 * so the #include succeeds.
 */
#ifndef BSDOS_SHIM_IOFRAMEBUFFERSHARED_H
#define BSDOS_SHIM_IOFRAMEBUFFERSHARED_H
#endif /* BSDOS_SHIM_IOFRAMEBUFFERSHARED_H */
