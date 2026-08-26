/*
 * IOGraphicsInterface.h -- shim replacement.
 *
 * See src/freebsd-shims/missing-headers/README.md. Same situation as this
 * directory's IOFramebufferShared.h: dangling symlink into the
 * uninitialized `IOGraphics` submodule, pulled in unconditionally by
 * graphics.subproj/IOGraphicsLib.h, but nothing in IOGraphicsLib.h's own
 * body or in the CoreGraphics sources this shim targets uses any
 * identifier from it (verified: `grep -rn 'IOGraphicsInterface\|
 * IOAccelerator' src/external/cocotron/CoreGraphics` finds nothing beyond
 * this file's own name in the #include line). Left intentionally empty.
 */
#ifndef BSDOS_SHIM_IOGRAPHICSINTERFACE_H
#define BSDOS_SHIM_IOGRAPHICSINTERFACE_H
#endif /* BSDOS_SHIM_IOGRAPHICSINTERFACE_H */
