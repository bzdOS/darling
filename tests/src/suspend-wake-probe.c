/* suspend-wake-probe.c - lost-wakeup probe for darlingserver's microthread suspend/resume.
 * Thread B waits on a Mach semaphore that thread A signals (ping-pong, A waits for B's answer with a timeout);
 * thread C meanwhile thread_suspend()/thread_resume()s B in a tight loop (user suspension path).
 * A lost wakeup shows up as A's timed wait on B's answer expiring although B was signalled.
 * Phases: plain (no suspender, control), suspend (C active). Env: SWP_ITERS (default 20000 per phase),
 * SWP_TIMEOUT_S (default 5, an expired wait), SWP_MAX_STALLS (default 2, then the probe stops),
 * SWP_PHASE_S (default 40, time cap per phase), SWP_GAP_US (default 100000: pause between C's resume and its next suspend).
 * An expired wait is classified by stopping the suspender and waiting SWP_RECOVER_S (10) more seconds:
 *   answer arrives -> "starved" (B was held in the signal handler by back-to-back suspends; with SWP_GAP_US=0 this is
 *   the expected livelock of the signal-based suspension emulation, not a lost wakeup), no answer -> "stall" (lost wakeup).
 * Output: one line  SUSPEND-WAKE-VERDICT pass|fail n=<round trips> stalls=<lost wakeups> starved=<recovered waits>
 * (phase lines SWP-PHASE ...). Verdict fail only for lost wakeups (stalls>0). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <sched.h>
#include <time.h>
#include <mach/mach.h>
#include <mach/semaphore.h>
#include <mach/thread_act.h>

static semaphore_t sB, sA;
static volatile int stop_all, b_ready;
static mach_port_t b_port;
static volatile long b_count, suspends;

static void *thread_b(void *arg)
{
	b_port = mach_thread_self();
	b_ready = 1;
	while (!stop_all) {
		mach_timespec_t ts = { 1, 0 };
		kern_return_t kr = semaphore_timedwait(sB, ts);
		if (kr == KERN_SUCCESS) { b_count++; semaphore_signal(sA); }
	}
	return NULL;
}

static volatile int suspender_on;
static long gap_us = 100000;
static void *thread_c(void *arg)
{
	while (!stop_all) {
		if (!suspender_on) { usleep(200); continue; }
		if (thread_suspend(b_port) == KERN_SUCCESS) {
			suspends++;
			thread_resume(b_port);
		}
		if (gap_us > 0) usleep(gap_us); else if ((suspends & 7) == 0) sched_yield();
	}
	return NULL;
}

int main(void)
{
	long iters = getenv("SWP_ITERS") ? atol(getenv("SWP_ITERS")) : 20000;
	int tmo = getenv("SWP_TIMEOUT_S") ? atoi(getenv("SWP_TIMEOUT_S")) : 5;
	int maxst = getenv("SWP_MAX_STALLS") ? atoi(getenv("SWP_MAX_STALLS")) : 2;
	int hold_s = getenv("SWP_HOLD_S") ? atoi(getenv("SWP_HOLD_S")) : 0, held = 0;
	int recov = getenv("SWP_RECOVER_S") ? atoi(getenv("SWP_RECOVER_S")) : 10;
	int phase_s = getenv("SWP_PHASE_S") ? atoi(getenv("SWP_PHASE_S")) : 40;
	long n = 0, stalls = 0, starved = 0;
	if (getenv("SWP_GAP_US")) gap_us = atol(getenv("SWP_GAP_US"));
	pthread_t tb, tc;

	if (semaphore_create(mach_task_self(), &sB, SYNC_POLICY_FIFO, 0) != KERN_SUCCESS ||
	    semaphore_create(mach_task_self(), &sA, SYNC_POLICY_FIFO, 0) != KERN_SUCCESS) {
		printf("SUSPEND-WAKE-VERDICT fail n=0 stalls=0 starved=0 (semaphore_create)\n"); return 2;
	}
	pthread_create(&tb, NULL, thread_b, NULL);
	pthread_create(&tc, NULL, thread_c, NULL);
	while (!b_ready) usleep(1000);

	for (int phase = 0; phase < 2 && stalls < maxst; phase++) {
		suspender_on = phase;
		long pn = 0, pst = 0;
		time_t t0 = time(NULL);
		for (long i = 0; i < iters && stalls < maxst && time(NULL) - t0 < phase_s; i++) {
			semaphore_signal(sB);
			mach_timespec_t ts = { tmo, 0 };
			kern_return_t kr = semaphore_timedwait(sA, ts);
			if (kr == KERN_SUCCESS) { n++; pn++; }
			else {
				printf("SWP-EXPIRED phase=%s iter=%ld kr=%d b_count=%ld suspends=%ld\n", phase ? "suspend" : "plain", i, (int)kr, b_count, suspends);
				fflush(stdout);
				suspender_on = 0;
				/* SWP_HOLD_S: wait here (suspender stopped, B still silent) so the stalled state can be inspected from outside */
				if (hold_s > 0 && !held) { held = 1; printf("SWP-HOLD %d s, suspender stopped\n", hold_s); fflush(stdout); sleep(hold_s); }
				mach_timespec_t t2 = { recov, 0 };
				if (semaphore_timedwait(sA, t2) == KERN_SUCCESS) { n++; pn++; starved++; printf("SWP-STARVED answer arrived after the suspender stopped\n"); }
				else { stalls++; pst++; printf("SWP-STALL lost wakeup: b_count=%ld, B never answers with the suspender idle\n", b_count); }
				fflush(stdout);
				if (phase) suspender_on = 1;
			}
		}
		printf("SWP-PHASE %s n=%ld stalls=%ld suspends=%ld\n", phase ? "suspend" : "plain", pn, pst, suspends);
		fflush(stdout);
	}
	suspender_on = 0;
	printf("SUSPEND-WAKE-VERDICT %s n=%ld stalls=%ld starved=%ld\n", stalls ? "fail" : "pass", n, stalls, starved);
	fflush(stdout);
	_exit(stalls ? 1 : 0);   /* B/C may be parked in the very hang being reported */
}
