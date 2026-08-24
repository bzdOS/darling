/* Type-only CGBase.h/CoreGraphics.h stub.
 *
 * CoreGraphics.framework itself does not exist in this project's vendored
 * tree at all (confirmed: no src/external submodule, no symlink anywhere in
 * the SDK resolves to one — a genuine, structural gap, not a missing-init
 * one). Several Foundation source files pull in <CoreGraphics/CoreGraphics.h>
 * purely for the CGFloat/CGPoint/CGSize/CGRect *type definitions* (to
 * support NSValue/NSCoder encoding CGPoint/CGSize/CGRect) — none of them
 * call an actual CoreGraphics function or link against a CoreGraphics
 * binary. These are the exact, unchanging public definitions from Apple's
 * own CGBase.h (CGFLOAT_IS_DOUBLE has been 1 on all 64-bit Apple platforms
 * since day one of the 64-bit ABI, so this isn't a guess). */
#ifndef CGBASE_H_
#define CGBASE_H_

#include <stdint.h>

#if defined(__LP64__) && __LP64__
# define CGFLOAT_TYPE double
# define CGFLOAT_IS_DOUBLE 1
#else
# define CGFLOAT_TYPE float
# define CGFLOAT_IS_DOUBLE 0
#endif

typedef CGFLOAT_TYPE CGFloat;
#define CGFLOAT_MIN DBL_MIN
#define CGFLOAT_MAX DBL_MAX

#endif /* CGBASE_H_ */
