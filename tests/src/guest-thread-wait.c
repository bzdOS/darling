/* guest-thread-wait.c — does a spawned guest thread EVER come back from a
 * blocking wait?
 *
 * WHY THIS EXISTS
 * ---------------
 * A window probe found that a blocking Wayland roundtrip issued from a spawned
 * guest thread never returns, while the same call on the main thread returns
 * promptly (roundtrip returned 3, wl_display_get_error = 0). One connection,
 * one call, two threads, two answers.
 *
 * That leaves the question this file exists to answer, and the question is not
 * "is Wayland broken". It is: **is the failure specific to the event path, or
 * do guest threads under mldr fail to return from ANY blocking wait?** The two
 * have very different fixes and very different amounts of work behind them, so
 * the classification has to be measured rather than assumed from the one
 * symptom.
 *
 * So: three kinds of blocking wait, each run TWICE — once on the main thread as
 * a control, once on a spawned thread. The three are chosen to separate the
 * mechanisms rather than to repeat the same test:
 *
 *   1. nanosleep   a timer. No descriptor, no lock, nothing to wake. If this
 *                  fails, the thread is not being resumed at all.
 *   2. read(pipe)  a descriptor wait. The main thread writes; the thread must
 *                  come back because of another thread's action.
 *   3. sem_wait    a lock wait. The main thread posts; same shape as the pipe
 *                  but through the semaphore path.
 *
 * The outcome is read off the three results:
 *
 *   (A) none of the three returns on the thread  -> a general problem with
 *       guest threads under mldr: scheduling, signal delivery, TLS or stack.
 *   (B) the timer and the lock return, only the descriptor wait does not -> the
 *       failure is in the descriptor/event path, not in thread resumption.
 *
 * NOTHING HERE IS FIXED, ONLY MEASURED. And this deliberately does NOT touch
 * the parked question about get_perthread_wd reading %gs:(,0xc9*8): two facts
 * about threads in one run are two facts, and a shared %gs is a hypothesis, not
 * a link.
 *
 * NO ROOT AND NO SEAT. There is no window, no compositor and no Wayland in this
 * file, so the run needs neither. Every wait is bounded: the joins poll with a
 * deadline and the program prints a verdict and exits rather than sitting in a
 * syscall, because a probe that hangs cannot report that it hung.
 */

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <semaphore.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define WAIT_DEADLINE_SEC 3

/* What the spawned thread last managed to do. Written by the thread, read by
 * the main thread after a bounded wait; volatile because it is a flag shared
 * across threads and nothing here needs more than that. */
struct thread_report {
	volatile int nanosleep_returned;
	volatile int read_returned;
	volatile int sem_returned;
	long nanosleep_elapsed_ms;
};

static struct thread_report rep;

/* --- the three waits, each a plain function so the thread and the main thread
 * --- run the SAME code and the comparison is like for like. ------------- */

static void do_nanosleep(long ms)
{
	struct timespec ts;
	struct timespec start, end;

	ts.tv_sec = ms / 1000;
	ts.tv_nsec = (ms % 1000) * 1000000L;
	clock_gettime(CLOCK_MONOTONIC, &start);
	nanosleep(&ts, NULL);
	clock_gettime(CLOCK_MONOTONIC, &end);
	rep.nanosleep_elapsed_ms = (end.tv_sec - start.tv_sec) * 1000 +
	                           (end.tv_nsec - start.tv_nsec) / 1000000;
	rep.nanosleep_returned = 1;
}

static void do_read(int fd)
{
	char c;
	ssize_t n = read(fd, &c, 1);   /* blocks until the main thread writes */
	rep.read_returned = (n == 1) ? 1 : 0;
}

static void do_sem(sem_t *s)
{
	sem_wait(s);                   /* blocks until the main thread posts */
	rep.sem_returned = 1;
}

/* --- the spawned-thread body. It runs all three in order; each one that
 * --- returns sets its own flag, so a partial answer is still an answer and is
 * --- the more interesting one. ----------------------------------------- */

struct thread_arg {
	int pipefd;
	sem_t *sem;
};

static void *thread_body(void *p)
{
	struct thread_arg *a = p;

	do_nanosleep(200);
	do_read(a->pipefd);
	do_sem(a->sem);
	return NULL;
}

/* Bounded wait for a flag, so a thread that never returns costs seconds and
 * not the run.
 *
 * The deadline is a fixed START time and the loop compares elapsed time
 * against it. An earlier version recomputed an absolute deadline inside the
 * loop, which made the elapsed comparison zero on the first pass and gave up
 * after one poll — while the flags it had given up on went on to be set by the
 * thread a moment later. The result was a report whose per-line verdicts and
 * whose summary disagreed, and it disagreed in the direction of looking like a
 * defect. Running this natively is what caught it. */
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

int main(void)
{
	int fds[2];
	sem_t sem;
	pthread_t th;
	struct thread_arg arg;
	struct timespec ms200;
	char byte = 'x';
	int thread_ok;
	int main_ns, main_rd, main_sm;

	setvbuf(stdout, NULL, _IONBF, 0);

	puts("guest threads under mldr: does a blocking wait ever come back?");
	puts("  three kinds of wait, main thread as control, spawned thread as test");
	puts("");

	/* ---------- control: the main thread, all three ---------- */
	puts("[control] main thread:");
	if (pipe(fds) < 0) { perror("pipe"); return 2; }
	sem_init(&sem, 0, 0);
	memset(&rep, 0, sizeof(rep));

	/* the writer is this thread itself: write first, so the read cannot be
	 * the thing that blocks forever — the point is that the MAIN thread's
	 * blocking calls all come back, which is the baseline the thread lane is
	 * measured against. */
	if (write(fds[1], &byte, 1) != 1) { perror("write"); return 2; }
	do_nanosleep(200);
	say("main", "nanosleep", rep.nanosleep_returned);
	printf("  %-6s %-12s %.0f ms elapsed\n", "main", "(timing)", (double)rep.nanosleep_elapsed_ms);

	do_read(fds[0]);
	say("main", "read(pipe)", rep.read_returned);

	sem_post(&sem);
	do_sem(&sem);
	say("main", "sem_wait", rep.sem_returned);

	/* Keep the control's real results. Printing a hardcoded "all passed" here
	 * would report a baseline that was never observed, and the whole lane is
	 * measured against this baseline. */
	main_ns = rep.nanosleep_returned;
	main_rd = rep.read_returned;
	main_sm = rep.sem_returned;

	close(fds[0]);
	close(fds[1]);
	sem_destroy(&sem);
	puts("");

	/* ---------- the lane: a spawned thread, all three ---------- */
	puts("[lane] spawned guest thread:");
	memset(&rep, 0, sizeof(rep));
	if (pipe(fds) < 0) { perror("pipe"); return 2; }
	sem_init(&sem, 0, 0);
	arg.pipefd = fds[0];
	arg.sem = &sem;

	if (pthread_create(&th, NULL, thread_body, &arg) != 0) {
		puts("  FATAL: pthread_create failed, the lane cannot be measured here");
		return 2;
	}

	/* Give the thread a moment to reach its first wait, then let it go one
	 * at a time. nanosleep needs nothing from us; the pipe and the semaphore
	 * each need exactly one thing, and both are posted below only after their
	 * flag has been given its chance to appear. */
	ms200.tv_sec = 0;
	ms200.tv_nsec = 150000000L;   /* 150ms */
	nanosleep(&ms200, NULL);

	thread_ok = wait_flag(&rep.nanosleep_returned);
	say("thread", "nanosleep", thread_ok);
	if (thread_ok)
		printf("  %-6s %-12s %.0f ms elapsed\n", "thread", "(timing)",
		       (double)rep.nanosleep_elapsed_ms);

	if (write(fds[1], &byte, 1) != 1) perror("write");
	say("thread", "read(pipe)", wait_flag(&rep.read_returned));

	sem_post(&sem);
	say("thread", "sem_wait", wait_flag(&rep.sem_returned));

	puts("");
	puts("VERDICT");
	if (rep.nanosleep_returned && rep.read_returned && rep.sem_returned) {
		puts("  (none) every blocking wait came back on the spawned thread, so the");
		puts("        window probe's stuck roundtrip is NOT a general guest-thread");
		puts("        problem. It is specific to what that roundtrip waits on.");
	} else if (!rep.nanosleep_returned) {
		puts("  (A) even a plain timer does not come back on a spawned guest thread.");
		puts("      This is a GENERAL guest-thread problem under mldr -- scheduling,");
		puts("      signal delivery, TLS or stack -- and it is not about Wayland.");
	} else if (rep.nanosleep_returned && !rep.read_returned) {
		puts("  (B) the timer comes back and the lock does not, only the descriptor");
		puts("      wait is stuck: the failure is in the DESCRIPTOR/EVENT path, not in");
		puts("        thread resumption.");
	} else {
		puts("  (partial) a mixed answer, which is itself the finding:");
		printf("        nanosleep=%d read=%d sem=%d\n",
		       rep.nanosleep_returned, rep.read_returned, rep.sem_returned);
	}
	printf("\nmain thread control: nanosleep=%d read=%d sem=%d\n",
	       main_ns, main_rd, main_sm);
	if (!main_ns || !main_rd || !main_sm)
		puts("  WARNING: the main thread control did not pass everything, so the"
		     "\n           lane below is not being compared against a clean"
		     "\n           baseline and its result means less.");

	close(fds[0]);
	close(fds[1]);
	sem_destroy(&sem);
	return 0;
}
