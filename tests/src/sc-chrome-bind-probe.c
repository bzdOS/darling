/* sc-chrome-bind-probe: the SystemConfiguration dynamic store and IOPowerSources symbols are imported
 * two-level and bound at link time to /usr/lib/IOKitExtras.dylib, which is where the Chrome framework binds
 * them (lib ordinal IOKitExtras); no dlsym. Calls them the way network_config_watcher_apple.cc and
 * battery_status_manager_mac.cc do and prints SCBIND <name> ok|NULL. */
#include <stdio.h>
typedef const void *R;
extern R SCDynamicStoreCreate(R, R, void *, void *);
extern unsigned char SCDynamicStoreSetNotificationKeys(R, R, R);
extern R SCDynamicStoreCreateRunLoopSource(R, R, long);
extern R IOPSCopyPowerSourcesInfo(void);
extern R IOPSCopyPowerSourcesList(R);
extern R IOPSGetPowerSourceDescription(R, R);
extern R IOPSNotificationCreateRunLoopSource(void (*)(void *), void *);
extern void CFRelease(R);
extern char __CFInitialized;
extern void __CFInitialize(void);
static int bad;
static void res(const char *n, R v) { printf("SCBIND %s %s\n", n, v ? "ok" : "NULL"); fflush(stdout); if (!v) bad++; }
int main(void)
{
	if (!__CFInitialized) __CFInitialize();
	R store = SCDynamicStoreCreate(0, 0, 0, 0);
	res("SCDynamicStoreCreate", store);
	res("SCDynamicStoreSetNotificationKeys", (R)(long)SCDynamicStoreSetNotificationKeys(store, 0, 0));
	res("SCDynamicStoreCreateRunLoopSource", SCDynamicStoreCreateRunLoopSource(0, store, 0));
	R info = IOPSCopyPowerSourcesInfo();
	res("IOPSCopyPowerSourcesInfo", info);
	res("IOPSCopyPowerSourcesList", IOPSCopyPowerSourcesList(info));
	res("IOPSGetPowerSourceDescription", IOPSGetPowerSourceDescription(info, 0));
	res("IOPSNotificationCreateRunLoopSource", IOPSNotificationCreateRunLoopSource(0, 0));
	if (store) CFRelease(store);
	printf("SCBIND DONE fail=%d\n", bad); fflush(stdout);
	return bad != 0;
}
