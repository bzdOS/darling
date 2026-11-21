/* devnull-probe: guest /dev/null, /dev/zero, /dev/urandom open and realpath (crashpad close_stdio opens /dev/null) */
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>

int main(void)
{
	int bad = 0;
	const char *devs[] = { "/dev/null", "/dev/zero", "/dev/urandom" };
	for (int i = 0; i < 3; i++) {
		int fd = open(devs[i], O_RDWR);
		char b[16]; long n = -1;
		if (fd >= 0) { n = (i == 0) ? write(fd, "x", 1) : read(fd, b, sizeof(b)); close(fd); }
		char rp[PATH_MAX]; char *r = realpath(devs[i], rp);
		printf("DEV %s open=%s io=%ld realpath=%s\n", devs[i], fd >= 0 ? "ok" : strerror(errno), n,
		       r ? r : strerror(errno));
		if (fd < 0 || n < 1 || !r) bad = 1;
	}
	printf(bad ? "PROBE-BAD\n" : "PROBE-OK\n");
	return bad;
}
