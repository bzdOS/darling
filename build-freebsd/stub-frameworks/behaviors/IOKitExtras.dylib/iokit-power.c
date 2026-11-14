/* IOKitExtras behaviour: system power notifications on a machine without IOKit.
 *
 * Chromium's power monitor calls IORegisterForSystemPower and then adds
 * IONotificationPortGetRunLoopSource(port) to the run loop without a NULL test. The
 * plain stubs returned 0 for both, so CFRunLoopAddSource(rl, NULL, mode) faulted
 * (a read at address 8). Here the registration succeeds, the port is a dummy token,
 * and the run loop source is a real, never signalled CFRunLoopSource. */
typedef void *CFTypeRef;
extern void *dlsym(void *handle, const char *name);
/* CoreFoundation is reached by name at run time: a link time bind to it is attributed
 * by the linker to IOKit, which re-exports it only on paper (dyld: "Expected in IOKit"). */
#define RTLD_DEFAULT ((void *)-2)

static int dummy_port_token;
static CFTypeRef power_source;

void *IONotificationPortCreate(unsigned master)
{
    (void)master;
    return &dummy_port_token;
}

void IONotificationPortDestroy(void *port) { (void)port; }

CFTypeRef IONotificationPortGetRunLoopSource(void *port)
{
    (void)port;
    if (!power_source) {
        long ctx[16] = {0}; /* CFRunLoopSourceContext, version 0, no callbacks */
        CFTypeRef (*create)(CFTypeRef, long, void *) = dlsym(RTLD_DEFAULT, "CFRunLoopSourceCreate");
        power_source = create ? create(0, 0, ctx) : 0;
    }
    return power_source;
}

unsigned IORegisterForSystemPower(void *refcon, void **notifyPortRef, void *callback,
                                  unsigned *notifier)
{
    (void)refcon; (void)callback;
    if (notifyPortRef)
        *notifyPortRef = &dummy_port_token;
    if (notifier)
        *notifier = 1;
    return 1; /* a non-null io_connect_t */
}

int IODeregisterForSystemPower(unsigned *notifier)
{
    if (notifier)
        *notifier = 0;
    return 0;
}

int IOAllowPowerChange(unsigned kernelPort, long notificationID)
{
    (void)kernelPort; (void)notificationID;
    return 0;
}
