/* window-draw-probe: an NSWindow with a solid red background on the Wayland backend, no Chrome.
 * Orders the window front and runs the event loop for WINDOW_DRAW_SECS (default 15) seconds so a compositor can map it.
 * Prints WINDOW-DRAW-PROBE READY once it is ordered front and WINDOW-DRAW-PROBE DONE at the end. */
#include <stdio.h>
#include <stdlib.h>
typedef struct { double x, y, w, h; } R;
extern void *objc_getClass(const char *);
extern void *sel_registerName(const char *);
extern void *objc_msgSend(void *, void *, ...);
#define S(x) sel_registerName(x)
#define M(...) objc_msgSend(__VA_ARGS__)
#define C(x) objc_getClass(x)
int main(void)
{
    void *(*mc)(void *, void *, double, double, double, double) = (void *)objc_msgSend;
    void *(*mw)(void *, void *, R, unsigned long, unsigned long, char) = (void *)objc_msgSend;
    void *(*until)(void *, void *, double) = (void *)objc_msgSend;
    void *(*nxt)(void *, void *, unsigned long, void *, void *, char) = (void *)objc_msgSend;
    const char *e = getenv("WINDOW_DRAW_SECS");
    int i, secs = e ? atoi(e) : 15;
    void *pool = M(M(C("NSAutoreleasePool"), S("alloc")), S("init"));
    void *app = M(C("NSApplication"), S("sharedApplication"));
    R r = {100, 100, 400, 300};
    void *win = mw(M(C("NSWindow"), S("alloc")), S("initWithContentRect:styleMask:backing:defer:"), r, 15UL, 2UL, 0);
    void *mode = M(C("NSString"), S("stringWithUTF8String:"), "kCFRunLoopDefaultMode");
    M(win, S("setBackgroundColor:"), mc(C("NSColor"), S("colorWithCalibratedRed:green:blue:alpha:"), 1.0, 0.0, 0.0, 1.0));
    M(win, S("makeKeyAndOrderFront:"), 0);
    printf("WINDOW-DRAW-PROBE READY win=%p\n", win);
    fflush(stdout);
    for (i = 0; i < secs * 5; i++) {
        void *ev = nxt(app, S("nextEventMatchingMask:untilDate:inMode:dequeue:"), ~0UL, until(C("NSDate"), S("dateWithTimeIntervalSinceNow:"), 0.2), mode, 1);
        if (ev) M(app, S("sendEvent:"), ev);
        M(win, S("display"));
    }
    printf("WINDOW-DRAW-PROBE DONE\n");
    fflush(stdout);
    return 0;
}
