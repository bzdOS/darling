/* cancel-rpc-probe.c: does a Mach trap fail with ECONNREFUSED (-111 in the
 * libsystem_kernel "failed (internally)" message) after pthread_cancel, or
 * under heavy RPC load from many threads?
 * Phase A: CANCEL_N threads block in semaphore_timedwait, get pthread_cancel,
 *          the main thread then does traps on fresh threads.
 * Phase B: LOAD_N threads each do LOAD_ITERS semaphore_timedwait(0 timeout)
 *          + semaphore_signal calls.
 * Output: one line per phase, then PROBE-VERDICT ok|fail. */
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <mach/mach.h>
#include <mach/semaphore.h>
#include <mach/task.h>

static semaphore_t sem;
static volatile int bad;

static void *blocker(void *a) {
	(void)a;
	for (;;) {
		mach_timespec_t t = { 5, 0 };
		kern_return_t kr = semaphore_timedwait(sem, t);
		if (kr != KERN_SUCCESS && kr != KERN_OPERATION_TIMED_OUT && kr != KERN_ABORTED) {
			printf("blocker kr=%d\n", kr); __sync_fetch_and_add(&bad, 1);
		}
		pthread_testcancel();
	}
	return 0;
}

static int iters;
static void *loader(void *a) {
	(void)a;
	for (int i = 0; i < iters; i++) {
		mach_timespec_t t = { 0, 1000 };
		kern_return_t kr = semaphore_timedwait(sem, t);
		if (kr != KERN_SUCCESS && kr != KERN_OPERATION_TIMED_OUT && kr != KERN_ABORTED) {
			printf("loader kr=%d\n", kr); __sync_fetch_and_add(&bad, 1);
		}
		semaphore_signal(sem);
	}
	return 0;
}

int main(void) {
	int cn = getenv("CANCEL_N") ? atoi(getenv("CANCEL_N")) : 16;
	int ln = getenv("LOAD_N") ? atoi(getenv("LOAD_N")) : 64;
	iters = getenv("LOAD_ITERS") ? atoi(getenv("LOAD_ITERS")) : 2000;
	setvbuf(stdout, 0, _IONBF, 0);
	if (semaphore_create(mach_task_self(), &sem, SYNC_POLICY_FIFO, 0) != KERN_SUCCESS) {
		printf("semaphore_create failed\nPROBE-VERDICT fail\n"); return 1;
	}
	for (int round = 0; round < 4; round++) {
		pthread_t th[256];
		for (int i = 0; i < cn; i++) pthread_create(&th[i], 0, blocker, 0);
		for (int i = 0; i < cn; i++) pthread_cancel(th[i]);
		for (int i = 0; i < cn; i++) semaphore_signal(sem);
		for (int i = 0; i < cn; i++) pthread_join(th[i], 0);
		mach_timespec_t t = { 0, 1000 };
		kern_return_t kr = semaphore_timedwait(sem, t);
		if (kr != KERN_SUCCESS && kr != KERN_OPERATION_TIMED_OUT) { printf("main kr=%d\n", kr); bad++; }
	}
	printf("PHASE-A cancel_n=%d bad=%d\n", cn, bad);
	pthread_t lt[512];
	if (ln > 512) ln = 512;
	for (int i = 0; i < ln; i++) pthread_create(&lt[i], 0, loader, 0);
	for (int i = 0; i < ln; i++) pthread_join(lt[i], 0);
	printf("PHASE-B load_n=%d iters=%d bad=%d\n", ln, iters, bad);
	printf("PROBE-VERDICT %s\n", bad ? "fail" : "ok");
	return bad ? 1 : 0;
}
