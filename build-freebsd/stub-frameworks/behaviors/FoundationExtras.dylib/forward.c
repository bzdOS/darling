/* FoundationExtras behaviour: a forward handler that gives a message to an unknown selector a defined result.
 *
 * CoreFoundation installs _CF_forwarding_prep_0 as the forward handler. In this tree that function is
 * `push %rbp; mov %rsp,%rbp; pop %rbp; ret`: it neither forwards nor raises, so a message to a selector the
 * receiver does not implement returns whatever happens to be in RAX. Measured: Chromium's
 * [NSApp effectiveAppearance] (no such method in this AppKit) came back as a heap pointer, objc_retain'ed
 * and then used as a receiver: "Attempt to use unknown class 0x...", abort. Every missing API is such a
 * time bomb, so the class of fault is closed here, once, instead of one selector at a time.
 *
 * The handler installed here, for the normal (register return) path:
 *   1. asks the receiver for forwardingTargetForSelector: when its class implements it, and if that gives
 *      another object, re-sends the message to it with the original arguments (tail jump);
 *   2. otherwise returns 0 in RAX/RDX/XMM0/XMM1, i.e. nil / NO / 0 / 0.0, the answer of a message to nil;
 *      the first time a (class, selector) pair is seen one line "OBJC-FWD-MISS -[Class sel]" goes to
 *      stderr, so the list of missing API is read from a run's log. With OBJC_FWD_FATAL=1 in the
 *      environment it calls doesNotRecognizeSelector: instead (the stock behaviour).
 * forwardInvocation: is not run (no NSInvocation here); a class that relies on it gets the nil answer, as
 * it got garbage before. The struct-return handler hands the caller's return buffer back (RAX = RDI).
 * It is installed from a constructor, after CoreFoundation's own initialiser (this image depends on it),
 * with objc_setForwardHandler. */
#include "../objc-proto.h"
extern Class object_getClass(id obj);
extern const char *class_getName(Class c);
extern unsigned char class_isMetaClass(Class c);
extern unsigned char class_respondsToSelector(Class c, SEL s);
extern const char *sel_getName(SEL s);
extern void objc_setForwardHandler(void *fwd, void *fwd_stret);
extern char *getenv(const char *);
extern int snprintf(char *, unsigned long, const char *, ...);
extern long write(int, const void *, unsigned long);
extern unsigned long strlen(const char *);

extern char ext_fwd_handler[] __asm__("_ext_fwd_handler");
extern char ext_fwd_handler_stret[] __asm__("_ext_fwd_handler_stret");

__asm__(
    ".text\n"
    ".p2align 4\n"
    "_ext_fwd_handler:\n"
    "    pushq %rbp\n"
    "    movq %rsp, %rbp\n"
    "    subq $192, %rsp\n"
    "    movq %rdi, 128(%rsp)\n"
    "    movq %rsi, 136(%rsp)\n"
    "    movq %rdx, 144(%rsp)\n"
    "    movq %rcx, 152(%rsp)\n"
    "    movq %r8, 160(%rsp)\n"
    "    movq %r9, 168(%rsp)\n"
    "    movq %rax, 176(%rsp)\n"
    "    movdqu %xmm0, 0(%rsp)\n"
    "    movdqu %xmm1, 16(%rsp)\n"
    "    movdqu %xmm2, 32(%rsp)\n"
    "    movdqu %xmm3, 48(%rsp)\n"
    "    movdqu %xmm4, 64(%rsp)\n"
    "    movdqu %xmm5, 80(%rsp)\n"
    "    movdqu %xmm6, 96(%rsp)\n"
    "    movdqu %xmm7, 112(%rsp)\n"
    "    call _ext_fwd_resolve\n"
    "    movq %rax, %r11\n"
    "    movq 128(%rsp), %rdi\n"
    "    movq 136(%rsp), %rsi\n"
    "    movq 144(%rsp), %rdx\n"
    "    movq 152(%rsp), %rcx\n"
    "    movq 160(%rsp), %r8\n"
    "    movq 168(%rsp), %r9\n"
    "    movdqu 0(%rsp), %xmm0\n"
    "    movdqu 16(%rsp), %xmm1\n"
    "    movdqu 32(%rsp), %xmm2\n"
    "    movdqu 48(%rsp), %xmm3\n"
    "    movdqu 64(%rsp), %xmm4\n"
    "    movdqu 80(%rsp), %xmm5\n"
    "    movdqu 96(%rsp), %xmm6\n"
    "    movdqu 112(%rsp), %xmm7\n"
    "    testq %r11, %r11\n"
    "    jz 1f\n"
    "    movq 176(%rsp), %rax\n"
    "    movq %r11, %rdi\n"
    "    leave\n"
    "    jmp _objc_msgSend\n"
    "1:  xorl %eax, %eax\n"
    "    xorl %edx, %edx\n"
    "    xorps %xmm0, %xmm0\n"
    "    xorps %xmm1, %xmm1\n"
    "    leave\n"
    "    ret\n"
    ".p2align 4\n"
    "_ext_fwd_handler_stret:\n"
    "    movq %rdi, %rax\n"
    "    ret\n");

#define SEEN_MAX 1024
static struct { Class cls; SEL sel; } seen[SEEN_MAX];
static unsigned seen_n;

static int first_time(Class cls, SEL sel)
{
    unsigned i, n = __atomic_load_n(&seen_n, __ATOMIC_ACQUIRE);
    if (n > SEEN_MAX)
        n = SEEN_MAX;
    for (i = 0; i < n; i++)
        if (seen[i].cls == cls && seen[i].sel == sel)
            return 0;
    i = __atomic_fetch_add(&seen_n, 1, __ATOMIC_ACQ_REL);
    if (i >= SEEN_MAX)
        return 1;
    seen[i].cls = cls;
    seen[i].sel = sel;
    return 1;
}

__attribute__((visibility("hidden"), used)) id ext_fwd_resolve(id self, SEL sel)
{
    Class cls = object_getClass(self);
    SEL fts = sel_registerName("forwardingTargetForSelector:");
    if (class_respondsToSelector(cls, fts)) {
        id t = objc_msgSend(self, fts, sel);
        if (t && t != self)
            return t;
    }
    if (first_time(cls, sel)) {
        char buf[320];
        int meta = class_isMetaClass(cls);
        int n = snprintf(buf, sizeof buf, "OBJC-FWD-MISS %c[%s %s]\n", meta ? '+' : '-',
                         meta ? class_getName(self) : class_getName(cls), sel_getName(sel));
        if (n > 0)
            write(2, buf, (unsigned long)(n < (int)sizeof buf ? n : (int)sizeof buf - 1));
    }
    {
        const char *e = getenv("OBJC_FWD_FATAL");
        if (e && e[0] == '1')
            objc_msgSend(self, sel_registerName("doesNotRecognizeSelector:"), sel);
    }
    return 0;
}

__attribute__((constructor)) static void foundation_extras_forward(void)
{
    objc_setForwardHandler(ext_fwd_handler, ext_fwd_handler_stret);
}
