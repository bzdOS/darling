/* Type-only CoreGraphics.h stub — see CGBase.h in this same directory for
 * why. CGPoint/CGSize/CGRect/CGAffineTransform are the exact, unchanging
 * public struct layouts from Apple's CGGeometry.h; no function bodies or
 * linkable symbols here, only what NSValue/NSCoder need to encode them. */
#ifndef CGGEOMETRY_H_
#define CGGEOMETRY_H_

#include "CGBase.h"

struct CGPoint {
    CGFloat x;
    CGFloat y;
};
typedef struct CGPoint CGPoint;

struct CGSize {
    CGFloat width;
    CGFloat height;
};
typedef struct CGSize CGSize;

struct CGRect {
    CGPoint origin;
    CGSize size;
};
typedef struct CGRect CGRect;

struct CGVector {
    CGFloat dx;
    CGFloat dy;
};
typedef struct CGVector CGVector;

typedef enum CGRectEdge {
    CGRectMinXEdge, CGRectMinYEdge, CGRectMaxXEdge, CGRectMaxYEdge
} CGRectEdge;

/* Real Apple headers define these as trivial static-inline constructors —
 * no behavior beyond filling in the struct fields. */
static inline CGPoint CGPointMake(CGFloat x, CGFloat y) {
    CGPoint p; p.x = x; p.y = y; return p;
}
static inline CGSize CGSizeMake(CGFloat width, CGFloat height) {
    CGSize s; s.width = width; s.height = height; return s;
}
static inline CGRect CGRectMake(CGFloat x, CGFloat y, CGFloat width, CGFloat height) {
    CGRect r; r.origin = CGPointMake(x, y); r.size = CGSizeMake(width, height); return r;
}

#endif /* CGGEOMETRY_H_ */
