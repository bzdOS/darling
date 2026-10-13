/*
 * objc-got-slot-undoc.m — Control #163, step 41 marker probe.
 *
 * purpose:     Close Control #162's two open reads, at runtime:
 *              (1) which symbol does the libobjc __got slot at vmaddr
 *                  0x59070 hold — the constant lookUpImpOrForward's tail
 *                  compares the found IMP against (0x2d64d/0x2d6c5 both
 *                  resolve there; __got spans 0x59008..0x590e8, so the
 *                  slot is a bound imported symbol, not the stock
 *                  __objc_msgForward_impcache TEXT gadget at 0x48e4c);
 *              (2) what IMP does the guest's lookup return for the empty
 *                  stub class + description — i.e. what __objc_msgSend_uncached
 *                  jumps to. Called directly with the same registers the
 *                  send path uses: (rdi=obj, rsi=sel, rdx=class, ecx=3).
 * input:       none.
 * output:      Every line carries the marker GOTSLOT; the acceptance lines
 *              are  gotslot slot=<p> sel=<name>  and  lookup ret=<p> sel=<name>.
 *              stdout unbuffered; no message is ever sent to a returned value.
 * sideEffects: one direct lookup call (same locks the send path takes).
 *
 * Build: build-freebsd/build-got-slot-undoc.sh
 * Run:   build-freebsd/run-got-slot-undoc.sh [wait-secs]
 */
#import <objc/objc.h>
#import <objc/runtime.h>
#import <objc/message.h>
#import <Foundation/Foundation.h>
#import <dlfcn.h>
#import <string.h>
#import <stdlib.h>

extern int printf(const char *, ...);

/* Anchor symbols, printed by ADDRESS never by value.
 * File offsets in the STAGED overlay image (from step 40's offline reads):
 *   _objc_msgSend 0x480c0 · _lookUpImpOrForward 0x2d4a0 */
extern char _anchor_objc_msgSend __asm__("_objc_msgSend");
extern char _anchor_lookUp __asm__("_lookUpImpOrForward");

/* The guest's lookup, called with the same register layout
 * __objc_msgSend_uncached uses (rdi=obj, rsi=sel, rdx=class, ecx=3). */
typedef void *(*lookup_fn)(id obj, SEL sel, Class cls, long behavior);

static void where(const char *tag, const void *p) {
    Dl_info info;
    int ok = dladdr(p, &info);
    printf("GOTSLOT %s value=%p dladdr=%d fname=%s fbase=%p sname=%s saddr=%p\n",
           tag, p, ok,
           (ok && info.dli_fname) ? info.dli_fname : "(none)",
           (ok ? info.dli_fbase : NULL),
           (ok && info.dli_sname) ? info.dli_sname : "(none)",
           (ok ? info.dli_saddr : NULL));
}

int main(void) {
    setvbuf(stdout, NULL, _IONBF, 0);

    printf("GOTSLOT anchors objc_msgSend=%p lookUpImpOrForward=%p\n",
           (void *)&_anchor_objc_msgSend, (void *)&_anchor_lookUp);

    /* libobjc's image base from the anchor (file offset 0x480c0). */
    char *objc_base = (char *)&_anchor_objc_msgSend - 0x480c0;
    printf("GOTSLOT libobjc base=%p (anchor - 0x480c0)\n", (void *)objc_base);
    where("where-anchor", &_anchor_objc_msgSend);

    /* (1) THE got slot's runtime value: libobjc vmaddr 0x59070. */
    {
        void **slot = (void **)(objc_base + 0x59070);
        void *v = *slot;
        printf("GOTSLOT slot-addr=%p value=%p\n", (void *)slot, v);
        where("where-gotslot", v);
        printf("gotslot slot=%p\n", v);
    }

    /* The stock sentinel, for comparison: __objc_msgForward_impcache (0x48e4c). */
    {
        void *impcache = (char *)&_anchor_objc_msgSend + (0x48e4c - 0x480c0);
        printf("GOTSLOT impcache-gadget=%p equals-slot=%s\n",
               impcache, (impcache == *(void **)(objc_base + 0x59070)) ? "yes" : "no");
    }

    Class stub = objc_getClass("NSConstantDictionary");
    if (!stub) {
        printf("GOTSLOT DONE rc=2 (stub class missing)\n");
        return 2;
    }
    printf("GOTSLOT stub-class=%p name=%s\n", (void *)stub, class_getName(stub));

    /* The stub class's single inline bucket, as the fast path reads it:
     * bucket pointer at class+0x10, bucket layout {sel, imp}, imp stored
     * XOR the class (the fast path does xorq %r10,%r11 before jmp *%r11). */
    {
        char *cls = (char *)stub;
        void **bucket = *(void ***)(cls + 0x10);
        void *sel_slot = bucket ? (void *)bucket[0] : NULL;
        void *imp_stored = bucket ? (void *)bucket[1] : NULL;
        printf("GOTSLOT bucket=%p sel-slot=%p imp-stored=%p decoded-imp=%p\n",
               (void *)bucket, sel_slot, imp_stored,
               (void *)((uintptr_t)imp_stored ^ (uintptr_t)stub));
    }

    SEL desc = sel_registerName("description");
    struct fake_t { void *isa; long z[16]; } fake;
    memset(&fake, 0, sizeof(fake));
    fake.isa = (void *)stub;

    /* (2) THE lookup, called exactly as the send path calls it. */
    lookup_fn lookup = (lookup_fn)&_anchor_lookUp;
    void *imp = lookup((id)&fake, desc, stub, 3);
    printf("GOTSLOT lookup ret=%p\n", imp);
    where("where-lookup", imp);
    printf("lookup ret=%p\n", imp);

    /* Side-effect check: does the direct lookup (re)write what the API sees? */
    {
        void *post = (void *)class_getMethodImplementation(stub, desc);
        printf("GOTSLOT post getimp=%p equals-lookup=%s\n",
               post, (post == imp) ? "yes" : "no");
        where("where-post", post);
    }

    /* And what the send itself still returns, for the record. */
    {
        printf("GOTSLOT before-send sel=description\n");
        id r = ((id (*)(id, SEL))objc_msgSend)((id)&fake, desc);
        printf("GOTSLOT after-send\n");
        printf("GOTSLOT send ret=%p\n", (void *)r);
    }

    /* The last link: __objc_msgForward jumps to *(_objc_forward_handler slot).
     * The slot is the data symbol __objc_forward_handler at libobjc vmaddr
     * 0x5c258 (nm: D). Reading it at runtime names the handler that actually
     * runs — the one whose return the send hands back. */
    {
        void **hslot = (void **)(objc_base + 0x5c258);
        void *h = *hslot;
        printf("GOTSLOT handler-slot=%p value=%p\n", (void *)hslot, h);
        where("where-handler", h);
        /* and the empty-bucket sentinel the class's cache points at */
        void **bucket0 = *(void ***)((char *)stub + 0x10);
        printf("GOTSLOT cache-buckets=%p first-sel=%p first-imp=%p\n",
               (void *)bucket0,
               bucket0 ? (void *)bucket0[0] : NULL,
               bucket0 ? (void *)bucket0[1] : NULL);
    }

    printf("GOTSLOT DONE rc=0\n");
    return 0;
}
