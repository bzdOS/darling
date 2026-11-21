/* sig-reentry-probe.c - guest stress for asynchronous signal 65 (thread_hold).
 *
 *   worker   : tight loop of emulated syscalls and darlingserver RPCs
 *              (getpid, mach_port_allocate/deallocate, semaphore signal/wait,
 *              mach_msg to itself, pthread mutex).
 *   suspender: thread_suspend()/thread_resume() on the worker, N times. Each
 *              suspend is darlingserver thread_hold(), i.e. signal 65 sent to
 *              the worker thread.
 *   spawner  : pthread_create/pthread_join of short-lived threads in a loop
 *              (thread start runs arch_prctl(ARCH_SET_GS) under the trap handler).
 *
 * usage: sig-reentry-probe [suspends] [spawns] [idle_worker]   (defaults 3000, 300, 0)
 * Prints PROBE-OK when all three finish; the watchdog prints PROBE-HANG and exits 3
 * when any still-running role (worker, suspender, spawner) makes no progress for 20 s. A crash is the process dying. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <mach/mach.h>
#include <mach/semaphore.h>
#include <mach/task.h>
#include <time.h>

/* role -> ids at thread start, so the server's per-thread ring table (host tid) can be matched exactly:
 * ROLE <name> threadid=<pthread_threadid_np> port=<mach_thread_self>; the ring's first kind-8 event of each
 * host tid follows the ROLE line's time order. */
static double now_s(void) { struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts); return ts.tv_sec + ts.tv_nsec / 1e9; }
static void role(const char *name)
{
	uint64_t id = 0;
	pthread_threadid_np(NULL, &id);
	mach_port_t p = mach_thread_self();
	printf("ROLE %s threadid=%llu port=0x%x t=%.3f\n", name, (unsigned long long)id, p, now_s());
	mach_port_deallocate(mach_task_self(), p);
}

static volatile int stop;
static volatile unsigned long progress;
static mach_port_t worker_port;
static volatile int worker_ready;
static int nsusp = 3000, nspawn = 300;
static volatile unsigned long susp_rounds, spawns_done; /* liveness of the other two roles */
static volatile int susp_finished, spawn_finished;
static volatile const char *susp_last = "start"; /* suspender: last call entered */
static int idle_worker; /* argv[3]=1: the worker only sleeps (no syscall storm) */

static void *worker(void *a)
{
	semaphore_t sem;
	role("worker");
	pthread_mutex_t mu = PTHREAD_MUTEX_INITIALIZER;
	semaphore_create(mach_task_self(), &sem, SYNC_POLICY_FIFO, 0);
	worker_port = mach_thread_self();
	worker_ready = 1;
	while (!stop) {
		if (idle_worker) { usleep(2000); progress++; continue; }
		mach_port_t p = MACH_PORT_NULL;
		if (mach_port_allocate(mach_task_self(), MACH_PORT_RIGHT_RECEIVE, &p) == KERN_SUCCESS)
			mach_port_deallocate(mach_task_self(), p);
		semaphore_signal(sem);
		semaphore_wait(sem);
		pthread_mutex_lock(&mu);
		(void)getpid();
		pthread_mutex_unlock(&mu);
		mach_port_t t = mach_thread_self();
		mach_port_deallocate(mach_task_self(), t);
		progress++;
		if ((progress % 2000) == 0) printf("worker progress=%lu t=%.3f\n", progress, now_s());
	}
	return NULL;
}

static void *suspender(void *a)
{
	role("suspender");
	while (!worker_ready) usleep(1000);
	for (int i = 0; i < nsusp; i++) {
		susp_last = "thread_suspend";
		kern_return_t kr = thread_suspend(worker_port);
		if (kr != KERN_SUCCESS) { printf("thread_suspend kr=%d at %d\n", kr, i); break; }
		susp_last = "thread_resume";
		kr = thread_resume(worker_port);
		susp_last = "between";
		susp_rounds = i + 1;
		if (kr != KERN_SUCCESS) { printf("thread_resume kr=%d at %d\n", kr, i); break; }
		if ((i % 500) == 0) printf("suspend round %d progress=%lu last=%s t=%.3f\n", i, progress, susp_last, now_s());
	}
	susp_last = "done";
	susp_finished = 1;
	printf("suspender done rounds=%lu t=%.3f\n", susp_rounds, now_s());
	return NULL;
}

static void *empty(void *a) { return a; }

static void *spawner(void *a)
{
	role("spawner");
	for (int i = 0; i < nspawn; i++) {
		pthread_t t;
		if (pthread_create(&t, NULL, empty, NULL) != 0) { printf("pthread_create failed at %d\n", i); break; }
		pthread_join(t, NULL);
		spawns_done = i + 1;
	}
	spawn_finished = 1;
	printf("spawner done spawns=%lu t=%.3f\n", spawns_done, now_s());
	return NULL;
}

/* Watches all three roles, by wall time (sleep(1) can take ~4 s under debug logging): a role that is still
 * running and has not advanced for 20 s is a hang. Before, only the worker was watched, so a stuck spawner
 * or suspender with a live worker ran into the outer timeout without PROBE-HANG. */
static void *watchdog(void *a)
{
	role("watchdog");
	unsigned long lw = progress, ls = susp_rounds, lp = spawns_done;
	double tw = now_s(), ts = tw, tp = tw;
	while (!stop) {
		sleep(1);
		double t = now_s();
		if (progress != lw) { lw = progress; tw = t; }
		if (susp_rounds != ls || susp_finished) { ls = susp_rounds; ts = t; }
		if (spawns_done != lp || spawn_finished) { lp = spawns_done; tp = t; }
		const char *who = t - tw >= 20 ? "worker" : t - ts >= 20 ? "suspender" : t - tp >= 20 ? "spawner" : NULL;
		if (who) {
			printf("PROBE-HANG: no %s progress for 20 s (progress=%lu susp_rounds=%lu susp_last=%s spawns=%lu t=%.3f)\n",
			       who, progress, susp_rounds, (const char *)susp_last, spawns_done, t);
			_exit(3);
		}
	}
	return NULL;
}

int main(int argc, char **argv)
{
	setbuf(stdout, NULL);
	if (argc > 1) nsusp = atoi(argv[1]);
	if (argc > 2) nspawn = atoi(argv[2]);
	if (argc > 3) idle_worker = atoi(argv[3]);
	pthread_t w, s, sp, wd;
	pthread_create(&w, NULL, worker, NULL);
	pthread_create(&wd, NULL, watchdog, NULL);
	pthread_create(&s, NULL, suspender, NULL);
	pthread_create(&sp, NULL, spawner, NULL);
	pthread_join(s, NULL);
	pthread_join(sp, NULL);
	stop = 1;
	pthread_join(w, NULL);
	printf("sig-reentry-probe: suspends=%d spawns=%d progress=%lu\n", nsusp, nspawn, progress);
	printf("PROBE-OK\n");
	return 0;
}
