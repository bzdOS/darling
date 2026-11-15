/* osversion-probe: what the guest reports as its OS version. */
#include <stdio.h>
#include <sys/sysctl.h>
#include <sys/utsname.h>

static void show(const char *name)
{
	char buf[128] = "";
	size_t len = sizeof buf;
	int rc = sysctlbyname(name, buf, &len, NULL, 0);
	printf("osversion-probe: %s rc=%d \"%s\"\n", name, rc, buf);
}

int main(void)
{
	struct utsname u;
	show("kern.osproductversion");
	show("kern.osrelease");
	show("kern.osversion");
	if (uname(&u) == 0)
		printf("osversion-probe: uname release=%s version=%s\n", u.release, u.version);
	return 0;
}
