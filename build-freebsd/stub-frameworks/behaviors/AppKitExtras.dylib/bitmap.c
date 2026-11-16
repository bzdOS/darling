/* AppKitExtras behaviour: the pixel readers of NSBitmapImageRep.
 *
 * This AppKit (cocotron) implements -getPixel:atX:y: and -colorAtX:y: as NSUnimplementedMethod(): each call
 * logs "unimplemented" through NSLog (a CFTimeZone and a file lookup per call, about ten milliseconds here) and
 * returns without writing the pixel. Chromium reads an image pixel by pixel through them, so one image kept the
 * main thread busy for minutes (a stack of ChromeFW frames over -[NSBitmapImageRep getPixel:atX:y:] ->
 * NSLog -> CFTimeZoneCopySystem in every hang dump).
 *
 * Replaced once AppKit is mapped (add-image retry as in screen.c):
 *   -getPixel:atX:y:   the samples of the pixel, 1..16 and 32 bits per sample, chunky or planar
 *   -colorAtX:y:       a calibrated RGB (3+ samples) or white (1 sample) NSColor, alpha unpremultiplied
 * Both read through the public accessors (-getBitmapDataPlanes:, -bytesPerRow, ...), so the lazily created
 * bitmap of the image is made by AppKit itself. Float samples return nil/0. */
typedef void *Class;
typedef void *SEL;
typedef void *IMP;
typedef void *id;
extern Class objc_getClass(const char *name);
extern SEL sel_registerName(const char *name);
extern IMP class_replaceMethod(Class cls, SEL name, IMP imp, const char *types);
extern id objc_msgSend(id, SEL, ...);
struct mach_header;
extern void _dyld_register_func_for_add_image(void (*func)(const struct mach_header *, long));

#define S(x) sel_registerName(x)
#define SEND_L(o, s) (((long (*)(id, SEL))objc_msgSend)((o), S(s)))

#define NS_ALPHA_FIRST 1UL
#define NS_ALPHA_NONPREMULT 2UL
#define NS_FLOAT 4UL

static int get_pixel(id self, long x, long y, unsigned long *pixel, int max)
{
    unsigned char *planes[5] = {0, 0, 0, 0, 0};
    long bps = SEND_L(self, "bitsPerSample"), spp = SEND_L(self, "samplesPerPixel");
    long bpp = SEND_L(self, "bitsPerPixel"), bpr = SEND_L(self, "bytesPerRow");
    int planar = (int)(signed char)SEND_L(self, "isPlanar");
    int i;

    if (!pixel || x < 0 || y < 0 || x >= SEND_L(self, "pixelsWide") || y >= SEND_L(self, "pixelsHigh"))
        return 0;
    if (SEND_L(self, "bitmapFormat") & NS_FLOAT)
        return 0;
    if (bps <= 0)
        bps = 8;
    if (spp > max)
        spp = max;
    ((void (*)(id, SEL, unsigned char **))objc_msgSend)(self, S("getBitmapDataPlanes:"), planes);
    for (i = 0; i < spp; i++) {
        const unsigned char *row;
        long bit;
        if (planar) {
            row = planes[i] ? planes[i] + bpr * y : 0;
            bit = x * bps;
        } else {
            row = planes[0] ? planes[0] + bpr * y : 0;
            bit = x * bpp + i * bps;
        }
        if (!row) {
            pixel[i] = 0;
        } else if (bps == 8) {
            pixel[i] = row[bit / 8];
        } else if (bps == 16) {
            pixel[i] = (unsigned long)row[bit / 8] | ((unsigned long)row[bit / 8 + 1] << 8);
        } else if (bps == 32) {
            pixel[i] = (unsigned long)row[bit / 8] | ((unsigned long)row[bit / 8 + 1] << 8) |
                       ((unsigned long)row[bit / 8 + 2] << 16) | ((unsigned long)row[bit / 8 + 3] << 24);
        } else if (bps < 8) {
            pixel[i] = (row[bit / 8] >> (8 - bps - (int)(bit % 8))) & ((1u << bps) - 1);
        } else {
            pixel[i] = 0;
        }
    }
    return (int)spp;
}

static void get_pixel_imp(id self, SEL cmd, unsigned long *pixel, long x, long y)
{
    (void)cmd;
    get_pixel(self, x, y, pixel, 32);
}

static id color_at_imp(id self, SEL cmd, long x, long y)
{
    unsigned long p[8] = {0};
    double v[8], alpha = 1.0, maxv;
    long bps = SEND_L(self, "bitsPerSample"), fmt = SEND_L(self, "bitmapFormat");
    int n, ncol, i, has_alpha = (int)(signed char)SEND_L(self, "hasAlpha");
    id nscolor = objc_getClass("NSColor");
    (void)cmd;

    n = get_pixel(self, x, y, p, 8);
    if (n <= 0 || !nscolor)
        return 0;
    if (bps <= 0)
        bps = 8;
    maxv = (double)((1UL << bps) - 1);
    for (i = 0; i < n; i++)
        v[i] = (double)p[i] / maxv;
    ncol = has_alpha ? n - 1 : n;
    if (has_alpha) {
        if (fmt & NS_ALPHA_FIRST) {
            alpha = v[0];
            for (i = 0; i < ncol; i++)
                v[i] = v[i + 1];
        } else {
            alpha = v[n - 1];
        }
        if (!(fmt & NS_ALPHA_NONPREMULT) && alpha > 0.0)
            for (i = 0; i < ncol; i++) {
                v[i] /= alpha;
                if (v[i] > 1.0)
                    v[i] = 1.0;
            }
    }
    if (ncol >= 3)
        return ((id (*)(id, SEL, double, double, double, double))objc_msgSend)(
            nscolor, S("colorWithCalibratedRed:green:blue:alpha:"), v[0], v[1], v[2], alpha);
    if (ncol >= 1)
        return ((id (*)(id, SEL, double, double))objc_msgSend)(
            nscolor, S("colorWithCalibratedWhite:alpha:"), v[0], alpha);
    return 0;
}

static int done;

static void try_add(void)
{
    Class c;
    if (done)
        return;
    c = objc_getClass("NSBitmapImageRep");
    if (!c)
        return;
    class_replaceMethod(c, S("getPixel:atX:y:"), (IMP)get_pixel_imp, "v40@0:8^Q16q24q32");
    class_replaceMethod(c, S("colorAtX:y:"), (IMP)color_at_imp, "@32@0:8q16q24");
    done = 1;
}

static void on_image(const struct mach_header *mh, long slide) { (void)mh; (void)slide; try_add(); }

__attribute__((constructor)) static void appkit_extras_bitmap(void)
{
    try_add();
    if (!done)
        _dyld_register_func_for_add_image(on_image);
}
