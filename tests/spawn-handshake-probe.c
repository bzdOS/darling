// spawn-handshake-probe: parent bootstrap_check_in()s a service, posix_spawn()s itself as a child; the child
// bootstrap_look_up()s the service, sends a request carrying a reply port, and waits for the reply (the
// Chrome MachPortRendezvous shape). After the handshake the child blocks in a few cancellable calls
// (poll, nanosleep, select) for PHASE_MS each and counts EINTR returns per call.
// Output: one line per phase "PHASE <name> calls=N eintr=M" and a final "SUMMARY ... ok=1|0".
// usage: spawn-handshake-probe [PHASE_MS]   (child mode: spawn-handshake-probe --child NAME PHASE_MS)
#include <mach/mach.h>
#include <servers/bootstrap.h>
#include <spawn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <poll.h>
#include <time.h>
#include <sys/select.h>
#include <sys/wait.h>
#include <sys/time.h>

extern char **environ;

typedef struct { mach_msg_header_t h; int v; mach_msg_trailer_t t; } msg_t;

static long now_ms(void) { struct timeval tv; gettimeofday(&tv, NULL); return tv.tv_sec * 1000L + tv.tv_usec / 1000; }

static int child(const char *name, int phase_ms) {
	mach_port_t svc = MACH_PORT_NULL, reply = MACH_PORT_NULL;
	kern_return_t kr = KERN_SUCCESS;
	if (strcmp(name, "-") == 0) { svc = bootstrap_port; printf("CHILD using special bootstrap port %u\n", svc); }
	else { kr = bootstrap_look_up(bootstrap_port, name, &svc); printf("CHILD look_up kr=%d\n", kr); }
	if (kr != KERN_SUCCESS || svc == MACH_PORT_NULL) return 2;
	mach_port_allocate(mach_task_self(), MACH_PORT_RIGHT_RECEIVE, &reply);
	msg_t m; memset(&m, 0, sizeof m);
	m.h.msgh_bits = MACH_MSGH_BITS(MACH_MSG_TYPE_COPY_SEND, MACH_MSG_TYPE_MAKE_SEND_ONCE);
	m.h.msgh_size = sizeof m - sizeof m.t; m.h.msgh_remote_port = svc; m.h.msgh_local_port = reply; m.h.msgh_id = 1; m.v = 42;
	kr = mach_msg(&m.h, MACH_SEND_MSG | MACH_RCV_MSG, m.h.msgh_size, sizeof m, reply, 5000, MACH_PORT_NULL);
	printf("CHILD handshake kr=%d v=%d\n", kr, m.v);
	if (kr != KERN_SUCCESS) return 3;
	fflush(stdout);

	int p[2]; pipe(p);
	long end; int calls, eintr;
	calls = eintr = 0; end = now_ms() + phase_ms;
	while (now_ms() < end) { struct pollfd pf = { p[0], POLLIN, 0 }; calls++; if (poll(&pf, 1, 50) < 0 && errno == EINTR) eintr++; }
	printf("PHASE poll calls=%d eintr=%d\n", calls, eintr);
	calls = eintr = 0; end = now_ms() + phase_ms;
	while (now_ms() < end) { struct timespec ts = { 0, 50000000 }; calls++; if (nanosleep(&ts, NULL) < 0 && errno == EINTR) eintr++; }
	printf("PHASE nanosleep calls=%d eintr=%d\n", calls, eintr);
	calls = eintr = 0; end = now_ms() + phase_ms;
	while (now_ms() < end) { fd_set r; FD_ZERO(&r); FD_SET(p[0], &r); struct timeval tv = { 0, 50000 }; calls++; if (select(p[0] + 1, &r, NULL, NULL, &tv) < 0 && errno == EINTR) eintr++; }
	printf("PHASE select calls=%d eintr=%d\n", calls, eintr);
	return 0;
}

int main(int argc, char **argv) {
	if (argc >= 4 && strcmp(argv[1], "--child") == 0) { int r = child(argv[2], atoi(argv[3])); fflush(stdout); _exit(r); }
	int phase_ms = argc > 1 ? atoi(argv[1]) : 1000;
	char name[128]; snprintf(name, sizeof name, "org.darling.spawn-handshake-probe.%d", getpid());
	mach_port_t rp = MACH_PORT_NULL;
	kern_return_t kr = bootstrap_check_in(bootstrap_port, name, &rp);
	printf("PARENT check_in kr=%d\n", kr);
	posix_spawnattr_t at; posix_spawnattr_init(&at);
	if (kr != KERN_SUCCESS) {
		// no bootstrap server (no launchd): hand the port to the child as its bootstrap special port
		mach_port_allocate(mach_task_self(), MACH_PORT_RIGHT_RECEIVE, &rp);
		mach_port_insert_right(mach_task_self(), rp, rp, MACH_MSG_TYPE_MAKE_SEND);
		kr = posix_spawnattr_setspecialport_np(&at, rp, TASK_BOOTSTRAP_PORT);
		printf("PARENT specialport kr=%d\n", kr);
		strcpy(name, "-");
	}
	char ms[16]; snprintf(ms, sizeof ms, "%d", phase_ms);
	char *cargv[] = { argv[0], "--child", name, ms, NULL };
	pid_t pid; int e = posix_spawn(&pid, argv[0], NULL, &at, cargv, environ);
	printf("PARENT spawn rc=%d pid=%d\n", e, pid);
	if (e) { printf("SUMMARY handshake=0 ok=0\n"); return 1; }
	msg_t m; memset(&m, 0, sizeof m);
	kr = mach_msg(&m.h, MACH_RCV_MSG | MACH_RCV_TIMEOUT, 0, sizeof m, rp, 10000, MACH_PORT_NULL);
	printf("PARENT recv kr=%d v=%d\n", kr, m.v);
	int hs = 0;
	if (kr == KERN_SUCCESS) {
		mach_msg_header_t r; memset(&r, 0, sizeof r);
		r.msgh_bits = MACH_MSGH_BITS(MACH_MSGH_BITS_REMOTE(m.h.msgh_bits), 0);
		r.msgh_size = sizeof r; r.msgh_remote_port = m.h.msgh_remote_port; r.msgh_id = 2;
		kr = mach_msg(&r, MACH_SEND_MSG, sizeof r, 0, MACH_PORT_NULL, 0, MACH_PORT_NULL);
		printf("PARENT reply kr=%d\n", kr); hs = (kr == KERN_SUCCESS);
	}
	fflush(stdout);
	int st = 0; waitpid(pid, &st, 0);
	printf("SUMMARY handshake=%d child_status=%d ok=%d\n", hs, st, hs && st == 0);
	return 0;
}
