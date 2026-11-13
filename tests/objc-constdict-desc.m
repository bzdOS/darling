/*
 * objc-constdict-desc.m — Control #160, step 38 marker probe.
 *
 * purpose:     Measure which lookUpImpOrForward branch (step 37, §2) really
 *              fires when `description` is sent to an NSConstantDictionary
 *              instance under the guest runtime — the class chrome's fatal
 *              msgSend bound to (CoreFoundationExtras, an empty stub root
 *              class: flags 0x2, super 0, baseMethods 0, step 36).
 * input:       argv[1] == "stamp" installs this probe's own
 *              objc_setForwardHandler before the sends, to prove which
 *              forward handler the runtime consults; the default run (no
 *              argument) measures the pristine guest behaviour.
 * output:      Every line carries the marker CONSTDICT, plus the acceptance
 *              line  desc-imp=<value> foundation_790b0=<expected> match=<yes|no>
 *              which compares the returned pointer against
 *              &__NSCFCopyDescription2 (Foundation+0x790B0) — the value
 *              chrome's run returned from the same message. The last line
 *              of a clean run is CONSTDICT DONE rc=0. Pointers are printed
 *              raw (%p) and no method is ever sent to a returned value, so
 *              a code pointer cannot crash the probe before it prints.
 * sideEffects: None.
 *
 * ASLR anchors: runtime address minus the symbol's file offset in the
 * staged overlay image gives that image's load base:
 *   Foundation:        _NSLog (0x790f0) — __NSCFCopyDescription2 is 0x790b0,
 *                      so expected = &_NSLog - 0x40
 *   CoreFoundation:    _CFStringGetLength (0xef8b0)
 *   libobjc:           _objc_msgSend (0x480c0) — __objc_msgForward is 0x48e80
 *   CoreFoundationExtras: _OBJC_CLASS_$_NSConstantDictionary (0x2050)
 *   libSystem:         _close (0xf706, defined in libSystem.B.dylib)
 *
 * Build: build-freebsd/build-constdict-desc-probe.sh (same recipe as
 * build-freebsd/build-real-macho-tests.sh: clang -target
 * x86_64-apple-macos10.12 + ld64.lld against the staged overlay).
 * Run:   build-freebsd/run-constdict-desc-probe.sh
 */
#import <objc/objc.h>
#import <objc/runtime.h>
#import <objc/message.h>
#import <Foundation/Foundation.h>
#import <dlfcn.h>
#import <string.h>

extern int printf(const char *, ...);

/* Installed when argv[1] == "stamp", before any send: if the guest runtime
 * really routes an unresolvable selector through the forward handler, this
 * prints and the stamp proves the path; if it never prints, the send
 * returned by some other route entirely. objc_setForwardHandler's real
 * prototype lives in objc/runtime.h and takes the stret handler too. */
static void stamp_handler(id self, SEL _cmd) {
    printf("CONSTDICT forward-handler-called self=%p cmd=%s\n",
           (void *)self, sel_getName(_cmd));
}

/* Anchor symbols, declared as chars and printed by ADDRESS: passing the
 * symbol itself would read its first 8 bytes as the pointer value. The
 * asm label spelling is needed because $ is not a C identifier character. */
extern char _anchor_NSLog __asm__("_NSLog");
extern char _anchor_CFStringGetLength __asm__("_CFStringGetLength");
extern char _anchor_objc_msgSend __asm__("_objc_msgSend");
extern char _anchor_NSConstantDictionary __asm__("_OBJC_CLASS_$_NSConstantDictionary");
extern char _anchor_close __asm__("_close");
extern char _anchor_supl __asm__("_memset_pattern16");

/* A function used only as a code pointer with a real x86-64 prologue
 * (55 48 89 e5) for the variant-B fake receiver (step 34's corrupted isa). */
static void prologue_source(void) { }

static void where(const char *tag, const void *p) {
    Dl_info info;
    int ok = dladdr(p, &info);
    printf("CONSTDICT %s value=%p dladdr=%d fname=%s fbase=%p sname=%s saddr=%p\n",
           tag, p, ok,
           (ok && info.dli_fname) ? info.dli_fname : "(none)",
           (ok ? info.dli_fbase : NULL),
           (ok && info.dli_sname) ? info.dli_sname : "(none)",
           (ok ? info.dli_saddr : NULL));
}

/* The core measurement: hand-built fake object (isa = class, zeros after),
 * one `description` message, the raw return and the comparison against
 * chrome's measured outcome. The `copy` send after it reproduces the exact
 * shape of chrome's death (Control #160): chrome died sending `copy` to the
 * code pointer that `description` returned. With the stub's own real methods
 * (Control #167) both returns are the receiver itself. */
static void probe(const char *tag, void *isa) {
    struct { void *isa; long z[8]; } fake;
    memset(&fake, 0, sizeof(fake));
    fake.isa = isa;

    void *expected = (void *)((char *)&_anchor_NSLog - 0x40); /* Foundation+0x790b0 */
    printf("CONSTDICT %s isa=%p class=%s receiver=%p expected_foundation_790b0=%p\n",
           tag, isa, isa ? class_getName((Class)isa) : "(nil)", (void *)&fake, expected);

    printf("CONSTDICT %s before-send\n", tag);
    id r = ((id (*)(id, SEL))objc_msgSend)((id)&fake, sel_registerName("description"));
    printf("CONSTDICT %s after-send\n", tag);
    printf("desc-imp=%p foundation_790b0=%p match=%s\n",
           (void *)r, expected, (r == (id)expected) ? "yes" : "no");
    printf("CONSTDICT %s description=%p same-as-receiver=%s\n",
           tag, (void *)r, (r == (id)&fake) ? "yes" : "no");
    where(tag, r);

    printf("CONSTDICT %s before-copy-send\n", tag);
    id r2 = ((id (*)(id, SEL))objc_msgSend)(r, sel_registerName("copy"));
    printf("CONSTDICT %s after-copy-send\n", tag);
    printf("CONSTDICT %s copy-ret=%p same-as-receiver=%s\n",
           tag, (void *)r2, (r2 == (id)&fake) ? "yes" : "no");
}

/* Step 38: is +resolveInstanceMethod: the owner of the description resolve
 * on the REAL class? Measured on NSConstantDictionary itself (the class
 * chrome's object carried, step 36: flags 0x2, super 0, baseMethods 0). The
 * stub's metaclass has no method list and no superclass, so a resolve step
 * there would have nothing to find; planting a logger at
 * +resolveInstanceMethod: on that metaclass gives the step a name if it ever
 * runs. The method does not exist there (baseMethods 0), so the logger is
 * added rather than replaced — and the log says which happened. */
static int real_resolve_calls;

static BOOL real_resolve_logger(Class self, SEL _cmd, SEL sel) {
    real_resolve_calls++;
    printf("CONSTDICT real-resolve-fired n=%d class=%s sel=%s\n",
           real_resolve_calls,
           self ? class_getName(self) : "(nil)",
           sel ? sel_getName(sel) : "(nil)");
    return NO;
}

static void real_class_measure(const char *tag, Class real) {
    SEL desc = sel_registerName("description");
    SEL resolve = sel_registerName("resolveInstanceMethod:");
    Class meta = object_getClass((id)real);

    printf("CONSTDICT %s class=%p name=%s meta=%p meta-name=%s\n",
           tag, (void *)real, class_getName(real),
           (void *)meta, meta ? class_getName(meta) : "(nil)");

    unsigned cc = 0, mc = 0;
    Method *cm = class_copyMethodList(real, &cc);
    Method *mm = class_copyMethodList(meta, &mc);
    printf("CONSTDICT %s method-list class=%u meta=%u\n", tag, cc, mc);
    if (cm) free(cm);
    if (mm) free(mm);

    IMP pre = class_getMethodImplementation(real, desc);
    printf("CONSTDICT %s pre-hook getMethodImplementation(description)=%p responds=%d\n",
           tag, (void *)pre, class_respondsToSelector(real, desc));
    where("where-real-class-pre-hook-getimp", (void *)pre);

    Method rm = class_getInstanceMethod(meta, resolve);
    if (rm) {
        IMP old = method_setImplementation(rm, (IMP)real_resolve_logger);
        printf("CONSTDICT %s resolve-hook=setImplementation old=%p\n",
               tag, (void *)old);
    } else {
        BOOL added = class_addMethod(meta, resolve, (IMP)real_resolve_logger, "c@::");
        printf("CONSTDICT %s resolve-hook=addMethod added=%d meta-responds-now=%d\n",
               tag, added, class_respondsToSelector(meta, resolve));
    }

    IMP post = class_getMethodImplementation(real, desc);
    printf("CONSTDICT %s post-hook getMethodImplementation(description)=%p\n",
           tag, (void *)post);
    where("where-real-class-post-hook-getimp", (void *)post);
    printf("CONSTDICT %s resolve-fired-after-hook=%d\n", tag, real_resolve_calls);
}

/* Control: a REAL object (no fake) — the send must return a real
 * description object; this proves the probe's send path itself is sound. */
static void control(const char *tag, id obj) {
    void *expected = (void *)((char *)&_anchor_NSLog - 0x40);
    printf("CONSTDICT %s real-obj=%p class=%s\n", tag, (void *)obj,
           class_getName(object_getClass(obj)));
    printf("CONSTDICT %s before-send\n", tag);
    id r = ((id (*)(id, SEL))objc_msgSend)(obj, sel_registerName("description"));
    printf("CONSTDICT %s after-send\n", tag);
    printf("CONSTDICT %s description=%p (expected_foundation_790b0=%p match=%s)\n",
           tag, (void *)r, expected, (r == (id)expected) ? "yes" : "no");
}

int main(int argc, char **argv) {
    /* Unbuffered: a FATAL signal mid-run must not lose the markers — the
     * first run crashed inside a send and every printf was lost to the
     * stdio buffer. */
    setvbuf(stdout, NULL, _IONBF, 0);
    int stamp = (argc > 1 && strcmp(argv[1], "stamp") == 0);
    if (stamp) {
        objc_setForwardHandler((void *)stamp_handler, NULL);
    }
    @autoreleasepool {
        where("where-supl-text", (void *)&_anchor_supl);
        printf("CONSTDICT anchors NSLog=%p CFStringGetLength=%p objc_msgSend=%p CFE-NSConstantDictionary=%p close=%p\n",
               (void *)&_anchor_NSLog, (void *)&_anchor_CFStringGetLength,
               (void *)&_anchor_objc_msgSend, (void *)&_anchor_NSConstantDictionary,
               (void *)&_anchor_close);

        /* Control 1: the empty dictionary singleton (Foundation's own path). */
        control("empty-dict", [NSDictionary dictionary]);

        /* Control 2: a placeholder instance. */
        control("placeholder", [[NSDictionary alloc] init]);

        /* The case under investigation: the CoreFoundationExtras stub class
         * as-is in the current runtime (may have methods), plus an explicitly
         * EMPTY root class (method-list-count=0, like step 36) to emulate the
         * chrome scenario without touching overlay/staging. */
        Class real_stub = objc_getClass("NSConstantDictionary");
        printf("CONSTDICT real-stub-class=%p name=%s\n", (void *)real_stub,
               real_stub ? class_getName(real_stub) : "(nil)");
        if (real_stub) {
            unsigned count = 0;
            Method *ml = class_copyMethodList(real_stub, &count);
            printf("CONSTDICT real-stub method-list-count=%u\n", count);
            for (unsigned i = 0; i < count; i++) {
                printf("CONSTDICT real-stub method[%u]=%s imp=%p\n", i,
                       sel_getName(method_getName(ml[i])),
                       (void *)method_getImplementation(ml[i]));
            }
            if (ml) free(ml);
            SEL desc = sel_registerName("description");
            printf("CONSTDICT real-stub responds(description)=%d getMethodImplementation=%p\n",
                   class_respondsToSelector(real_stub, desc),
                   (void *)class_getMethodImplementation(real_stub, desc));
            where("where-real-stub-getimp", (void *)class_getMethodImplementation(real_stub, desc));
        }

        /* Step 38: the resolve owner on the real class. Everything that can
         * be read before a send is read before it: a hit on the armed
         * Foundation+0x790b0 trap is fatal (resume=0), so the send goes last
         * and whatever it does not reach is already in the log. */
        if (real_stub) {
            real_class_measure("real-class", real_stub);
            printf("CONSTDICT real-class resolve-fired-before-send=%d\n",
                   real_resolve_calls);
            probe("real-class", (void *)real_stub);
        } else {
            printf("CONSTDICT real-class absent: objc_getClass(NSConstantDictionary)=nil\n");
        }

        /* Empty root class: no superclass, no methods added (step-36 shape). */
        Class empty = objc_allocateClassPair(NULL, "EmptyConstDictProbe", 0);
        if (empty) {
            objc_registerClassPair(empty);
        }
        printf("CONSTDICT empty-stub-class=%p name=%s\n", (void *)empty,
               empty ? class_getName(empty) : "(nil)");
        if (empty) {
            unsigned count = 0;
            Method *ml = class_copyMethodList(empty, &count);
            printf("CONSTDICT empty-stub method-list-count=%u\n", count);
            for (unsigned i = 0; i < count; i++) {
                printf("CONSTDICT empty-stub method[%u]=%s imp=%p\n", i,
                       sel_getName(method_getName(ml[i])),
                       (void *)method_getImplementation(ml[i]));
            }
            if (ml) free(ml);
            SEL desc = sel_registerName("description");
            printf("CONSTDICT empty-stub responds(description)=%d getMethodImplementation=%p\n",
                   class_respondsToSelector(empty, desc),
                   (void *)class_getMethodImplementation(empty, desc));
            where("where-empty-stub-getimp", (void *)class_getMethodImplementation(empty, desc));

            probe("empty-stub", empty);
        }

        /* Variant B: a receiver whose isa is a code pointer with a real
         * prologue — the corrupted-receiver shape of step 34. Expected to
         * die inside the lookup; the markers above are already printed. */
        unsigned char prologue[4];
        memcpy(prologue, (void *)&prologue_source, 4);
        printf("CONSTDICT variant-b code-isa=%p prologue=%02x %02x %02x %02x\n",
               (void *)&prologue_source,
               prologue[0], prologue[1], prologue[2], prologue[3]);
        probe("variant-b", (void *)&prologue_source);
    }
    printf("CONSTDICT DONE rc=0\n");
    return 0;
}
