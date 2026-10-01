/* guest-sem-open.c — CAN THE GUEST SEE A NAMED POSIX SEMAPHORE?
 *
 * A separate probe, not a mode of guest-thread-wait.c, because the question is
 * not the lane's question and the lane's probe has been accepted and read. This
 * one answers a single yes/no that decides whether the lane can be measured at
 * all, and it answers it the only way that counts: by calling sem_open in the
 * guest and printing what came back.
 *
 * WHY THE QUESTION EXISTS. The guest's _sem_init is an upstream stub — XNU
 * stopped generating the syscalls for the obsolete anonymous POSIX semaphore
 * (libsyscall/wrappers/posix_sem_obsolete.c, whose own header comment says so;
 * sys/semaphore.h marks sem_init, sem_destroy and sem_getvalue __deprecated
 * and does not mark sem_open). So every wait in the lane probe has been reading
 * an uninitialised int, and the EINVAL it drew belonged to that. sem_open is a
 * different story: sys_sem_open calls elfcalls()->sem_open, which mldr fills
 * from the host's sem_open (elfcalls.c:111), and on the build machine
 * sem_open + sem_wait park and release correctly.
 *
 * BUT sem_open takes a NAME. An address in memory needs no filesystem; a name
 * does. This probe exists because that difference has never been tested inside
 * the guest's vchroot, and assuming it works would be the same class of
 * unearned verdict the lane has already produced twice.
 *
 * WHAT IT PRINTS, and why each line is separate:
 *   sem_open's return value and errno  — whether the constructor is reachable
 *   the name's directory after the call — whether a namespace exists at all
 *   the wait's rc, errno and duration  — whether the SAME wait that refused on
 *                                        an uninitialised object parks here
 *   sem_unlink's rc                    — whether the probe cleans up after
 * itself, which is what makes it safe to run repeatedly
 *
 * A verdict is printed only from what the call returned. There is no path here
 * that reports success it did not observe, and none that explains a failure
 * away: if sem_open returns NULL, that is the finding, and the errno is printed
 * with it rather than summarised as "unavailable".
 */

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <semaphore.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define RELEASE_MS 2000
#define BOUND_MS   3000

/* The name is an argument so a failure can be retried with a different one
 * without rebuilding. It carries a LEADING SLASH, and that is not a style
 * choice: POSIX requires one, and a name without it is refused with EINVAL.
 * The first version of this file asserted the opposite in a comment — that a
 * leading slash was "implementation-defined" and that avoiding the question
 * made the probe cleaner. The native run disagreed within a minute: EINVAL on
 * the bare name, rc=0 on the slashed one, same machine, same second. A
 * self-test that cannot contradict its own author's assumption is not a
 * self-test, and this comment is left as the record of having been wrong. */
#define DEFAULT_NAME "/guest-sem-open-probe"

/* Same shape as the lane probe's spin: a bounded wait on the only clock this
 * guest exports (228), never a sleep, because 35 nanosleep is undefined here. */
static long now_ms(void)
{
	struct timespec ts;

	if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) return 0;
	return (long)ts.tv_sec * 1000L + ts.tv_nsec / 1000000L;
}

static void spin_ms(long ms)
{
	long start = now_ms();

	if (start == 0) return;
	while (now_ms() - start < ms) { }
}

static sem_t *the_sem;
static volatile int poster_ran;

/* The release under test, and the only one. One post, because a second would
 * be a safety net that cannot tell a probe which post woke it — and a probe
 * that cannot tell that must not claim the release worked. */
static void *poster(void *p)
{
	(void)p;
	spin_ms(RELEASE_MS);
	poster_ran = 1;
	if (the_sem != NULL)
		sem_post(the_sem);
	return NULL;
}

int main(int argc, char **argv)
{
	const char *name = (argc > 1) ? argv[1] : DEFAULT_NAME;
	pthread_t t;
	int rc, wait_rc, wait_err;
	long t0, dur;
	int post_rc = -1, post_err = 0;

	setvbuf(stdout, NULL, _IONBF, 0);
	puts("guest named-semaphore reachability, under mldr:");
	printf("  name: %s\n", name);

	errno = 0;
	the_sem = sem_open(name, O_CREAT | O_EXCL, 0600, 0);
	rc = (the_sem == NULL) ? -1 : 0;
	printf("  %-14s rc=%d errno=%s handle=%s\n", "sem_open", rc,
	       rc ? strerror(errno) : "0", the_sem ? "non-NULL" : "NULL");

if (the_sem == NULL) {
		/* Printed ONLY on failure. The first version printed them always and
		 * claimed they name the layer that is missing; the guest run
		 * contradicted that outright — /dev/shm ABSENT while sem_open returned
		 * rc=0 — because FreeBSD keeps POSIX semaphores under /tmp, not
		 * /dev/shm. A diagnostic that is wrong when the call succeeds cannot
		 * be trusted to be right when it fails either, so they are now part of
		 * the failure report and nowhere else. */
		printf("  %-14s %s\n", "/dev/shm",
		       (access("/dev/shm", F_OK) == 0) ? "exists" : "ABSENT");
		printf("  %-14s %s\n", "/tmp",
		       (access("/tmp", F_OK) == 0) ? "exists" : "ABSENT");
		puts("");
		puts("VERDICT");
		puts("  (no) the guest's sem_open did not return a semaphore, so the lane's");
		puts("  legs have no initialisable primitive and no classification is possible.");
		puts("  The errno above is the finding; it is not explained away here.");
		return 0;
	}

	/* The same wait, on a semaphore this constructor actually built. */
	if (pthread_create(&t, NULL, poster, NULL) != 0) {
		puts("  FATAL: the poster thread could not be created");
		return 0;
	}

	t0 = now_ms();
	errno = 0;
	wait_rc = sem_wait(the_sem);
	wait_err = errno;
	dur = now_ms() - t0;
	pthread_detach(t);

	/* Parked is claimed only from what the wait returned: rc==0 after the
	 * RELEASE_MS window is a wait that sat in the kernel, and anything else is
	 * printed as whatever it was. A probe cannot tell "parked" from "returned
	 * early" without measuring, so this line is the measurement. */
	printf("  %-14s rc=%d errno=%s over %s poster_ran=%d\n", "sem_wait",
	       wait_rc, wait_err ? strerror(wait_err) : "0",
	       (t0 == 0) ? "no clock" : ((dur >= RELEASE_MS - 100)
	                                 ? "the RELEASE_MS window"
	                                 : "LESS than the window"),
	       poster_ran);

	errno = 0;
	post_rc = sem_post(the_sem);
	post_err = errno;
	printf("  %-14s rc=%d errno=%s\n", "sem_post", post_rc,
	       post_err ? strerror(post_err) : "0");

	errno = 0;
	rc = sem_unlink(name);
	printf("  %-14s rc=%d errno=%s  (cleanup: this is what makes a rerun safe)\n",
	       "sem_unlink", rc, rc ? strerror(errno) : "0");

	puts("");
	puts("VERDICT");
	if (wait_rc == 0 && dur >= RELEASE_MS - 100) {
		puts("  (yes) the guest can build a named semaphore and its wait PARKED and came");
		puts("  back after the release. The lane's legs have a primitive that exists, so");
		puts("  the only change needed is the constructor — not the wait, not the trap.");
	} else if (wait_rc == 0) {
		puts("  (partial) the constructor works and the wait returned 0, but it came back");
		puts("  BEFORE the release window, so parking is still unproved here. That is a");
		puts("  different result from a refusal and is reported as one.");
	} else {
		puts("  (no) the constructor works but the wait refused, so a named semaphore does");
		puts("  not give the lane a primitive either. The errno above is the finding.");
	}
	return 0;
}