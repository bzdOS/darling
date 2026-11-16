/* bitmap-probe: -[NSBitmapImageRep getPixel:atX:y:] and -colorAtX:y: must return the pixel, and quickly.
 * Makes a 4x4 RGBA image, sets pixel (1,2) to 10,20,30,255 through bitmapData, reads it back with getPixel and
 * colorAtX, then times CALLS getPixel calls. With the stock AppKit the method is NSUnimplementedMethod(): the array
 * stays untouched and each call logs through NSLog (about ten milliseconds). Prints one BITMAP-PROBE line:
 *   BITMAP-PROBE got=<samples> color=<nil|ok> calls=<n> ms=<total> */
#include <stdio.h>
#include <time.h>

#define CALLS 300
extern void *objc_getClass(const char *);
extern void *sel_registerName(const char *);
extern void *objc_msgSend(void *, void *, ...);
#define S(x) sel_registerName(x)
#define M(...) objc_msgSend(__VA_ARGS__)

int main(void)
{
    unsigned long px[5] = {0xdead, 0xdead, 0xdead, 0xdead, 0xdead};
    void *cs = M(objc_getClass("NSString"), S("stringWithUTF8String:"), "NSDeviceRGBColorSpace");
    void *rep = M(M(objc_getClass("NSBitmapImageRep"), S("alloc")),
                  S("initWithBitmapDataPlanes:pixelsWide:pixelsHigh:bitsPerSample:samplesPerPixel:hasAlpha:isPlanar:colorSpaceName:bytesPerRow:bitsPerPixel:"),
                  (void *)0, 4L, 4L, 8L, 4L, (long)1, (long)0, cs, 0L, 0L);
    unsigned char *bits;
    long bpr, i;
    struct timespec a, b;
    void *col;

    if (!rep) { printf("BITMAP-PROBE got=none color=nil calls=0 ms=0 [no rep]\n"); return 0; }
    bits = (unsigned char *)M(rep, S("bitmapData"));
    bpr = (long)M(rep, S("bytesPerRow"));
    if (!bits) { printf("BITMAP-PROBE got=none color=nil calls=0 ms=0 [no data]\n"); return 0; }
    bits += bpr * 2 + 4;
    bits[0] = 10; bits[1] = 20; bits[2] = 30; bits[3] = 255;
    M(rep, S("getPixel:atX:y:"), px, 1L, 2L);
    col = M(rep, S("colorAtX:y:"), 1L, 2L);
    clock_gettime(CLOCK_MONOTONIC, &a);
    for (i = 0; i < CALLS; i++)
        M(rep, S("getPixel:atX:y:"), px, 1L, 2L);
    clock_gettime(CLOCK_MONOTONIC, &b);
    printf("BITMAP-PROBE got=%lu,%lu,%lu,%lu color=%s calls=%d ms=%ld\n", px[0], px[1], px[2], px[3],
           col ? "ok" : "nil", CALLS, (b.tv_sec - a.tv_sec) * 1000 + (b.tv_nsec - a.tv_nsec) / 1000000);
    fflush(stdout);
    printf("BITMAP-PROBE DONE\n");
    fflush(stdout);
    return 0;
}
