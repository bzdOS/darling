/* bitmap-draw-probe: drawing into an NSBitmapImageRep must reach its bitmapData.
 * Fills a 4x4 RGBA rep with opaque red (255,0,0,255) through (a) NSImage lockFocusOnRepresentation: and
 * (b) +[NSGraphicsContext graphicsContextWithBitmapImageRep:] made current, reads pixel (1,1) with getPixel
 * and prints one BITMAP-DRAW-PROBE line:
 * (0.2,0.4,0.6,1 = 51,102,153,255), then asks [NSColor colorUsingColorSpace:[NSColorSpace sRGBColorSpace]] of a
 * calibrated colour and of a gray colour: Chrome falls back to drawing into a 1x1 rep and recurses until it is non-nil.
 *   BITMAP-DRAW-PROBE lockfocus=<r,g,b,a> ctx=<r,g,b,a> ctxnil=<0|1> srgb=<ok|nil> srgbgray=<ok|nil> */
#include <stdio.h>
typedef struct { double x, y, w, h; } R;
extern void *objc_getClass(const char *);
extern void *sel_registerName(const char *);
extern void *objc_msgSend(void *, void *, ...);
#define S(x) sel_registerName(x)
#define M(...) objc_msgSend(__VA_ARGS__)
#define C(x) objc_getClass(x)

static void *mkrep(void)
{
    void *cs = M(C("NSString"), S("stringWithUTF8String:"), "NSDeviceRGBColorSpace");
    return M(M(C("NSBitmapImageRep"), S("alloc")),
             S("initWithBitmapDataPlanes:pixelsWide:pixelsHigh:bitsPerSample:samplesPerPixel:hasAlpha:isPlanar:colorSpaceName:bytesPerRow:bitsPerPixel:"),
             (void *)0, 4L, 4L, 8L, 4L, (long)1, (long)0, cs, 0L, 0L);
}
static void px(void *rep, char *out)
{
    unsigned long p[5] = {9, 9, 9, 9, 9};
    M(rep, S("getPixel:atX:y:"), p, 1L, 1L);
    sprintf(out, "%lu,%lu,%lu,%lu", p[0], p[1], p[2], p[3]);
}
static void paint(void)
{
    void *(*mc)(void *, void *, double, double, double, double) = (void *)objc_msgSend;
    void (*fr)(void *, void *, R) = (void *)objc_msgSend;
    void *col = mc(C("NSColor"), S("colorWithCalibratedRed:green:blue:alpha:"), 0.2, 0.4, 0.6, 1.0);
    R r = {0, 0, 4, 4};
    M(col, S("set"));
    fr(C("NSBezierPath"), S("fillRect:"), r);
}
int main(void)
{
    char a[40] = "none", b[40] = "none";
    void *rep1 = mkrep(), *rep2 = mkrep(), *img, *ctx;
    struct { double w, h; } sz = {4, 4};
    void (*setsz)(void *, void *, double, double) = (void *)objc_msgSend;
    if (!rep1 || !rep2) { printf("BITMAP-DRAW-PROBE lockfocus=none ctx=none ctxnil=1 [no rep]\n"); printf("BITMAP-DRAW-PROBE DONE\n"); return 0; }
    img = M(M(C("NSImage"), S("alloc")), S("init"));
    setsz(img, S("setSize:"), sz.w, sz.h);
    M(img, S("addRepresentation:"), rep1);
    M(img, S("lockFocusOnRepresentation:"), rep1);
    paint();
    M(img, S("unlockFocus"));
    px(rep1, a);
    ctx = M(C("NSGraphicsContext"), S("graphicsContextWithBitmapImageRep:"), rep2);
    if (ctx) {
        M(C("NSGraphicsContext"), S("saveGraphicsState"));
        M(C("NSGraphicsContext"), S("setCurrentContext:"), ctx);
        paint();
        M(C("NSGraphicsContext"), S("restoreGraphicsState"));
        px(rep2, b);
    }
    {
        void *(*mc)(void *, void *, double, double, double, double) = (void *)objc_msgSend;
        void *srgb = (C("NSColorSpace") && M(C("NSColorSpace"), S("respondsToSelector:"), S("sRGBColorSpace"))) ? M(C("NSColorSpace"), S("sRGBColorSpace")) : 0;
        void *c1 = mc(C("NSColor"), S("colorWithCalibratedRed:green:blue:alpha:"), 0.2, 0.4, 0.6, 1.0);
        void *(*mw)(void *, void *, double, double) = (void *)objc_msgSend;
        void *c2 = mw(C("NSColor"), S("colorWithCalibratedWhite:alpha:"), 0.5, 1.0);
        int ok1 = 0, ok2 = 0;
        if (M(c1, S("respondsToSelector:"), S("colorUsingColorSpace:"))) ok1 = M(c1, S("colorUsingColorSpace:"), srgb) != 0;
        if (c2 && M(c2, S("respondsToSelector:"), S("colorUsingColorSpace:"))) ok2 = M(c2, S("colorUsingColorSpace:"), srgb) != 0;
        fprintf(stderr, "cs=%p c2=%p\n", srgb, c2); printf("BITMAP-DRAW-PROBE lockfocus=%s ctx=%s ctxnil=%d srgb=%s srgbgray=%s\n", a, b, ctx ? 0 : 1, ok1 ? "ok" : "nil", ok2 ? "ok" : "nil");
    }
    fflush(stdout);
    printf("BITMAP-DRAW-PROBE DONE\n");
    fflush(stdout);
    return 0;
}
