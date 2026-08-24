/* Type-only CGAffineTransform.h stub — see CGBase.h for why. Real Apple
 * headers split this into its own file from CGGeometry.h; kept as one
 * struct definition here and just re-exposed under both header names. */
#ifndef CGAFFINETRANSFORM_H_
#define CGAFFINETRANSFORM_H_

#include "CGBase.h"

typedef struct CGAffineTransform {
    CGFloat a, b, c, d;
    CGFloat tx, ty;
} CGAffineTransform;

#endif /* CGAFFINETRANSFORM_H_ */
