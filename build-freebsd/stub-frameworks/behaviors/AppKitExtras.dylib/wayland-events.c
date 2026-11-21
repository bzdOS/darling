/* AppKitExtras behaviour: the Wayland backend's event wait.
 *
 * -[WaylandDisplay nextEventMatchingMask:untilDate:inMode:dequeue:] only ran wl_display_dispatch_pending and then the
 * base NSDisplay implementation, which runs the run loop once up to the date and answers with a placeholder event
 * (never nil). Two consequences:
 *   - a modal session (-[NSApplication runModalSession:], used by -[NSAlert runModal]) polls with [NSDate date] and
 *     loops while it gets an event, so it spun at full speed (mach_msg with a zero timeout, ~2000 per sample);
 *   - nothing but the blocking dispatch in window creation ever read the compositor socket, so configure, frame, key
 *     and pointer events were never received in the event loop.
 * The Wayland.backend binary cannot be rebuilt (its shim sources are gone), so the method is replaced here once the
 * backend is loaded (add-image retry as in screen.c). The replacement waits on the Wayland socket and the run loop in
 * slices of at most 20 ms until an event is queued, a run loop source ran, or the deadline passed; a deadline that is
 * already due in NSModalPanelRunLoopMode is lifted to 50 ms. Reads events with wl_display_dispatch once poll reports the socket readable.
 * The same change is kept as a source patch (build-freebsd/patches/cocotron-wayland-event-wait.patch).
 * If the wl_* functions cannot be resolved the original method runs unchanged. */
#include "../objc-proto.h"
typedef void *Ivar;
typedef void *Method;
struct pollfd { int fd; short events; short revents; };
extern IMP class_replaceMethod(Class cls, SEL name, IMP imp, const char *types);
extern Ivar class_getInstanceVariable(Class cls, const char *name);
extern long ivar_getOffset(Ivar iv);
extern void *dlopen(const char *path, int mode);
extern void *dlsym(void *handle, const char *name);
extern unsigned int _dyld_image_count(void);
extern const char *_dyld_get_image_name(unsigned int index);
extern int poll(struct pollfd *fds, unsigned long nfds, int timeout);
extern int strcmp(const char *a, const char *b);
extern char *strstr(const char *a, const char *b);
extern int *__error(void);
extern long write(int, const void *, unsigned long);
struct mach_header;
extern void _dyld_register_func_for_add_image(void (*func)(const struct mach_header *, long));

#define RTLD_NOLOAD 0x10
#define RTLD_LAZY 1
#define POLLIN 1
#define EAGAIN_ 35

/* Compositor-driven resize. The backend's xdg_toplevel.configure callback ignores width/height, so after sway tiled the
 * window the shm buffer stayed at the size it was created with. -[WaylandWindow initWithDelegate:] is wrapped: once it has
 * returned, the window's xdg_toplevel proxy gets a copy of its listener whose configure records the size and then calls
 * the original; the recorded size is applied after the dispatch (outside libwayland) by resizing the NSWindow frame, which
 * makes the backend rebuild its context and reallocate the shm buffers at the next flush. Layout of wl_proxy (libwayland):
 * object{interface,implementation,id}=24 bytes, display, queue, flags/refcount, user_data at 48; the layout is verified
 * (user_data == window) before the listener is replaced. */
typedef struct { double x, y, w, h; } RectD;
extern void objc_msgSend_stret(void *, id, SEL, ...);
static IMP orig_win_init;
static void *orig_listener[4], *my_listener[4];
static long toplevel_off = -1, wdelegate_off = -1;
static id pending_win;
static int pending_w, pending_h;

static void my_toplevel_configure(void *data, void *toplevel, int w, int h, void *states)
{
    ((void (*)(void *, void *, int, int, void *))orig_listener[0])(data, toplevel, w, h, states);
    if (w > 0 && h > 0) {
        pending_win = (id)data;
        pending_w = w;
        pending_h = h;
    }
}

static void apply_pending(void)
{
    RectD f;
    id win = pending_win, del;
    if (!win || wdelegate_off < 0)
        return;
    pending_win = 0;
    del = *(id *)((char *)win + wdelegate_off);
    if (!del)
        return;
    objc_msgSend_stret(&f, del, S("frame"));
    if ((int)f.w == pending_w && (int)f.h == pending_h)
        return;
    f.w = pending_w;
    f.h = pending_h;
    ((void (*)(id, SEL, RectD, char))objc_msgSend)(del, S("setFrame:display:"), f, 1);
    write(2, "WAYLAND-EVENTS: configure resize applied\n", 41);
}

static id win_init(id self, SEL cmd, id delegate)
{
    id r = ((id (*)(id, SEL, id))orig_win_init)(self, cmd, delegate);
    if (r && toplevel_off >= 0) {
        char *proxy = *(char **)((char *)r + toplevel_off);
        if (proxy && *(void **)(proxy + 48) == (void *)r && *(void **)(proxy + 8) &&
            *(void **)(proxy + 8) != (void *)my_listener) {
            int i;
            for (i = 0; i < 4; i++)
                orig_listener[i] = ((void **)*(void **)(proxy + 8))[i];
            for (i = 0; i < 4; i++)
                my_listener[i] = orig_listener[i];
            my_listener[0] = (void *)my_toplevel_configure;
            *(void **)(proxy + 8) = (void *)my_listener;
        } else
            write(2, "WAYLAND-EVENTS: xdg_toplevel proxy layout unexpected, resize hook off\n", 70);
    }
    return r;
}

static IMP orig_next;
static int (*p_dispatch)(void *);
static int (*p_dispatch_pending)(void *);
static int (*p_flush)(void *);
static int (*p_get_fd)(void *);
static long display_off = -1;

static double now_ref(void)
{
    return ((double (*)(id, SEL))objc_msgSend)(objc_getClass("NSDate"), S("timeIntervalSinceReferenceDate"));
}

static id date_now(void)
{
    return ((id (*)(id, SEL))objc_msgSend)(objc_getClass("NSDate"), S("date"));
}

/* The shim exports no wl_display_prepare_read/read_events, so a readable socket is read with wl_display_dispatch
 * (it reads what is there and dispatches it; it is only called when poll says data is waiting). */
static void wait_wayland(void *d, double seconds)
{
    struct pollfd pfd;
    int ms, r;
    p_dispatch_pending(d);
    if (p_flush(d) < 0 && *__error() != EAGAIN_)
        return;
    pfd.fd = p_get_fd(d);
    pfd.events = POLLIN;
    pfd.revents = 0;
    ms = seconds <= 0 ? 0 : (int)(seconds * 1000.0 + 0.5);
    r = poll(&pfd, 1, ms);
    if (r > 0 && (pfd.revents & POLLIN))
        p_dispatch(d);
    apply_pending();
}

static id wayland_next_event(id self, SEL cmd, unsigned long mask, id untilDate, id mode, char dequeue)
{
    void *d = display_off >= 0 ? *(void **)((char *)self + display_off) : 0;
    id queue = 0;
    double deadline, now;
    Ivar qv = class_getInstanceVariable(*(Class *)self ? (Class)objc_getClass("NSDisplay") : 0, "_eventQueue");
    if (!d || !qv || !p_dispatch)
        return ((id (*)(id, SEL, unsigned long, id, id, char))orig_next)(self, cmd, mask, untilDate, mode, dequeue);
    queue = *(id *)((char *)self + ivar_getOffset(qv));
    p_dispatch_pending(d);
    apply_pending();
    p_flush(d);
    now = now_ref();
    deadline = untilDate ? ((double (*)(id, SEL))objc_msgSend)(untilDate, S("timeIntervalSinceReferenceDate"))
                         : now + 1e9;
    if (mode && deadline <= now) {
        const char *m = ((const char *(*)(id, SEL))objc_msgSend)(mode, S("UTF8String"));
        if (m && strcmp(m, "NSModalPanelRunLoopMode") == 0)
            deadline = now + 0.05;
    }
    while (((long (*)(id, SEL))objc_msgSend)(queue, S("count")) == 0) {
        double slice;
        id loop;
        now = now_ref();
        if (now >= deadline)
            break;
        slice = deadline - now;
        if (slice > 0.02)
            slice = 0.02;
        wait_wayland(d, slice);
        loop = ((id (*)(id, SEL))objc_msgSend)(objc_getClass("NSRunLoop"), S("currentRunLoop"));
        if (((char (*)(id, SEL, id, id))objc_msgSend)(loop, S("runMode:beforeDate:"), mode, date_now()))
            break;
    }
    return ((id (*)(id, SEL, unsigned long, id, id, char))orig_next)(self, cmd, mask, date_now(), mode, dequeue);
}

static void *backend_handle(void)
{
    unsigned int i, n = _dyld_image_count();
    for (i = 0; i < n; i++) {
        const char *nm = _dyld_get_image_name(i);
        if (nm && strstr(nm, "Wayland.backend/Contents/MacOS/Wayland"))
            return dlopen(nm, RTLD_NOLOAD | RTLD_LAZY);
    }
    return 0;
}

static int done;

static void note(const char *msg, unsigned long n)
{
    static unsigned int shown;
    unsigned int bit = 1u << (n & 15);
    if (shown & bit)
        return;
    shown |= bit;
    write(2, msg, n);
}
#define NOTE(m) note(m "\n", sizeof(m))

static void try_add(void)
{
    Class c;
    void *h;
    Ivar iv;
    if (done)
        return;
    c = objc_getClass("WaylandDisplay");
    if (!c) {
        NOTE("WAYLAND-EVENTS: class WaylandDisplay not registered yet");
        return;
    }
    h = backend_handle();
    if (!h) {
        NOTE("WAYLAND-EVENTS: backend image handle not found");
        return;
    }
    p_dispatch = dlsym(h, "wl_display_dispatch");
    p_dispatch_pending = dlsym(h, "wl_display_dispatch_pending");
    p_flush = dlsym(h, "wl_display_flush");
    p_get_fd = dlsym(h, "wl_display_get_fd");
    iv = class_getInstanceVariable(c, "_display");
    if (iv)
        display_off = ivar_getOffset(iv);
    if (!p_dispatch || !p_dispatch_pending || !p_flush || !p_get_fd || display_off < 0) {
        p_dispatch = 0; /* the original method stays in place */
        NOTE("WAYLAND-EVENTS: wl_* symbols or _display ivar not found, original method kept");
        done = 1;
        return;
    }
    orig_next = class_replaceMethod(c, S("nextEventMatchingMask:untilDate:inMode:dequeue:"), (IMP)wayland_next_event,
                                    "@48@0:8Q16@24@32c40");
    done = orig_next != 0;
    c = objc_getClass("WaylandWindow");
    if (done && c && !orig_win_init) {
        iv = class_getInstanceVariable(c, "_xdgToplevel");
        toplevel_off = iv ? ivar_getOffset(iv) : -1;
        iv = class_getInstanceVariable(c, "_delegate");
        wdelegate_off = iv ? ivar_getOffset(iv) : -1;
        if (toplevel_off >= 0 && wdelegate_off >= 0)
            orig_win_init = class_replaceMethod(c, S("initWithDelegate:"), (IMP)win_init, "@24@0:8@16");
    }
    if (done)
        write(2, "WAYLAND-EVENTS installed: blocking event wait in -[WaylandDisplay nextEventMatchingMask:...]\n", 93);
}

typedef struct { const char *dli_fname; void *dli_fbase; const char *dli_sname; void *dli_saddr; } Dl_info_x;
extern int dladdr(const void *, Dl_info_x *);
extern Class object_getClass(id obj);

/* The backend bundle is loaded by AppKit at run time and its class is registered after the image callbacks ran, so the
 * replacement cannot be installed from the image callback. +[NSDisplay currentDisplay] is asked for by AppKit every time it
 * needs the display, by which time the Wayland display exists: that is hooked to install the replacement (once). */
static IMP orig_current;
static int hooked;

static id current_display(id self, SEL cmd)
{
    id d = ((id (*)(id, SEL))orig_current)(self, cmd);
    if (!done)
        try_add();
    return d;
}

static void hook_current(void)
{
    Class c, meta;
    if (hooked)
        return;
    c = objc_getClass("NSDisplay");
    if (!c)
        return;
    meta = object_getClass(c);
    orig_current = class_replaceMethod(meta, S("currentDisplay"), (IMP)current_display, "@16@0:8");
    hooked = orig_current != 0;
}

/* Only AppKit itself can make NSDisplay appear: do nothing for the other images (the callback runs for every image a
 * thread loads). */
static void on_image(const struct mach_header *mh, long slide)
{
    Dl_info_x di;
    static int armed; /* set once AppKit itself was seen: its classes may be registered a moment later, retry then */
    (void)slide;
    if (hooked)
        return;
    if (!armed) {
        if (!dladdr(mh, &di) || !di.dli_fname || !strstr(di.dli_fname, "AppKit.framework/Versions/C/AppKit"))
            return;
        armed = 1;
    }
    hook_current();
}

__attribute__((constructor)) static void appkit_extras_wayland_events(void)
{
    hook_current();
    try_add();
    if (!hooked)
        _dyld_register_func_for_add_image(on_image);
}
