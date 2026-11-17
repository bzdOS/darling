/* darling-extras behaviour: class level key value coding of CATransaction.
 * Chromium calls +[CATransaction setValue:forKey:] (kCATransactionDisableActions) and reads it back. The QuartzCore stub's
 * CATransaction has only +begin, +commit and +setDisableActions:, so the call fell through to NSObject's KVC and raised
 * NSUnknownKeyException (through +setValue:forUndefinedKey:).
 *
 * Added to the CATransaction metaclass once QuartzCore is mapped (add-image retry as in AppKitExtras screen.c):
 *   +setValue:forKey:, +valueForKey:, +disableActions, +animationDuration (0.25 when unset), +setAnimationDuration:
 * and +setDisableActions: replaced, so that the flag is kept. One process wide dictionary, no transaction stack:
 * begin/commit do not scope the values. A nil value removes the key. */
typedef void *Class;
typedef void *SEL;
typedef void *IMP;
typedef void *id;
extern Class objc_getClass(const char *name);
extern Class object_getClass(id obj);
extern SEL sel_registerName(const char *name);
extern unsigned char class_addMethod(Class cls, SEL name, IMP imp, const char *types);
extern IMP class_replaceMethod(Class cls, SEL name, IMP imp, const char *types);
extern id objc_msgSend(id, SEL, ...);
extern void *dlsym(void *handle, const char *symbol);
struct mach_header;
extern void _dyld_register_func_for_add_image(void (*func)(const struct mach_header *, long));

#define RTLD_DEFAULT ((void *)-2)
#define MSG(ret, ...) ((ret (*)(__VA_ARGS__))objc_msgSend)

static volatile int lock;
static id store;

static void lock_take(void) { while (__sync_lock_test_and_set(&lock, 1)) { } }
static void lock_drop(void) { __sync_lock_release(&lock); }

static id key_disable(void)
{
    id *p = (id *)dlsym(RTLD_DEFAULT, "kCATransactionDisableActions");
    if (p && *p)
        return *p;
    return MSG(id, id, SEL, const char *)(objc_getClass("NSString"), sel_registerName("stringWithUTF8String:"), "disableActions");
}
static id key_duration(void)
{
    id *p = (id *)dlsym(RTLD_DEFAULT, "kCATransactionAnimationDuration");
    if (p && *p)
        return *p;
    return MSG(id, id, SEL, const char *)(objc_getClass("NSString"), sel_registerName("stringWithUTF8String:"), "animationDuration");
}

static void set_value(id self, SEL cmd, id value, id key)
{
    (void)self; (void)cmd;
    if (!key)
        return;
    lock_take();
    if (!store)
        store = MSG(id, id, SEL)(MSG(id, id, SEL)(objc_getClass("NSMutableDictionary"), sel_registerName("alloc")), sel_registerName("init"));
    if (value)
        MSG(void, id, SEL, id, id)(store, sel_registerName("setObject:forKey:"), value, key);
    else
        MSG(void, id, SEL, id)(store, sel_registerName("removeObjectForKey:"), key);
    lock_drop();
}
static id get_value(id self, SEL cmd, id key)
{
    id v = 0;
    (void)self; (void)cmd;
    if (!key)
        return 0;
    lock_take();
    if (store)
        v = MSG(id, id, SEL, id)(store, sel_registerName("objectForKey:"), key);
    lock_drop();
    return v;
}
static signed char get_disable(id self, SEL cmd)
{
    id v = get_value(self, cmd, key_disable());
    return v ? (signed char)MSG(signed char, id, SEL)(v, sel_registerName("boolValue")) : 0;
}
static void put_disable(id self, SEL cmd, signed char flag)
{
    id n = MSG(id, id, SEL, signed char)(objc_getClass("NSNumber"), sel_registerName("numberWithBool:"), flag);
    set_value(self, cmd, n, key_disable());
}
static double get_duration(id self, SEL cmd)
{
    id v = get_value(self, cmd, key_duration());
    return v ? MSG(double, id, SEL)(v, sel_registerName("doubleValue")) : 0.25;
}
static void put_duration(id self, SEL cmd, double d)
{
    id n = MSG(id, id, SEL, double)(objc_getClass("NSNumber"), sel_registerName("numberWithDouble:"), d);
    set_value(self, cmd, n, key_duration());
}

static int done;

static void try_add(void)
{
    Class c, meta;
    if (done)
        return;
    c = objc_getClass("CATransaction");
    if (!c)
        return;
    meta = object_getClass((id)c);
    class_addMethod(meta, sel_registerName("setValue:forKey:"), (IMP)set_value, "v32@0:8@16@24");
    class_addMethod(meta, sel_registerName("valueForKey:"), (IMP)get_value, "@24@0:8@16");
    class_addMethod(meta, sel_registerName("disableActions"), (IMP)get_disable, "c16@0:8");
    class_addMethod(meta, sel_registerName("animationDuration"), (IMP)get_duration, "d16@0:8");
    class_addMethod(meta, sel_registerName("setAnimationDuration:"), (IMP)put_duration, "v24@0:8d16");
    class_replaceMethod(meta, sel_registerName("setDisableActions:"), (IMP)put_disable, "v20@0:8c16");
    done = 1;
}

static void on_image(const struct mach_header *mh, long slide) { (void)mh; (void)slide; try_add(); }

__attribute__((constructor)) static void darling_extras_catransaction(void)
{
    try_add();
    if (!done)
        _dyld_register_func_for_add_image(on_image);
}
