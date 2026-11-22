/* AppKitExtras behaviour: DARLING_APPKIT_TRACE=1 traces window creation and the Wayland requests (off by default).
 *
 * Purpose: find the last window/Wayland operation before a CHECK trap in a GUI application. With the variable unset the
 * constructor returns at once and nothing is hooked. Lines go to stderr, one per call:
 *   [appkit-trace] #SEQ tid=T NSWindow 0xW <Class> <selector> args...
 *   [appkit-trace] #SEQ tid=T WL-REQ <interface>.<request>(opcode) proxy=0x.. [new=<interface> ver=N] caller=<image>+0xOFF
 * NSWindow: initWithContentRect:styleMask:backing:defer:[screen:], makeKeyAndOrderFront:, orderFront:, orderFrontRegardless,
 * orderOut:, orderWindow:relativeTo:, makeKeyWindow, makeMainWindow, setFrame:display:[animate:], setContentSize:, close; they
 * are wrapped once AppKit has registered NSWindow (add-image retry as in window.c).
 * Wayland: the Wayland.backend binary cannot be rebuilt (its shim sources are gone), so once its image is loaded the entry of
 * its wl_proxy_marshal, wl_proxy_marshal_constructor, wl_proxy_marshal_constructor_versioned and wl_proxy_marshal_flags
 * is patched: the shim's four entries share one 14 byte prologue (push %rbx; sub $0x38,%rsp; mov %rdi,(%rsp);
 * mov %rsi,8(%rsp)) that is replaced by an absolute jump to a thunk, which saves the argument registers, calls the logger and
 * runs the displaced instructions. The prologue bytes are compared before patching (a different binary is left alone) and the
 * entries are located by offset from the image header (tests/vendor/wayland-backend/Wayland, sha256 b918e22e...).
 * Compositor events and wl_display_dispatch are not traced (their entries have another prologue). */
#include "../objc-proto.h"
typedef void *Method;
struct mach_header;
extern const char *getenv(const char *);
extern int vdprintf(int, const char *, __builtin_va_list);
extern int mprotect(void *, unsigned long, int);
extern int pthread_threadid_np(void *, unsigned long long *);
extern int strncmp(const char *, const char *, unsigned long);
extern int memcmp(const void *, const void *, unsigned long);
extern void *memcpy(void *, const void *, unsigned long);
extern char *strstr(const char *a, const char *b);
extern Method class_getInstanceMethod(Class cls, SEL name);
extern IMP method_getImplementation(Method m);
extern const char *method_getTypeEncoding(Method m);
extern IMP class_replaceMethod(Class cls, SEL name, IMP imp, const char *types);
extern Class object_getClass(id obj);
extern const char *class_getName(Class cls);
extern void _dyld_register_func_for_add_image(void (*func)(const struct mach_header *, long));
typedef struct { const char *dli_fname; void *dli_fbase; const char *dli_sname; void *dli_saddr; } TrDlInfo;
extern int dladdr(const void *, TrDlInfo *);
extern unsigned int _dyld_image_count(void);
extern const struct mach_header *_dyld_get_image_header(unsigned int index);

typedef struct { double x, y, w, h; } TrRect;
static int tr_win_done, tr_wl_done, tr_armed;
static unsigned long tr_seq;

extern int dprintf(int, const char *, ...);

static void tr(const char *fmt, ...)
{
    unsigned long long tid = 0;
    __builtin_va_list ap;
    pthread_threadid_np(0, &tid);
    dprintf(2, "[appkit-trace] #%lu tid=%llu ", __sync_add_and_fetch(&tr_seq, 1), tid);
    __builtin_va_start(ap, fmt);
    vdprintf(2, fmt, ap);
    __builtin_va_end(ap);
}

/* ---- NSWindow ---- */
static const char *cname(id o)
{
    return o ? class_getName(object_getClass(o)) : "nil";
}

static IMP o_init, o_init_screen, o_mkf, o_of, o_ofr, o_oo, o_ow, o_mkw, o_mmw, o_sf, o_sfa, o_scs, o_close;

static id w_init(id self, SEL cmd, TrRect r, unsigned long style, unsigned long backing, signed char defer)
{
    id res;
    tr("NSWindow %p %s initWithContentRect:{%g,%g,%g,%g} styleMask:0x%lx backing:%lu defer:%d\n", self, cname(self), r.x,
       r.y, r.w, r.h, style, backing, defer);
    res = ((id (*)(id, SEL, TrRect, unsigned long, unsigned long, signed char))o_init)(self, cmd, r, style, backing, defer);
    tr("NSWindow %p %s initWithContentRect returned %p\n", self, cname(self), res);
    return res;
}

static id w_init_screen(id self, SEL cmd, TrRect r, unsigned long style, unsigned long backing, signed char defer,
                        id screen)
{
    id res;
    tr("NSWindow %p %s initWithContentRect:{%g,%g,%g,%g} styleMask:0x%lx backing:%lu defer:%d screen:%p\n", self,
       cname(self), r.x, r.y, r.w, r.h, style, backing, defer, screen);
    res = ((id (*)(id, SEL, TrRect, unsigned long, unsigned long, signed char, id))o_init_screen)(self, cmd, r, style,
                                                                                                   backing, defer, screen);
    tr("NSWindow %p %s initWithContentRect:screen: returned %p\n", self, cname(self), res);
    return res;
}

#define W_ID1(fn, orig, name)                                                       \
    static void fn(id self, SEL cmd, id a)                                          \
    {                                                                               \
        tr("NSWindow %p %s " name " sender=%p\n", self, cname(self), a);            \
        ((void (*)(id, SEL, id))orig)(self, cmd, a);                                \
        tr("NSWindow %p " name " done\n", self);                                    \
    }
W_ID1(w_mkf, o_mkf, "makeKeyAndOrderFront:")
W_ID1(w_of, o_of, "orderFront:")
W_ID1(w_oo, o_oo, "orderOut:")

#define W_VOID(fn, orig, name)                                                      \
    static void fn(id self, SEL cmd)                                                \
    {                                                                               \
        tr("NSWindow %p %s " name "\n", self, cname(self));                         \
        ((void (*)(id, SEL))orig)(self, cmd);                                       \
        tr("NSWindow %p " name " done\n", self);                                    \
    }
W_VOID(w_ofr, o_ofr, "orderFrontRegardless")
W_VOID(w_mkw, o_mkw, "makeKeyWindow")
W_VOID(w_mmw, o_mmw, "makeMainWindow")
W_VOID(w_close, o_close, "close")

static void w_ow(id self, SEL cmd, long mode, long rel)
{
    tr("NSWindow %p %s orderWindow:%ld relativeTo:%ld\n", self, cname(self), mode, rel);
    ((void (*)(id, SEL, long, long))o_ow)(self, cmd, mode, rel);
    tr("NSWindow %p orderWindow done\n", self);
}

static void w_sf(id self, SEL cmd, TrRect r, signed char display)
{
    tr("NSWindow %p %s setFrame:{%g,%g,%g,%g} display:%d\n", self, cname(self), r.x, r.y, r.w, r.h, display);
    ((void (*)(id, SEL, TrRect, signed char))o_sf)(self, cmd, r, display);
    tr("NSWindow %p setFrame:display: done\n", self);
}

static void w_sfa(id self, SEL cmd, TrRect r, signed char display, signed char animate)
{
    tr("NSWindow %p %s setFrame:{%g,%g,%g,%g} display:%d animate:%d\n", self, cname(self), r.x, r.y, r.w, r.h, display,
       animate);
    ((void (*)(id, SEL, TrRect, signed char, signed char))o_sfa)(self, cmd, r, display, animate);
    tr("NSWindow %p setFrame:display:animate: done\n", self);
}

static void w_scs(id self, SEL cmd, double w, double h)
{
    tr("NSWindow %p %s setContentSize:{%g,%g}\n", self, cname(self), w, h);
    ((void (*)(id, SEL, double, double))o_scs)(self, cmd, w, h);
    tr("NSWindow %p setContentSize: done\n", self);
}

static void hook_sel(Class c, const char *sel, IMP repl, IMP *orig)
{
    Method m = class_getInstanceMethod(c, S(sel));
    if (!m) {
        tr("NSWindow hook: %s not found\n", sel);
        return;
    }
    *orig = method_getImplementation(m);
    class_replaceMethod(c, S(sel), repl, method_getTypeEncoding(m));
}

static void try_window(void)
{
    Class c;
    if (tr_win_done)
        return;
    c = objc_getClass("NSWindow");
    if (!c)
        return;
    hook_sel(c, "initWithContentRect:styleMask:backing:defer:", (IMP)w_init, &o_init);
    hook_sel(c, "initWithContentRect:styleMask:backing:defer:screen:", (IMP)w_init_screen, &o_init_screen);
    hook_sel(c, "makeKeyAndOrderFront:", (IMP)w_mkf, &o_mkf);
    hook_sel(c, "orderFront:", (IMP)w_of, &o_of);
    hook_sel(c, "orderFrontRegardless", (IMP)w_ofr, &o_ofr);
    hook_sel(c, "orderOut:", (IMP)w_oo, &o_oo);
    hook_sel(c, "orderWindow:relativeTo:", (IMP)w_ow, &o_ow);
    hook_sel(c, "makeKeyWindow", (IMP)w_mkw, &o_mkw);
    hook_sel(c, "makeMainWindow", (IMP)w_mmw, &o_mmw);
    hook_sel(c, "setFrame:display:", (IMP)w_sf, &o_sf);
    hook_sel(c, "setFrame:display:animate:", (IMP)w_sfa, &o_sfa);
    hook_sel(c, "setContentSize:", (IMP)w_scs, &o_scs);
    hook_sel(c, "close", (IMP)w_close, &o_close);
    tr_win_done = 1;
    tr("NSWindow hooks installed\n");
}

/* ---- Wayland requests ---- */
static const char *wl_names[4] = { "wl_proxy_marshal", "wl_proxy_marshal_constructor", "wl_proxy_marshal_constructor_versioned",
                                   "wl_proxy_marshal_flags" };
/* offsets of the four entries from the image header (tests/vendor/wayland-backend/Wayland) */
static const unsigned long wl_off[4] = { 0x7309, 0x736c, 0x73db, 0x72a0 };
static const unsigned char wl_prologue[14] = { 0x53, 0x48, 0x83, 0xec, 0x38, 0x48, 0x89, 0x3c, 0x24, 0x48, 0x89, 0x74,
                                               0x24, 0x08 };

__attribute__((visibility("hidden"))) void *appkit_trace_cont0, *appkit_trace_cont1, *appkit_trace_cont2,
    *appkit_trace_cont3;

#define THUNK(N)                                                                                                      \
    __asm__(".text\n.p2align 4\n_appkit_trace_thunk" #N ":\n"                                                          \
            "pushq %rdi\n pushq %rsi\n pushq %rdx\n pushq %rcx\n pushq %r8\n pushq %r9\n pushq %rax\n"                  \
            "movq 56(%rsp), %r9\n movl $" #N ", %r8d\n"                                                                \
            "call _appkit_trace_wl\n"                                                                                  \
            "popq %rax\n popq %r9\n popq %r8\n popq %rcx\n popq %rdx\n popq %rsi\n popq %rdi\n"                         \
            "pushq %rbx\n subq $0x38, %rsp\n movq %rdi, (%rsp)\n movq %rsi, 8(%rsp)\n"                                  \
            "jmp *_appkit_trace_cont" #N "(%rip)\n");
THUNK(0)
THUNK(1)
THUNK(2)
THUNK(3)
extern char appkit_trace_thunk0[], appkit_trace_thunk1[], appkit_trace_thunk2[], appkit_trace_thunk3[];

static int plausible(const void *p)
{
    return p && (unsigned long)p > 0x10000 && ((unsigned long)p & 7) == 0;
}

static const char *iface_name(const void *iface)
{
    return plausible(iface) && plausible(*(const void *const *)iface) ? *(const char *const *)iface : "?";
}

__attribute__((visibility("hidden"))) void appkit_trace_wl(void *proxy, unsigned long opcode, void *a3, unsigned long a4,
                                                           unsigned long kind, void *ret)
{
    const char *ifn = "?", *req = "?";
    TrDlInfo di;
    if (plausible(proxy)) {
        const void *iface = *(const void *const *)proxy;
        ifn = iface_name(iface);
        if (plausible(iface)) {
            int n = *(const int *)((const char *)iface + 12);
            const char *methods = *(const char *const *)((const char *)iface + 16);
            if (opcode < (unsigned long)n && n < 256 && plausible(methods) && plausible(*(const void *const *)(methods + 24 * opcode)))
                req = *(const char *const *)(methods + 24 * opcode);
        }
    }
    di.dli_fname = 0;
    if (!dladdr(ret, &di) || !di.dli_fname)
        di.dli_fname = "?";
    if (kind == 0)
        tr("WL-REQ %s.%s(%lu) proxy=%p via %s caller=%s+0x%lx\n", ifn, req, opcode, proxy, wl_names[kind], di.dli_fname,
           (unsigned long)((char *)ret - (char *)di.dli_fbase));
    else if (kind == 1)
        tr("WL-REQ %s.%s(%lu) proxy=%p new=%s via %s caller=%s+0x%lx\n", ifn, req, opcode, proxy, iface_name(a3),
           wl_names[kind], di.dli_fname, (unsigned long)((char *)ret - (char *)di.dli_fbase));
    else
        tr("WL-REQ %s.%s(%lu) proxy=%p new=%s ver=%lu via %s caller=%s+0x%lx\n", ifn, req, opcode, proxy,
           iface_name(a3), a4, wl_names[kind], di.dli_fname, (unsigned long)((char *)ret - (char *)di.dli_fbase));
}

static void wl_patch(const struct mach_header *mh)
{
    void *thunks[4] = { appkit_trace_thunk0, appkit_trace_thunk1, appkit_trace_thunk2, appkit_trace_thunk3 };
    void **conts[4] = { &appkit_trace_cont0, &appkit_trace_cont1, &appkit_trace_cont2, &appkit_trace_cont3 };
    int i;
    for (i = 0; i < 4; i++) {
        unsigned char *t = (unsigned char *)mh + wl_off[i];
        unsigned long page = (unsigned long)t & ~0xfffUL;
        unsigned char jmp[14] = { 0xff, 0x25, 0, 0, 0, 0 };
        if (memcmp(t, wl_prologue, 14) != 0) {
            tr("WL hook: %s prologue differs, not hooked\n", wl_names[i]);
            continue;
        }
        *conts[i] = t + 14;
        memcpy(jmp + 6, &thunks[i], 8);
        if (mprotect((void *)page, 0x2000, 7) != 0) {
            tr("WL hook: mprotect rwx failed for %s\n", wl_names[i]);
            continue;
        }
        memcpy(t, jmp, 14);
        mprotect((void *)page, 0x2000, 5);
        tr("WL hook: %s patched at %p\n", wl_names[i], (void *)t);
    }
    tr_wl_done = 1;
}

static void try_wl(const struct mach_header *mh)
{
    TrDlInfo di;
    if (tr_wl_done || !mh)
        return;
    if (dladdr(mh, &di) && di.dli_fname && strstr(di.dli_fname, "Wayland.backend/Contents/MacOS/Wayland"))
        wl_patch(mh);
}

static void on_image(const struct mach_header *mh, long slide)
{
    TrDlInfo di;
    (void)slide;
    if (!tr_win_done) {
        if (!tr_armed && dladdr(mh, &di) && di.dli_fname && strstr(di.dli_fname, "AppKit.framework/Versions/C/AppKit"))
            tr_armed = 1;
        if (tr_armed)
            try_window();
    }
    try_wl(mh);
}

__attribute__((constructor)) static void appkit_extras_trace(void)
{
    unsigned int i, n;
    const char *v = getenv("DARLING_APPKIT_TRACE");
    if (!v || !*v || (v[0] == '0' && !v[1]))
        return;
    tr("DARLING_APPKIT_TRACE=%s on\n", v);
    try_window();
    n = _dyld_image_count();
    for (i = 0; i < n; i++)
        try_wl(_dyld_get_image_header(i));
    _dyld_register_func_for_add_image(on_image);
}
