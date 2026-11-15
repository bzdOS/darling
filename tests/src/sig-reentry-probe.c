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
 * when the worker makes no progress for 20 s. A crash is the process dying. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <mach/mach.h>
#include <mach/semaphore.h>
#include <mach/task.h>

static volatile int stop;
static volatile unsigned long progress;
static mach_port_t worker_port;
static volatile int worker_ready;
static int nsusp = 3000, nspawn = 300;
static int idle_worker; /* argv[3]=1: the worker only sleeps (no syscall storm) */

static void *worker(void *a)
{
	semaphore_t sem;
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
	}
	return NULL;
}

static void *suspender(void *a)
{
	while (!worker_ready) usleep(1000);
	for (int i = 0; i < nsusp; i++) {
		kern_return_t kr = thread_suspend(worker_port);
		if (kr != KERN_SUCCESS) { printf("thread_suspend kr=%d at %d\n", kr, i); break; }
		kr = thread_resume(worker_port);
		if (kr != KERN_SUCCESS) { printf("thread_resume kr=%d at %d\n", kr, i); break; }
		if ((i % 500) == 0) printf("suspend round %d progress=%lu\n", i, progress);
	}
	return NULL;
}

static void *empty(void *a) { return a; }

static void *spawner(void *a)
{
	for (int i = 0; i < nspawn; i++) {
		pthread_t t;
		if (pthread_create(&t, NULL, empty, NULL) != 0) { printf("pthread_create failed at %d\n", i); break; }
		pthread_join(t, NULL);
	}
	return NULL;
}

static void *watchdog(void *a)
{
	unsigned long last = 0;
	int idle = 0;
	while (!stop) {
		sleep(1);
		if (progress == last) idle++; else { idle = 0; last = progress; }
		if (idle >= 20) { printf("PROBE-HANG: no worker progress for 20 s (progress=%lu)\n", progress); _exit(3); }
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
