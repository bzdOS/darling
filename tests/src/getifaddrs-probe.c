/* getifaddrs-probe: getifaddrs must succeed and list at least the loopback interface (Chrome net/base/network_interfaces_getifaddrs.cc
 * calls it and then SIOCGIFAFLAG_IN6 / SIOCGIFINFO_IN6 style ioctls on an AF_INET6 socket). Prints GIFA <step> ok|FAIL. */
struct sockaddr { unsigned char sa_len, sa_family; char sa_data[14]; };
struct ifaddrs { struct ifaddrs *ifa_next; char *ifa_name; unsigned int ifa_flags; struct sockaddr *ifa_addr; struct sockaddr *ifa_netmask; struct sockaddr *ifa_dstaddr; void *ifa_data; };
extern int getifaddrs(struct ifaddrs **);
extern void freeifaddrs(struct ifaddrs *);
extern int printf(const char *, ...);
extern int fflush(void *);
extern int *__error(void);
extern int sysctl(int *, unsigned, void *, unsigned long *, void *, unsigned long);
extern int socket(int, int, int);
extern int ioctl(int, unsigned long, ...);
#define errno (*__error())
static int bad;
static void res(const char *n, int ok) { printf("GIFA %s %s (errno=%d)\n", n, ok ? "ok" : "FAIL", errno); fflush(0); if (!ok) bad++; }
int main(void)
{
	struct ifaddrs *ifa = 0, *p; int n = 0;
	errno = 0;
	{
		int mib[6] = {4, 17, 0, 0, 3, 0}; unsigned long len = 0; char buf[8192]; int e;
		errno = 0; e = sysctl(mib, 6, 0, &len, 0, 0);
		printf("GIFA sysctl-size r=%d len=%lu errno=%d\n", e, len, errno);
		len = sizeof buf; errno = 0; e = sysctl(mib, 6, buf, &len, 0, 0);
		printf("GIFA sysctl-data r=%d len=%lu errno=%d\n", e, len, errno);
		int s = socket(2, 2, 0);
		printf("GIFA socket %d errno=%d\n", s, errno);
		char ifr[64] = "lo0"; errno = 0;
		e = ioctl(s, 0xc0206911UL, ifr); /* SIOCGIFFLAGS */
		printf("GIFA ioctl SIOCGIFFLAGS r=%d errno=%d\n", e, errno);
	}
	int r = getifaddrs(&ifa);
	res("getifaddrs", r == 0 && ifa);
	for (p = ifa; p; p = p->ifa_next) { n++; printf("GIFA if %s flags=%x fam=%d\n", p->ifa_name, p->ifa_flags, p->ifa_addr ? p->ifa_addr->sa_family : -1); }
	res("count", n > 0);
	if (ifa) freeifaddrs(ifa);
	printf("GIFA DONE fail=%d\n", bad); fflush(0);
	return bad != 0;
}
