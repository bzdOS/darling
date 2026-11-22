/* sc-stubs-probe: resolve, through /usr/lib/suppl.dylib (where Chrome binds them), every SystemConfiguration
 * symbol Chrome imports at startup, call the functions and print SCCHK <name> ok|NULL. */
#include <dlfcn.h>
#include <stdio.h>
typedef const void *(*f0)(void);
typedef const void *(*f1)(const void *);
typedef const void *(*f2)(const void *, const void *);
typedef const void *(*f3)(const void *, const void *, const void *);
static int bad;
static void res(const char *n, const void *v) { printf("SCCHK %s %s\n", n, v ? "ok" : "NULL"); fflush(stdout); if (!v) bad++; }
int main(void)
{
	printf("SCCHK start malloc=%p\n", __builtin_malloc(16)); fflush(stdout);
	{ extern void *malloc_default_zone(void); printf("SCCHK dz=%p\n", malloc_default_zone()); fflush(stdout); }
	{ extern char __CFInitialized; extern void __CFInitialize(void); printf("SCCHK cfinit=%d\n", __CFInitialized); fflush(stdout); if (!__CFInitialized) __CFInitialize(); }
	{ extern const void *CFNumberCreate(const void *, int, const void *); int z = 0; printf("SCCHK cfnum=%p\n", CFNumberCreate(0, 3, &z)); fflush(stdout); }
	void *h = dlopen("/usr/lib/suppl.dylib", RTLD_NOW);
	if (!h) { printf("SCCHK dlopen failed: %s\n", dlerror()); printf("SCCHK DONE fail=1\n"); return 1; }
	const char *consts[] = { "kSCDynamicStoreDomainState", "kSCDynamicStorePropNetPrimaryInterface", "kSCEntNetIPv4", "kSCEntNetIPv6",
		"kSCEntNetInterface", "kSCPropNetProxiesExceptionsList", "kSCPropNetProxiesExcludeSimpleHostnames", "kSCPropNetProxiesFTPEnable",
		"kSCPropNetProxiesFTPPort", "kSCPropNetProxiesFTPProxy", "kSCPropNetProxiesHTTPEnable", "kSCPropNetProxiesHTTPPort",
		"kSCPropNetProxiesHTTPProxy", "kSCPropNetProxiesHTTPSEnable", "kSCPropNetProxiesHTTPSPort", "kSCPropNetProxiesHTTPSProxy",
		"kSCPropNetProxiesProxyAutoConfigEnable", "kSCPropNetProxiesProxyAutoConfigURLString", "kSCPropNetProxiesProxyAutoDiscoveryEnable",
		"kSCPropNetProxiesSOCKSEnable", "kSCPropNetProxiesSOCKSPort", "kSCPropNetProxiesSOCKSProxy", 0 };
	for (int i = 0; consts[i]; i++) {
		void **p = dlsym(h, consts[i]);
		res(consts[i], p ? *p : 0);
	}
	void *s;
	if (!(s = dlsym(h, "SCDynamicStoreCopyProxies"))) res("SCDynamicStoreCopyProxies(sym)", 0); else res("SCDynamicStoreCopyProxies", ((f1)s)(0));
	if (!(s = dlsym(h, "SCDynamicStoreCopyLocalHostName"))) res("SCDynamicStoreCopyLocalHostName(sym)", 0); else res("SCDynamicStoreCopyLocalHostName", ((f1)s)(0));
	if (!(s = dlsym(h, "SCDynamicStoreCopyComputerName"))) res("SCDynamicStoreCopyComputerName(sym)", 0); else res("SCDynamicStoreCopyComputerName", ((f2)s)(0, 0));
	if (!(s = dlsym(h, "SCDynamicStoreKeyCreateNetworkGlobalEntity"))) res("KeyCreateNetworkGlobalEntity(sym)", 0);
	else res("SCDynamicStoreKeyCreateNetworkGlobalEntity", ((f3)s)(0, *(void **)dlsym(h, "kSCDynamicStoreDomainState"), *(void **)dlsym(h, "kSCEntNetIPv4")));
	if (!(s = dlsym(h, "SCNetworkReachabilityCreateWithAddress"))) res("ReachabilityCreateWithAddress(sym)", 0);
	else {
		const void *r = ((f2)s)(0, "\x10\x02\0\0\0\0\0\0\0\0\0\0\0\0\0\0");
		res("SCNetworkReachabilityCreateWithAddress", r);
		unsigned flags = 0;
		int (*gf)(const void *, unsigned *) = dlsym(h, "SCNetworkReachabilityGetFlags");
		res("SCNetworkReachabilityGetFlags", gf && r && gf(r, &flags) && flags ? (const void *)1 : 0);
	}
	const char *syms[] = { "SCNetworkReachabilitySetCallback", "SCNetworkReachabilityScheduleWithRunLoop",
		"SCNetworkReachabilityUnscheduleFromRunLoop", "SCDynamicStoreKeyCreateProxies", "SCErrorString", "SCNetworkInterfaceCopyAll", 0 };
	for (int i = 0; syms[i]; i++) res(syms[i], dlsym(h, syms[i]));
	if ((s = dlsym(h, "SCNetworkInterfaceCopyAll"))) res("SCNetworkInterfaceCopyAll()", ((f0)s)());
	if ((s = dlsym(h, "SCDynamicStoreKeyCreateProxies"))) res("SCDynamicStoreKeyCreateProxies()", ((f1)s)(0));
	if (!(s = dlsym(h, "SCDynamicStoreCreate"))) res("SCDynamicStoreCreate(sym)", 0);
	else {
		const void *st = ((f3)s)(0, 0, 0);
		res("SCDynamicStoreCreate", st);
		const void *(*ck)(const void *, const void *, const void *, const void *) = dlsym(h, "SCDynamicStoreSetNotificationKeys");
		res("SCDynamicStoreSetNotificationKeys(sym)", (const void *)ck);
		const void *(*rs)(const void *, const void *, long) = dlsym(h, "SCDynamicStoreCreateRunLoopSource");
		res("SCDynamicStoreCreateRunLoopSource", rs && st ? rs(0, st, 0) : 0);
	}
	{
		/* IOKit power sources (Chrome battery_status_manager_mac.cc): no IOPS* may be missing or return NULL */
		void *k = dlopen("/System/Library/Frameworks/IOKit.framework/Versions/A/IOKit", RTLD_NOW);
		if (!k) res("IOKit dlopen", 0);
		else {
			const void *(*pi)(void) = dlsym(k, "IOPSCopyPowerSourcesInfo");
			const void *(*pl)(const void *) = dlsym(k, "IOPSCopyPowerSourcesList");
			const void *(*pd)(const void *, const void *) = dlsym(k, "IOPSGetPowerSourceDescription");
			const void *(*pn)(void (*)(void *), void *) = dlsym(k, "IOPSNotificationCreateRunLoopSource");
			const void *info = pi ? pi() : 0;
			res("IOPSCopyPowerSourcesInfo", info);
			res("IOPSCopyPowerSourcesList", pl && info ? pl(info) : 0);
			res("IOPSGetPowerSourceDescription(sym)", (const void *)pd);
			res("IOPSNotificationCreateRunLoopSource", pn ? pn(0, 0) : 0);
		}
	}
	printf("SCCHK DONE fail=%d\n", bad);
	return bad ? 1 : 0;
}
