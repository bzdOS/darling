/* fork-fail-probe.c - fork() and posix_spawn() must return, never block.
 * A successful fork passes only if the child is reaped and exited cleanly (status 0); a failed fork
 * passes if it returns -1 with errno set (a failed fork used to leave the parent in
 * fork_wait_for_child forever: stale _mach_fork_parent).
 * Output: one line  FORK-VERDICT pass|FAIL n=<checks> bad=<failed> [<names>]  (details on FORK-T lines);
 * no verdict line means a call blocked (the runner reports that as FAIL ... hang). */
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <spawn.h>
#include <sys/wait.h>

extern char **environ;
static int n, nbad;
static char badlist[256];
static void result(const char *name, int ok, long rc, int e)
{
	n++;
	if (!ok) { nbad++; strcat(badlist, name); strcat(badlist, ","); }
	printf("FORK-T %s %s rc=%ld errno=%d\n", name, ok ? "ok" : "bad", rc, e);
	fflush(stdout);
}

int main(void)
{
	int st = 0, e;
	long rc;
	printf("FORK-T begin fork\n"); fflush(stdout);
	errno = 0;
	rc = fork();
	e = errno;
	if (rc == 0) _exit(0);
	if (rc > 0) { st = -1; e = waitpid((pid_t)rc, &st, 0) == (pid_t)rc ? 0 : errno;
		result("fork", e == 0 && WIFEXITED(st) && WEXITSTATUS(st) == 0, rc, e); }
	else result("fork", rc == -1 && e != 0, rc, e);   /* failed fork: -1 with errno set */

	printf("FORK-T begin posix_spawn\n"); fflush(stdout);
	pid_t pid = 0;
	char *argv[] = { "/usr/bin/true", NULL };
	rc = posix_spawn(&pid, "/usr/bin/true", NULL, NULL, argv, environ);
	if (rc == 0 && pid > 0) { waitpid(pid, &st, 0); }
	result("posix_spawn", 1, rc, 0);   /* returned at all */

	printf("FORK-T begin fork-again\n"); fflush(stdout);
	errno = 0;
	rc = fork();
	e = errno;
	if (rc == 0) _exit(0);
	if (rc > 0) { st = -1; e = waitpid((pid_t)rc, &st, 0) == (pid_t)rc ? 0 : errno;
		result("fork-again", e == 0 && WIFEXITED(st) && WEXITSTATUS(st) == 0, rc, e); }
	else result("fork-again", rc == -1 && e != 0, rc, e);

	printf("FORK-VERDICT %s n=%d bad=%d [%s]\n", nbad ? "FAIL" : "pass", n, nbad, badlist);
	fflush(stdout);
	return nbad != 0;
}
