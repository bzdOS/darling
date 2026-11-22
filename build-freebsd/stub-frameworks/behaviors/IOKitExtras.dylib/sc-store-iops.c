/* IOKitExtras behaviour: the SystemConfiguration dynamic store and IOPowerSources entry points
 * that Chrome binds from THIS image (the Chrome framework imports _SCDynamicStoreCreate,
 * _SCDynamicStoreSetNotificationKeys, _SCDynamicStoreCreateRunLoopSource and the _IOPS*
 * functions with lib ordinal IOKitExtras, not SystemConfigurationExtras or suppl), so the
 * valid-object answers have to live here. Without configd/powerd there is no store: the
 * store is an empty CFDictionary (a real, releasable CF object), notification keys are
 * accepted, run loop sources are inert version-0 sources that never fire, and the machine
 * has no battery (empty snapshot, empty list, empty description).
 * CoreFoundation is reached by name at run time (see iokit-power.c). */
typedef void *CFTypeRef;
extern void *dlsym(void *handle, const char *name);
#define RTLD_DEFAULT ((void *)-2)

static CFTypeRef empty_dict(void)
{
    CFTypeRef (*create)(CFTypeRef, const void **, const void **, long, const void *, const void *) =
        dlsym(RTLD_DEFAULT, "CFDictionaryCreate");
    const void *kcb = dlsym(RTLD_DEFAULT, "kCFTypeDictionaryKeyCallBacks");
    const void *vcb = dlsym(RTLD_DEFAULT, "kCFTypeDictionaryValueCallBacks");
    return create ? create(0, 0, 0, 0, kcb, vcb) : 0;
}

static CFTypeRef inert_source(long order)
{
    long ctx[16] = {0}; /* CFRunLoopSourceContext, version 0, no callbacks */
    CFTypeRef (*create)(CFTypeRef, long, void *) = dlsym(RTLD_DEFAULT, "CFRunLoopSourceCreate");
    return create ? create(0, order, ctx) : 0;
}

CFTypeRef SCDynamicStoreCreate(CFTypeRef alloc, CFTypeRef name, void *callout, void *context)
{
    (void)alloc; (void)name; (void)callout; (void)context;
    return empty_dict();
}

unsigned char SCDynamicStoreSetNotificationKeys(CFTypeRef store, CFTypeRef keys, CFTypeRef patterns)
{
    (void)store; (void)keys; (void)patterns;
    return 1;
}

CFTypeRef SCDynamicStoreCreateRunLoopSource(CFTypeRef alloc, CFTypeRef store, long order)
{
    (void)alloc; (void)store;
    return inert_source(order);
}

CFTypeRef IOPSCopyPowerSourcesInfo(void) { return empty_dict(); }

CFTypeRef IOPSCopyPowerSourcesList(CFTypeRef blob)
{
    CFTypeRef (*create)(CFTypeRef, const void **, long, const void *) = dlsym(RTLD_DEFAULT, "CFArrayCreate");
    const void *cb = dlsym(RTLD_DEFAULT, "kCFTypeArrayCallBacks");
    (void)blob;
    return create ? create(0, 0, 0, cb) : 0;
}

/* Get rule: no source is ever listed, so any ps is unknown; an unretained empty description is returned. */
CFTypeRef IOPSGetPowerSourceDescription(CFTypeRef blob, CFTypeRef ps)
{
    static CFTypeRef empty;
    (void)blob; (void)ps;
    if (!empty)
        empty = empty_dict();
    return empty;
}

CFTypeRef IOPSNotificationCreateRunLoopSource(void (*callback)(void *), void *context)
{
    (void)callback; (void)context;
    return inert_source(0);
}
