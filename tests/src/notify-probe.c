/* notify-probe: notify_register_dispatch / _check / _plain / _cancel, imported two-level from libSystem as Chrome's
 * ThermalStateObserverMac does (base/power_monitor/thermal_state_observer_mac.mm), must succeed (NOTIFY_STATUS_OK == 0)
 * instead of NOTIFY_STATUS_SERVER_NOT_FOUND (9) when no notification server is reachable. Prints NTFY <step> ok|FAIL. */
extern int printf(const char *, ...);
extern int fflush(void *);
extern void *dispatch_get_global_queue(long, unsigned long);
extern int notify_register_dispatch(const char *, int *, void *, void (^)(int));
extern int notify_register_check(const char *, int *);
extern int notify_register_plain(const char *, int *);
extern int notify_check(int, int *);
extern int notify_get_state(int, unsigned long long *);
extern int notify_cancel(int);
static int bad;
static void res(const char *n, int r) { printf("NTFY %s %s (status=%d)\n", n, r == 0 ? "ok" : "FAIL", r); fflush(0); if (r) bad++; }
int main(void)
{
	int t1 = -1, t2 = -1, t3 = -1, chk = 0;
	res("notify_register_dispatch", notify_register_dispatch("com.apple.system.thermalpressurelevel", &t1, dispatch_get_global_queue(0, 0), ^(int t) { (void)t; }));
	res("notify_register_check", notify_register_check("com.apple.system.thermalpressurelevel", &t2));
	res("notify_register_plain", notify_register_plain("com.apple.system.thermalpressurelevel", &t3));
	res("notify_check", notify_check(t2, &chk));
	unsigned long long st = 7;
	res("notify_get_state", notify_get_state(t2, &st));
	res("notify_cancel", notify_cancel(t1));
	printf("NTFY DONE fail=%d\n", bad); fflush(0);
	return bad != 0;
}
