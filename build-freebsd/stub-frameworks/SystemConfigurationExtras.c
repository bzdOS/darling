/* SystemConfigurationExtras.dylib: the SystemConfiguration entry points and constants Chrome
 * uses at startup, answering from the local host instead of configd. Freestanding (no SDK
 * headers): CF types are opaque pointers. Nothing here returns NULL where a caller
 * dereferences the result: proxies is a non-empty dictionary, key builders return strings,
 * reachability returns an object that reports "reachable", the interface list is an empty
 * array. Built by build-sc-stubs.sh; checked by sc-stubs-check.sh. */
typedef const void *CFTypeRef, *CFStringRef, *CFDictionaryRef, *CFArrayRef, *CFAllocatorRef;
typedef unsigned char Boolean;
typedef long CFIndex;
typedef unsigned int CFStringEncoding;
#define kCFAllocatorDefault ((CFAllocatorRef)0) /* NULL selects the default allocator */
extern const char kCFTypeDictionaryKeyCallBacks[], kCFTypeDictionaryValueCallBacks[], kCFTypeArrayCallBacks[];
extern CFStringRef CFStringCreateWithCString(CFAllocatorRef, const char *, CFStringEncoding);
extern CFStringRef CFStringCreateWithFormat(CFAllocatorRef, CFDictionaryRef, CFStringRef, ...);
extern CFDictionaryRef CFDictionaryCreate(CFAllocatorRef, const void **, const void **, CFIndex, const void *, const void *);
extern CFArrayRef CFArrayCreate(CFAllocatorRef, const void **, CFIndex, const void *);
extern CFTypeRef CFNumberCreate(CFAllocatorRef, int, const void *);
extern void CFRelease(CFTypeRef);
extern int gethostname(char *, unsigned long);

#define K(sym, str) const CFStringRef sym = __builtin___CFStringMakeConstantString(str)
K(kSCDynamicStoreDomainState, "State:");
K(kSCDynamicStorePropNetPrimaryInterface, "PrimaryInterface");
K(kSCEntNetIPv4, "IPv4");
K(kSCEntNetIPv6, "IPv6");
K(kSCEntNetInterface, "Interface");
K(kSCPropNetProxiesExceptionsList, "ExceptionsList");
K(kSCPropNetProxiesExcludeSimpleHostnames, "ExcludeSimpleHostnames");
K(kSCPropNetProxiesFTPEnable, "FTPEnable");
K(kSCPropNetProxiesFTPPort, "FTPPort");
K(kSCPropNetProxiesFTPProxy, "FTPProxy");
K(kSCPropNetProxiesHTTPEnable, "HTTPEnable");
K(kSCPropNetProxiesHTTPPort, "HTTPPort");
K(kSCPropNetProxiesHTTPProxy, "HTTPProxy");
K(kSCPropNetProxiesHTTPSEnable, "HTTPSEnable");
K(kSCPropNetProxiesHTTPSPort, "HTTPSPort");
K(kSCPropNetProxiesHTTPSProxy, "HTTPSProxy");
K(kSCPropNetProxiesProxyAutoConfigEnable, "ProxyAutoConfigEnable");
K(kSCPropNetProxiesProxyAutoConfigURLString, "ProxyAutoConfigURLString");
K(kSCPropNetProxiesProxyAutoDiscoveryEnable, "ProxyAutoDiscoveryEnable");
K(kSCPropNetProxiesSOCKSEnable, "SOCKSEnable");
K(kSCPropNetProxiesSOCKSPort, "SOCKSPort");
K(kSCPropNetProxiesSOCKSProxy, "SOCKSProxy");

CFDictionaryRef SCDynamicStoreCopyProxies(CFTypeRef store)
{
	const void *keys[4] = { kSCPropNetProxiesHTTPEnable, kSCPropNetProxiesHTTPSEnable,
		kSCPropNetProxiesProxyAutoConfigEnable, kSCPropNetProxiesProxyAutoDiscoveryEnable };
	int zero = 0;
	CFTypeRef n = CFNumberCreate(kCFAllocatorDefault, 3 /* kCFNumberSInt32Type */, &zero);
	const void *vals[4] = { n, n, n, n };
	CFDictionaryRef d = CFDictionaryCreate(kCFAllocatorDefault, keys, vals, 4,
		kCFTypeDictionaryKeyCallBacks, kCFTypeDictionaryValueCallBacks);
	CFRelease(n);
	return d;
}

static CFStringRef host_name(void)
{
	char h[256] = "localhost";
	gethostname(h, sizeof h - 1);
	h[sizeof h - 1] = 0;
	return CFStringCreateWithCString(kCFAllocatorDefault, h, 0x08000100 /* UTF8 */);
}
CFStringRef SCDynamicStoreCopyComputerName(CFTypeRef store, CFStringEncoding *enc)
{
	if (enc) *enc = 0x08000100;
	return host_name();
}
CFStringRef SCDynamicStoreCopyLocalHostName(CFTypeRef store) { return host_name(); }

CFStringRef SCDynamicStoreKeyCreateNetworkGlobalEntity(CFAllocatorRef a, CFStringRef domain, CFStringRef entity)
{
	return CFStringCreateWithFormat(a, 0, __builtin___CFStringMakeConstantString("%@/Network/Global/%@"), domain, entity);
}
CFStringRef SCDynamicStoreKeyCreateProxies(CFAllocatorRef a)
{
	return SCDynamicStoreKeyCreateNetworkGlobalEntity(a, kSCDynamicStoreDomainState,
		__builtin___CFStringMakeConstantString("Proxies"));
}

/* reachability: the object is an empty dictionary (a real, releasable CF object) */
static CFTypeRef reach_new(void)
{
	return CFDictionaryCreate(kCFAllocatorDefault, 0, 0, 0, kCFTypeDictionaryKeyCallBacks, kCFTypeDictionaryValueCallBacks);
}
CFTypeRef SCNetworkReachabilityCreateWithAddress(CFAllocatorRef a, const void *addr) { return reach_new(); }
CFTypeRef SCNetworkReachabilityCreateWithAddressPair(CFAllocatorRef a, const void *l, const void *r) { return reach_new(); }
CFTypeRef SCNetworkReachabilityCreateWithName(CFAllocatorRef a, const char *n) { return reach_new(); }
Boolean SCNetworkReachabilityGetFlags(CFTypeRef t, unsigned int *flags)
{
	if (flags) *flags = 2; /* kSCNetworkReachabilityFlagsReachable */
	return 1;
}
Boolean SCNetworkReachabilitySetCallback(CFTypeRef t, void *cb, void *ctx) { return 1; }
Boolean SCNetworkReachabilityScheduleWithRunLoop(CFTypeRef t, CFTypeRef rl, CFStringRef mode) { return 1; }
Boolean SCNetworkReachabilityUnscheduleFromRunLoop(CFTypeRef t, CFTypeRef rl, CFStringRef mode) { return 1; }

const char *SCErrorString(int status) { return status ? "SystemConfiguration error" : "Success"; }
CFArrayRef SCNetworkInterfaceCopyAll(void) { return CFArrayCreate(kCFAllocatorDefault, 0, 0, kCFTypeArrayCallBacks); }
CFStringRef SCNetworkInterfaceGetBSDName(CFTypeRef i) { return 0; }
CFStringRef SCNetworkInterfaceGetHardwareAddressString(CFTypeRef i) { return 0; }

/* dynamic store without configd: the store is an empty dictionary (a real, releasable CF object), the
 * notification keys are accepted, the run loop source is an inert version-0 source that never fires.
 * Chrome's network_config_watcher_apple.cc retries forever when SCDynamicStoreCreate returns NULL. */
extern CFTypeRef CFRunLoopSourceCreate(CFAllocatorRef, CFIndex, void *);
CFTypeRef SCDynamicStoreCreate(CFAllocatorRef a, CFStringRef name, void *callout, void *context) { return reach_new(); }
Boolean SCDynamicStoreSetNotificationKeys(CFTypeRef store, CFArrayRef keys, CFArrayRef patterns) { return 1; }
CFTypeRef SCDynamicStoreCreateRunLoopSource(CFAllocatorRef a, CFTypeRef store, CFIndex order)
{
	long ctx[9] = { 0 };
	return CFRunLoopSourceCreate(a, order, ctx);
}
