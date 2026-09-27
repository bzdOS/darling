/* Type-only CGLInternal.h stub.
 *
 * AppKit/X11.backend/X11Window.h does `#import <OpenGL/CGLInternal.h>` and
 * then declares two ivars of those types (CGLContextObj, CGLWindowRef).
 * There is no such header in any shipped macOS SDK: the OpenGL framework
 * ships CGLTypes.h, CGLContext.h, CGLDevice.h, gl.h and friends, but no
 * CGLInternal.h -- it is a private framework-internal header that upstream
 * cocotron happens to be able to see from a full Xcode install. This tree
 * vendors no full Xcode SDK, so the include can never resolve as-is.
 *
 * CGLContextObj is a public type and comes from the real CGLTypes.h next to
 * this file. CGLWindowRef is not declared in any public OpenGL header, so
 * it is declared here as an opaque struct pointer, the same shape the real
 * type has (it is a CGLWindowObjRef in the private headers). No CGL
 * function is ever called through either type here -- the X11 backend only
 * stores the context it was handed -- so an opaque declaration is enough
 * to compile, and nothing needs to link against a real OpenGL.
 */
#ifndef CGLINTERNAL_H_
#define CGLINTERNAL_H_

#include <OpenGL/CGLTypes.h>

struct _CGLWindowObject;
typedef struct _CGLWindowObject *CGLWindowRef;

#endif /* CGLINTERNAL_H_ */
