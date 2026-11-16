/* crash-probe-race-selftest.c - a genuine fault on one thread while another thread runs guarded reads.
 * Includes crash_dump.c (the file linked into mldr). The "previous handler" H counts the faults it sees and jumps back.
 * argv[1] = iterations. Prints RACE-SELFTEST pass|FAIL and exits 0/1; death by signal means FAIL (the sh wrapper reports it). */
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

static sigjmp_buf main_jmp;
static volatile long hits;
static volatile int stop;
static uint64_t *good;
static char *none;

static void H(int sig, siginfo_t *si, void *uc) { (void)sig; (void)si; (void)uc; hits++; siglongjmp(main_jmp, 1); }

static void *prober(void *a)
{
	volatile uint64_t w;
	(void)a;
	while (!stop) {
		(void)mldr_dump_read_guarded((uintptr_t)good, &w);
		(void)mldr_dump_read_guarded((uintptr_t)none, &w);	/* PROT_NONE: takes the probe fault */
	}
	return NULL;
}

int main(int argc, char **argv)
{
	long n = argc > 1 ? atol(argv[1]) : 20000, i;
	struct sigaction sa, cur;
	pthread_t t;
	long ps = sysconf(_SC_PAGESIZE);
	char *m = mmap(NULL, 2 * ps, PROT_READ | PROT_WRITE, MAP_ANON | MAP_PRIVATE, -1, 0);
	good = (uint64_t *)m; *good = 1;
	none = m + ps; mprotect(none, ps, PROT_NONE);
	memset(&sa, 0, sizeof(sa));
	sa.sa_sigaction = H; sa.sa_flags = SA_SIGINFO; sigemptyset(&sa.sa_mask);
	sigaction(SIGSEGV, &sa, NULL);
	pthread_create(&t, NULL, prober, NULL);
	for (i = 0; i < n; i++) {
		if (sigsetjmp(main_jmp, 1) == 0)
			*(volatile int *)0x10 = 1;		/* genuine fault on this thread */
	}
	stop = 1; pthread_join(t, NULL);
	sigaction(SIGSEGV, NULL, &cur);
	if (hits != n) { printf("RACE-SELFTEST FAIL: previous handler saw %ld of %ld genuine faults\n", hits, n); return 1; }
	if (!(cur.sa_flags & SA_SIGINFO) || cur.sa_sigaction != H) { printf("RACE-SELFTEST FAIL: SIGSEGV disposition not restored (probe handler left installed)\n"); return 1; }
	printf("RACE-SELFTEST pass: %ld/%ld genuine faults reached the previous handler during concurrent probes, disposition restored\n", hits, n);
	return 0;
}
