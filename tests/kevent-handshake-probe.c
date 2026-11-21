/* kevent-handshake-probe: the kevent registration of crashpad ChildPortHandshake::RunServer
 * (EVFILT_MACHPORT on a receive right + EVFILT_WRITE on a pipe write end), each filter alone and both. */
#include <sys/event.h>
#include <mach/mach.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <spawn.h>
#include <sys/wait.h>
#include <mach-o/dyld.h>
extern char **environ;
#ifndef POSIX_SPAWN_CLOEXEC_DEFAULT
#define POSIX_SPAWN_CLOEXEC_DEFAULT 0x4000
#endif
int posix_spawn_file_actions_addinherit_np(posix_spawn_file_actions_t *, int);
static int try(const char *what, struct kevent *ch, int n);

/* child: is the descriptor open, and does the handshake kevent work on it */
static int child(int fd)
{
	int fl = fcntl(fd, F_GETFD);
	printf("CHILD fd=%d getfd=%d %s\n", fd, fl, fl < 0 ? strerror(errno) : "open");
	if (fl < 0) return 1;
	struct kevent ch;
	EV_SET(&ch, fd, EVFILT_WRITE, EV_ADD | EV_CLEAR, 0, 0, NULL);
	return try("child-pipewrite", &ch, 1) ? 3 : 0;
}

/* spawn ourselves with CLOEXEC_DEFAULT, passing the CLOEXEC write end by dup2(fd,fd) or addinherit_np */
static int spawn_mode(const char *mode, int fd)
{
	char exe[1024]; uint32_t sz = sizeof(exe);
	if (_NSGetExecutablePath(exe, &sz) != 0) { printf("SPAWN %s no exe path\n", mode); return 1; }
	posix_spawn_file_actions_t fa; posix_spawnattr_t at;
	posix_spawn_file_actions_init(&fa); posix_spawnattr_init(&at);
	posix_spawnattr_setflags(&at, POSIX_SPAWN_CLOEXEC_DEFAULT);
	if (!strcmp(mode, "dup2")) posix_spawn_file_actions_adddup2(&fa, fd, fd);
	else posix_spawn_file_actions_addinherit_np(&fa, fd);
	char fds[16]; snprintf(fds, sizeof(fds), "%d", fd);
	char *av[] = { exe, "--child", fds, NULL };
	pid_t pid; int rv = posix_spawn(&pid, exe, &fa, &at, av, environ);
	int st = -1; if (rv == 0) waitpid(pid, &st, 0);
	printf("SPAWN %s rv=%d status=%d\n", mode, rv, st);
	return rv != 0 || !WIFEXITED(st) || WEXITSTATUS(st) != 0;
}

static int try(const char *what, struct kevent *ch, int n)
{
	int kq = kqueue();
	if (kq < 0) { printf("KQ %s kqueue=%s\n", what, strerror(errno)); return 1; }
	int rv = kevent(kq, ch, n, NULL, 0, NULL);
	printf("KQ %s kevent=%d %s\n", what, rv, rv < 0 ? strerror(errno) : "ok");
	close(kq);
	return rv < 0;
}

int main(int argc, char **argv)
{
	if (argc == 3 && !strcmp(argv[1], "--child")) return child(atoi(argv[2]));
	mach_port_t port = MACH_PORT_NULL;
	kern_return_t kr = mach_port_allocate(mach_task_self(), MACH_PORT_RIGHT_RECEIVE, &port);
	printf("PORT kr=%d port=%u\n", kr, port);
	int p[2];
	if (pipe(p) < 0) { printf("pipe=%s\n", strerror(errno)); return 1; }
	struct kevent ch[2];
	EV_SET(&ch[0], port, EVFILT_MACHPORT, EV_ADD | EV_CLEAR, 0, 0, NULL);
	EV_SET(&ch[1], p[1], EVFILT_WRITE, EV_ADD | EV_CLEAR, 0, 0, NULL);
	int bad = 0;
	bad |= try("machport", &ch[0], 1);
	bad |= try("pipewrite", &ch[1], 1);
	bad |= try("both", ch, 2);
	fcntl(p[1], F_SETFD, FD_CLOEXEC);
	bad |= spawn_mode("dup2", p[1]);
	bad |= spawn_mode("inherit", p[1]);
	printf(bad ? "PROBE-BAD\n" : "PROBE-OK\n");
	return bad;
}
