/* guest-thread-wait.c — does a spawned guest thread EVER come back from a
 * blocking wait?
 *
 * WHY THIS EXISTS
 * ---------------
 * A window probe found that a blocking Wayland roundtrip issued from a spawned
 * guest thread did not come back, while the same call on the main thread
 * returned promptly (roundtrip returned 3, wl_display_get_error = 0). One
 * connection, one call, two threads, two answers.
 *
 * That leaves the question this file exists to answer, and the question is not
 * "is Wayland broken". It is: **is the failure specific to the event path, or
 * do guest threads under mldr fail to return from ANY blocking wait?** The two
 * have very different fixes and very different amounts of work behind them, so
 * the classification has to be measured rather than assumed from one symptom.
 *
 * So: three kinds of blocking wait, each run TWICE — once on the main thread as
 * a control, once on a spawned thread — chosen to separate the MECHANISMS
 * rather than to repeat the same test three times:
 *
 *   1. read(pipe)   a descriptor wait, released by another thread's write
 *   2. write(pipe)  the mirror: the pipe starts FULL, released by a drain
 *   3. sem_wait     a lock, released by another thread's sem_post
 *
 * TWO PIPES, and one direction each. A pipe has a read end and a write end and
 * they are not interchangeable: a write to the read end is EBADF, not a block.
 * The first version of this probe passed one descriptor to the thread and used
 * it for both directions, so the thread's write failed with EBADF while the
 * main thread sat in a drain with an empty pipe — a hang, and a hang in a probe
 * whose whole job is to report rather than stall.
 *
 * WHY THERE IS NO TIMER LEG. It was here first and the syscall table removed
 * it. mldr's dispatch implements read, write and futex; it does NOT define
 * nanosleep (35), clock_nanosleep (230), poll (7), ppoll (271), select (23) or
 * pselect6 (270) — every one of those falls through to ENOSYS. A guest
 * nanosleep would fail on the MAIN thread too, so it would have measured a
 * missing syscall rather than a thread that cannot be resumed. The three legs
 * above are the three the guest can actually perform.
 *
 * RELEASE FIRST, THEN WAIT, on every leg. An earlier version waited for the
 * read flag and only then wrote the byte that the read was blocked on: a
 * deadlock, which reported all three legs as dead on a machine where all three
 * return, and reported it in the direction of blaming the guest. The release is
 * unconditional and immediate; only the wait that follows has a deadline. If the
 * flag does not appear after its release, the thread did not come back — which
 * is the thing being measured.
 *
 * NOTHING HERE IS FIXED, ONLY MEASURED. And this deliberately does NOT touch
 * the parked question about get_perthread_wd reading %gs:(,0xc9*8): two facts
 * about threads in one run are two facts, and a shared %gs is a hypothesis, not
 * a link.
 *
 * NO ROOT AND NO SEAT. There is no window, no compositor and no Wayland in this
 * file. Every wait is bounded and the program prints a verdict and exits rather
 * than sitting in a syscall, because a probe that hangs cannot report that it
 * hung.
 *
 * Compiling this for the HOST and running it there is a self-test of the probe,
 * and nothing more: on a real FreeBSD all three legs must return, and if they
 * do not the probe is broken and any guest result it goes on to produce is
 * worthless. Two of the three bugs above were found exactly that way.
 */

#include <errno.h>
#include <pthread.h>
#include <semaphore.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define WAIT_DEADLINE_SEC 3
#define PIPE_FILL 4096

struct thread_report {
	volatile int read_returned;
	volatile int write_returned;
	volatile int sem_returned;
};

static struct thread_report rep;

struct thread_arg {
	int read_fd;    /* the thread reads here; main writes */
	int write_fd;   /* the thread writes here; main drains. Pipe is full. */
	sem_t *sem;
};

/* --- the three waits, as plain functions so the thread and the main thread
 * --- run the SAME code and the comparison is like for like. ------------- */

static void do_read(int fd)
{
	char c;
	ssize_t n = read(fd, &c, 1);   /* blocks until main writes */
	rep.read_returned = (n == 1) ? 1 : 0;
}

static void do_write(int fd)
{
	char buf[64];
	memset(buf, 'w', sizeof(buf));
	/* The pipe is full, so this blocks until main drains. */
	if (write(fd, buf, sizeof(buf)) == (ssize_t)sizeof(buf))
		rep.write_returned = 1;
}

static void do_sem(sem_t *s)
{
	sem_wait(s);                   /* blocks until main posts */
	rep.sem_returned = 1;
}

static void *thread_body(void *p)
{
	struct thread_arg *a = p;

	do_read(a->read_fd);
	do_write(a->write_fd);
	do_sem(a->sem);
	return NULL;
}

/* Bounded wait for a flag. A fixed start time with an elapsed comparison: an
 * earlier version recomputed an absolute deadline inside the loop, so the
 * comparison was zero on the first pass and it gave up after one 20ms poll —
 * while the thread went on to set the very flags it had given up on, and the
 * per-line verdicts and the summary ended up disagreeing. */
static int wait_flag(volatile int *flag)
{
	time_t started = time(NULL);
	struct timespec nap;

	for (;;) {
		if (*flag) return 1;
		nap.tv_sec = 0;
		nap.tv_nsec = 20000000L;    /* 20ms */
		if (nanosleep(&nap, NULL) < 0 && errno != EINTR) return 0;
		if (time(NULL) - started >= WAIT_DEADLINE_SEC) return 0;
	}
}

static void say(const char *who, const char *what, int ok)
{
	printf("  %-6s %-12s %s\n", who, what, ok ? "RETURNED" : "did not return");
	fflush(stdout);
}

/* Fill a pipe so that a write to it must block. PIPE_BUF is the atomic unit;
 * writing more than the buffer holds is what makes write(2) wait. */
static int fill(int fd)
{
	static char buf[PIPE_FILL];
	ssize_t w = write(fd, buf, sizeof(buf));
	return w > 0;
}

int main(void)
{
	int rpipe[2], wpipe[2];
	sem_t sem;
	pthread_t th;
	struct thread_arg arg;
	char byte = 'x';
	int main_rd, main_wr, main_sm;

	setvbuf(stdout, NULL, _IONBF, 0);

	puts("guest threads under mldr: does a blocking wait ever come back?");
	puts("  three kinds of wait, main thread as control, spawned thread as test");
	puts("");

	/* ---------- control: the main thread, all three ---------- */
	puts("[control] main thread:");
	if (pipe(rpipe) < 0 || pipe(wpipe) < 0) { perror("pipe"); return 2; }
	sem_init(&sem, 0, 0);
	memset(&rep, 0, sizeof(rep));

	/* This thread plays both sides here, which is the point: the MAIN thread's
	 * blocking calls all come back, and that is the baseline the lane below is
	 * measured against. Its results are printed, not assumed — an earlier
	 * draft printed a hardcoded "all passed", which would have reported a
	 * baseline nobody observed. */
	if (write(rpipe[1], &byte, 1) != 1) { perror("write"); return 2; }
	do_read(rpipe[0]);
	say("main", "read(pipe)", rep.read_returned);

	if (!fill(wpipe[1])) { perror("fill"); return 2; }
	do_write(wpipe[1]);
	say("main", "write(full)", rep.write_returned);
	{
		char drain[PIPE_FILL];
		if (read(wpipe[0], drain, sizeof(drain)) <= 0) perror("drain");
	}

	sem_post(&sem);
	do_sem(&sem);
	say("main", "sem_wait", rep.sem_returned);

	main_rd = rep.read_returned;
	main_wr = rep.write_returned;
	main_sm = rep.sem_returned;

	close(rpipe[0]); close(rpipe[1]);
	close(wpipe[0]); close(wpipe[1]);
	sem_destroy(&sem);
	puts("");

	/* ---------- the lane: a spawned thread, all three ---------- */
	puts("[lane] spawned guest thread:");
	memset(&rep, 0, sizeof(rep));
	if (pipe(rpipe) < 0 || pipe(wpipe) < 0) { perror("pipe"); return 2; }
	sem_init(&sem, 0, 0);
	arg.read_fd = rpipe[0];      /* thread reads, main writes */
	arg.write_fd = wpipe[1];     /* thread writes, main drains */
	arg.sem = &sem;

	/* The thread's write must block, so this pipe is full before it starts. */
	if (!fill(wpipe[1])) { perror("fill"); return 2; }

	if (pthread_create(&th, NULL, thread_body, &arg) != 0) {
		puts("  FATAL: pthread_create failed, the lane cannot be measured here");
		return 2;
	}

	/* release, then wait, in that order, for every leg */
	if (write(rpipe[1], &byte, 1) != 1) perror("write");
	say("thread", "read(pipe)", wait_flag(&rep.read_returned));

	{
		char drain[PIPE_FILL];
		if (read(wpipe[0], drain, sizeof(drain)) <= 0) perror("drain");
	}
	say("thread", "write(full)", wait_flag(&rep.write_returned));

	sem_post(&sem);
	say("thread", "sem_wait", wait_flag(&rep.sem_returned));

	puts("");
	puts("VERDICT");
	if (rep.read_returned && rep.write_returned && rep.sem_returned) {
		puts("  (none) every blocking wait came back on the spawned thread, so the");
		puts("        window probe's stuck roundtrip is NOT a general guest-thread");
		puts("        problem. It is specific to what that roundtrip waits on.");
	} else if (!rep.read_returned && !rep.write_returned && !rep.sem_returned) {
		puts("  (A) not ONE of the three comes back on a spawned guest thread.");
		puts("      This is a GENERAL guest-thread problem under mldr -- scheduling,");
		puts("      signal delivery, TLS or stack -- and it is not about Wayland.");
	} else if (rep.sem_returned && !(rep.read_returned && rep.write_returned)) {
		puts("  (B) the lock comes back and the descriptor waits do not, so the");
		puts("      failure is in the DESCRIPTOR/EVENT path, not in resumption.");
	} else {
		puts("  (partial) a mixed answer, which is itself the finding:");
		printf("        read=%d write=%d sem=%d\n",
		       rep.read_returned, rep.write_returned, rep.sem_returned);
	}
	printf("\nmain thread control: read=%d write=%d sem=%d\n",
	       main_rd, main_wr, main_sm);
	if (!main_rd || !main_wr || !main_sm)
		puts("  WARNING: the main thread control did not pass everything, so the"
		     "\n           lane above is not being compared against a clean"
		     "\n           baseline and its result means less.");

	close(rpipe[0]); close(rpipe[1]);
	close(wpipe[0]); close(wpipe[1]);
	sem_destroy(&sem);
	pthread_detach(th);
	return 0;
}
