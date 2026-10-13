/*
 * objc-msgSend-imp-undoc.m — Control #162, step 40 marker probe.
 *
 * purpose:     Resolve the send-path disagreement found in Control #161
 *              (step 39): moments before the send,
 *              class_getMethodImplementation(stub, "description") reports
 *              _objc_msgForward (no method, would forward), yet the send
 *              itself returns a value (libsystem_trace+0x5eda cold probe,
 *              Foundation+0x790B0 in chrome). Control #161 measured that
 *              forwarding cannot run at all in this guest (the only
 *              handler that could run fatals, and no image installs
 *              another), so the returned value is a FOUND IMP — this probe
 *              measures whether the send writes that IMP into the class
 *              cache, whether it is stable, and whether it is
 *              selector-dependent.
 * input:       none.
 * output:      Every line carries the marker SENDUNDOC; the acceptance
 *              lines are  undoc pre=<p> ret1=<p> post=<p> stable=<yes|no>
 *              and  undoc sel=<name> ret=<p> live=<yes|no> . stdout is
 *              unbuffered so a fatal or signal cannot lose markers, and no
 *              message is ever sent to a returned value.
 * sideEffects: may populate the stub class's method cache; nothing else.
 *
 * Build: build-freebsd/build-msgSend-imp-undoc.sh
 * Run:   build-freebsd/run-msgSend-imp-undoc.sh [wait-secs]
 */
#import <objc/objc.h>
#import <objc/runtime.h>
#import <objc/message.h>
#import <Foundation/Foundation.h>
#import <dlfcn.h>
#import <string.h>
#import <stdlib.h>

extern int printf(const char *, ...);

/* Anchor symbols, printed by ADDRESS never by value. */
extern char _anchor_objc_msgSend __asm__("_objc_msgSend");
extern char _anchor_objc_msgForward __asm__("__objc_msgForward");
extern char _anchor_NSConstantDictionary __asm__("_OBJC_CLASS_$_NSConstantDictionary");
extern char _anchor_NSLog __asm__("_NSLog");

static void where(const char *tag, const void *p) {
    Dl_info info;
    int ok = dladdr(p, &info);
    printf("SENDUNDOC %s value=%p dladdr=%d fname=%s fbase=%p sname=%s saddr=%p\n",
           tag, p, ok,
           (ok && info.dli_fname) ? info.dli_fname : "(none)",
           (ok ? info.dli_fbase : NULL),
           (ok && info.dli_sname) ? info.dli_sname : "(none)",
           (ok ? info.dli_saddr : NULL));
}

/* One send on the shared fake receiver. Returns the raw value; prints it
 * with attribution. Never sends to the returned value. */
static void *send_one(const char *tag, id recv, SEL sel) {
    printf("SENDUNDOC %s before-send sel=%s\n", tag, sel_getName(sel));
    id r = ((id (*)(id, SEL))objc_msgSend)(recv, sel);
    printf("SENDUNDOC %s after-send\n", tag);
    printf("SENDUNDOC %s ret=%p\n", tag, (void *)r);
    where(tag, (void *)r);
    return (void *)r;
}

int main(void) {
    setvbuf(stdout, NULL, _IONBF, 0);

    printf("SENDUNDOC anchors objc_msgSend=%p objc_msgForward=%p CFE-NSConstantDictionary=%p NSLog=%p\n",
           (void *)&_anchor_objc_msgSend, (void *)&_anchor_objc_msgForward,
           (void *)&_anchor_NSConstantDictionary, (void *)&_anchor_NSLog);
    void *msgfwd = (void *)&_anchor_objc_msgForward;

    Class stub = objc_getClass("NSConstantDictionary");
    printf("SENDUNDOC stub-class=%p name=%s super=%s meta=%d\n",
           (void *)stub, stub ? class_getName(stub) : "(nil)",
           (stub && class_getSuperclass(stub)) ? class_getName(class_getSuperclass(stub)) : "(nil/root)",
           stub ? class_isMetaClass(stub) : -1);
    if (!stub) {
        printf("SENDUNDOC DONE rc=2 (stub class missing)\n");
        return 2;
    }

    SEL desc = sel_registerName("description");
    unsigned count = 0;
    Method *ml = class_copyMethodList(stub, &count);
    printf("SENDUNDOC stub method-list-count=%u\n", count);
    free(ml);

    /* Shared fake receiver: isa = stub class, zeros after it. */
    struct fake_t { void *isa; long z[16]; } fake;
    memset(&fake, 0, sizeof(fake));
    fake.isa = (void *)stub;

    /* THE measurement of the mechanism: the selector the send actually
     * receives. @selector(...) is a selref SLOT bound by the guest's
     * loader; sel_registerName interns through the runtime table. If the
     * two differ, the binary's selector references are bound wrong in
     * the guest, and every selref-based send in the guest (chrome's own
     * framework code) passes a garbage selector. */
    {
        SEL ref_desc = @selector(description);
        SEL reg_desc = sel_registerName("description");
        printf("SENDUNDOC selref description=%p\n", (void *)ref_desc);
        {
            Dl_info si;
            int ok = dladdr((void *)ref_desc, &si);
            printf("SENDUNDOC selref dladdr=%d fname=%s fbase=%p sname=%s saddr=%p\n",
                   ok, (ok && si.dli_fname) ? si.dli_fname : "(none)",
                   ok ? si.dli_fbase : NULL,
                   (ok && si.dli_sname) ? si.dli_sname : "(none)",
                   ok ? si.dli_saddr : NULL);
        }
        printf("SENDUNDOC regname description=%p\n", (void *)reg_desc);
        printf("SENDUNDOC selref-eq-regname=%s selref-le-0x1000=%s\n",
               (ref_desc == reg_desc) ? "yes" : "NO",
               ((unsigned long)ref_desc < 0x1000ULL) ? "YES-small" : "no");
        /* Only after the raw values are on the log: ask for the name. If
         * the selref-bound value is garbage, this dies here and the
         * markers above are already flushed. */
        printf("SENDUNDOC selref name=%s\n", sel_getName(ref_desc));
        printf("SENDUNDOC regname name=%s\n", sel_getName(reg_desc));
    }

    /* The measurement of Control #161, repeated as the pre-send state. */
    void *pre = (void *)class_getMethodImplementation(stub, desc);
    printf("SENDUNDOC pre responds=%d getimp=%p is_msgforward=%s\n",
           class_respondsToSelector(stub, desc), pre, (pre == msgfwd) ? "yes" : "no");
    where("where-pre", pre);

    /* Send #1: the case under investigation. */
    void *ret1 = send_one("send-desc-1", (id)&fake, desc);

    /* Post-send: did the send WRITE the cache? If the API now returns the
     * same pointer the send returned, the resolution produced+cached it;
     * if it still reports _objc_msgForward, the send's path is not the
     * cache the API reads. */
    void *post = (void *)class_getMethodImplementation(stub, desc);
    printf("SENDUNDOC post getimp=%p is_msgforward=%s wrote-cache=%s\n",
           post, (post == msgfwd) ? "yes" : "no",
           (post == ret1) ? "yes(equal-ret1)" : "no");
    where("where-post", post);

    /* Repeat: stable or varying? */
    void *ret2 = send_one("send-desc-2", (id)&fake, desc);
    printf("undoc pre=%p ret1=%p post=%p ret2=%p stable=%s\n",
           pre, ret1, post, ret2, (ret1 == ret2 && post == ret1) ? "yes" : "no");

    /* Selector-independence on the SAME empty root class: if other
     * selectors return values too, the miss path generates a value for
     * anything; if they fatal, "description" resolved specifically. */
    {
        const char *names[] = {"class", "hash", "length", "isEqual:", "retainCount"};
        for (unsigned i = 0; i < sizeof(names) / sizeof(names[0]); i++) {
            SEL s = sel_registerName(names[i]);
            printf("SENDUNDOC sel=%s before-send\n", names[i]);
            id r = ((id (*)(id, SEL))objc_msgSend)((id)&fake, s);
            printf("SENDUNDOC sel=%s ret=%p live=%s\n",
                   names[i], (void *)r, (r != NULL) ? "yes" : "no");
            where("where-sel", (void *)r);
        }
    }

    /* Control on a REAL object: the same send mechanics must find a real
     * description (proves the probe's send path itself is sound). */
    {
        id d = [NSDictionary dictionary];
        printf("SENDUNDOC control real-obj=%p class=%s\n", (void *)d,
               class_getName(object_getClass(d)));
        printf("SENDUNDOC control before-send\n");
        id r = ((id (*)(id, SEL))objc_msgSend)(d, desc);
        printf("SENDUNDOC control ret=%p\n", (void *)r);
    }

    printf("SENDUNDOC DONE rc=0\n");
    return 0;
}
