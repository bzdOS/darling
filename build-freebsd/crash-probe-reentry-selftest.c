/* crash-probe-reentry-selftest.c - a signal handler that probes while the same thread is inside its own probe window.
 * Includes crash_dump.c (the file linked into mldr). A sender thread signals the prober every ~20us (a flood with no gap only starves the prober); the handler calls
 * mldr_dump_read_guarded, as mldr's SIGILL handler does. A watchdog turns a self-deadlock into a FAIL line.
 * argv[1] = iterations. Prints REENTRY-SELFTEST pass|FAIL and exits 0/1. */
#include <pthread.h>
#include <setjmp.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#include "../src/startup/mldr/crash_dump.c"

static volatile int stop;
static volatile long progress, nested;
static uint64_t *good;
static pthread_t prober_t;

static void U(int sig) { volatile uint64_t w; (void)sig; (void)mldr_dump_read_guarded((uintptr_t)good, &w); nested++; }

static void *sender(void *a) { (void)a; while (!stop) { pthread_kill(prober_t, SIGUSR1); usleep(20); } return NULL; }

static void *watchdog(void *a)
{
	long last = -1;
	(void)a;
	for (int i = 0; i < 20 && !stop; i++) {
		sleep(1);
		if (progress == last) {
			printf("REENTRY-SELFTEST FAIL: prober made no progress for 1s at iteration %ld (self-deadlock on the probe lock)\n", progress);
			fflush(stdout);
			_exit(1);
		}
		last = progress;
	}
	return NULL;
}

int main(int argc, char **argv)
{
	long n = argc > 1 ? atol(argv[1]) : 200000, i;
	pthread_t s, w;
	volatile uint64_t v;
	struct sigaction sa;
	good = mmap(NULL, sysconf(_SC_PAGESIZE), PROT_READ | PROT_WRITE, MAP_ANON | MAP_PRIVATE, -1, 0);
	*good = 1;
	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = U; sigemptyset(&sa.sa_mask);
	sigaction(SIGUSR1, &sa, NULL);
	prober_t = pthread_self();
	pthread_create(&w, NULL, watchdog, NULL);
	pthread_create(&s, NULL, sender, NULL);
	for (i = 0; i < n; i++) { (void)mldr_dump_read_guarded((uintptr_t)good, &v); progress = i + 1; }
	stop = 1; pthread_join(s, NULL);
	printf("REENTRY-SELFTEST pass: %ld probes, %ld nested probes from signal handlers, no deadlock\n", n, nested);
	return 0;
}
