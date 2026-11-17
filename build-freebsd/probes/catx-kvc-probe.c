/* catx-kvc-probe.c - which selector/receiver/image serves +[CATransaction setValue:forKey:kCATransactionDisableActions].
 * Prints one "CATX-KVC-PROOF ..." line. An uncaught exception is reported by the handler as throws=y. */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <dlfcn.h>

typedef void *id; typedef void *Class; typedef void *SEL; typedef void *Method; typedef void *IMP_t;
extern Class objc_getClass(const char *);
extern SEL sel_registerName(const char *);
extern id objc_msgSend(id, SEL, ...);
extern Method class_getClassMethod(Class, SEL);
extern Method class_getInstanceMethod(Class, SEL);
extern IMP_t method_getImplementation(Method);
extern Class object_getClass(id);
extern const char *class_getName(Class);
extern const char *class_getImageName(Class);
extern Class class_getSuperclass(Class);

static const char *cstr(id s) { return s ? (const char *)objc_msgSend(s, sel_registerName("UTF8String")) : "(null)"; }
static const char *img(void *p) { Dl_info d; if (p && dladdr(p, &d) && d.dli_fname) return d.dli_fname; return "none"; }

static int printed;
static void report(const char *throws, id exc) {
    if (printed) return; printed = 1;
    Class c = objc_getClass("CATransaction");
    SEL s = sel_registerName("setValue:forKey:");
    Method mc = class_getClassMethod(c, s), mi = class_getInstanceMethod(c, s);
    void *impc = mc ? (void *)method_getImplementation(mc) : 0;
    void *impi = mi ? (void *)method_getImplementation(mi) : 0;
    /* class_getClassMethod finds NSObject's instance-level +setValue:forKey: too, so also say who owns it */
    printf("CATX-DETAIL classmethod=%s imp=%p image=%s\n", mc ? "y" : "n", impc, img(impc));
    printf("CATX-DETAIL instancemethod=%s imp=%p image=%s\n", mi ? "y" : "n", impi, img(impi));
    const char *names[] = {"setValue:forKey:", "valueForKey:", "setValue:forUndefinedKey:", "valueForUndefinedKey:", "setDisableActions:", "disableActions", "begin", "commit"};
    for (int i = 0; i < 8; i++) {
        SEL ss = sel_registerName(names[i]);
        Method m = class_getClassMethod(c, ss); void *ip = m ? (void *)method_getImplementation(m) : 0;
        printf("CATX-DETAIL +%s imp=%p image=%s\n", names[i], ip, img(ip));
    }
    printf("CATX-DETAIL CATransaction class image=%s super=%s\n", class_getImageName(c) ? class_getImageName(c) : "none",
           class_getSuperclass(c) ? class_getName(class_getSuperclass(c)) : "none");
    if (exc) printf("CATX-DETAIL exception=%s reason=%s\n", cstr(objc_msgSend(exc, sel_registerName("name"))), cstr(objc_msgSend(exc, sel_registerName("reason"))));
    printf("CATX-KVC-PROOF sel=setValue:forKey: receiver=class method_present=%s imp_image=%s throws=%s confirmed=%s\n",
           mc ? "y" : "n", img(impc), throws, (!mc || !strstr(img(impc), "QuartzCore")) ? "y" : "n");
    fflush(stdout);
}
static void handler(id exc) { report("y", exc); _exit(0); }

int main(void) {
    setbuf(stdout, NULL);
    { void *h = dlopen("/usr/lib/darling-extras.dylib", RTLD_NOW | RTLD_GLOBAL); printf("dlopen darling-extras %s\n", h ? "ok" : dlerror()); }
    printf("linked against the overlay QuartzCore and Foundation (no dlopen)\n");
    void (*setH)(void *) = dlsym(RTLD_DEFAULT, "NSSetUncaughtExceptionHandler");
    if (setH) setH((void *)handler);
    Class c = objc_getClass("CATransaction");
    if (!c) { printf("CATX-KVC-PROOF sel=setValue:forKey: receiver=none method_present=n imp_image=none throws=n confirmed=n (no class)\n"); return 1; }
    id *kp = dlsym(RTLD_DEFAULT, "kCATransactionDisableActions");
    id key = kp ? *kp : 0;
    printf("kCATransactionDisableActions sym=%p image=%s value=%s\n", (void *)kp, img(kp), cstr(key));
    if (!key) key = objc_msgSend(objc_getClass("NSString"), sel_registerName("stringWithUTF8String:"), "disableActions");
    id yes = objc_msgSend(objc_getClass("NSNumber"), sel_registerName("numberWithBool:"), 1);
    objc_msgSend(c, sel_registerName("setValue:forKey:"), yes, key);
    id v = objc_msgSend(c, sel_registerName("valueForKey:"), key);
    printf("valueForKey -> %s\n", v ? cstr(objc_msgSend(v, sel_registerName("description"))) : "(nil)");
    report("n", 0);
    printf("PROBE-OK catx-kvc\n");
    return 0;
}
