/* bind-publish-probe: does the host kernel publish a bound AF_UNIX
 * SOCK_DGRAM socket over the sockaddr shapes the checkin path uses?
 * Host-only probe, no guest involved.
 *
 * hold <kind> <path> <seconds>
 *   binds per <kind>, prints the result, keeps the socket open.
 *   kinds:
 *     sunlen=N    - memset-zero sockaddr with sun_len=N (N>=0);
 *                   N=auto means SUN_LEN(&addr)
 *     server      - byte-for-byte replica of the darlingserver bind site:
 *                   uninitialized stack struct, sun_family set, strncpy,
 *                   bind(..., sizeof(addr)); sun_len stays stack garbage.
 *                   The byte is printed after bind (readback).
 * client <path> <kind>
 *   connects a dgram socket per <kind> and prints rc/errno.
 */
#include <sys/socket.h>
#include <sys/un.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

static void dirty_stack(unsigned char b) {
	volatile unsigned char pad[2048];
	memset((void *)pad, b, sizeof(pad));
	(void)pad[0];
	(void)pad[2047];
}

/* returns >=0: explicit sun_len; -1: auto (SUN_LEN) */
static int parse_kind(const char *kind, struct sockaddr_un *a, const char *path) {
	memset(a, 0, sizeof(*a));
	a->sun_family = AF_UNIX;
	strncpy(a->sun_path, path, sizeof(a->sun_path) - 1);
	if (strcmp(kind, "auto") == 0) {
		a->sun_len = (unsigned char)SUN_LEN(a);
		return -1;
	}
	if (strncmp(kind, "sunlen=", 7) == 0)
		return atoi(kind + 7);
	return -2; /* "server": caller handles */
}

static void report(const char *what, int rc) {
	if (rc < 0)
		printf("PROBE %s rc=-1 errno=%d (%s)\n", what, errno, strerror(errno));
	else
		printf("PROBE %s rc=%d\n", what, rc);
	fflush(stdout);
}

int main(int argc, char **argv) {
	if (argc < 2) {
	usage:
		fprintf(stderr, "usage: %s hold <sunlen=N|auto|server> <path> <seconds>\n"
		                "       %s client <path> <sunlen=N|auto|server>\n", argv[0], argv[0]);
		return 2;
	}

	if (strcmp(argv[1], "hold") == 0 && argc == 5) {
		const char *kind = argv[2];
		const char *path = argv[3];
		int secs = atoi(argv[4]);
		int fd = socket(AF_UNIX, SOCK_DGRAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
		if (fd < 0) { report("socket", -1); return 1; }
		struct sockaddr_un addr;

		if (strcmp(kind, "server") == 0) {
			dirty_stack(0x00);
			dirty_stack(0xA5);
			/* uninitialized struct on a deliberately dirtied stack */
			struct sockaddr_un saddr;
			saddr.sun_family = AF_UNIX;
			saddr.sun_path[sizeof(saddr.sun_path) - 1] = '\0';
			strncpy(saddr.sun_path, path, sizeof(saddr.sun_path) - 1);
			report("bind_server", bind(fd, (struct sockaddr *)&saddr, sizeof(saddr)));
			printf("PROBE readback sun_len_byte=%u family=%u\n",
			       (unsigned)((unsigned char *)&saddr)[0],
			       (unsigned)((unsigned char *)&saddr)[1]);
			fflush(stdout);
		} else {
			int n = parse_kind(kind, &addr, path);
			if (n == -2) goto usage;
			if (n >= 0) addr.sun_len = (unsigned char)n;
			report("bind", bind(fd, (struct sockaddr *)&addr, sizeof(addr)));
			printf("PROBE readback sun_len_byte=%u\n", (unsigned)addr.sun_len);
			fflush(stdout);
		}

		struct sockaddr_un gn;
		socklen_t gl = sizeof(gn);
		if (getsockname(fd, (struct sockaddr *)&gn, &gl) == 0) {
			printf("PROBE getsockname len=%u path=%s\n", (unsigned)gn.sun_len, gn.sun_path);
			fflush(stdout);
		} else {
			report("getsockname", -1);
		}
		sleep(secs);
		close(fd);
		return 0;
	}

	if (strcmp(argv[1], "client") == 0 && argc == 4) {
		const char *path = argv[2];
		const char *kind = argv[3];
		int fd = socket(AF_UNIX, SOCK_DGRAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
		if (fd < 0) { report("client_socket", -1); return 1; }
		struct sockaddr_un addr;
		int n = parse_kind(kind, &addr, path);
		if (n == -2) goto usage;
		if (n >= 0) addr.sun_len = (unsigned char)n;
		report("client_connect", connect(fd, (struct sockaddr *)&addr, sizeof(addr)));
		/* a dgram send proves the peer is really there (connect alone can
		 * defer the refusal to the first send on some paths) */
		char b[1] = {0};
		report("client_send", (int)send(fd, b, 1, 0));
		close(fd);
		return 0;
	}

	goto usage;
}
