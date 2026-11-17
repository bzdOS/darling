/* semtw-probe.c - signal a thread parked in semaphore_timedwait, N times, for SIGUSR1 (handler).
 * Thread A: semaphore_timedwait(sem, 2s). Thread B: after a short delay pthread_kill(A, sig).
 * Prints "SEMTW iters=N early=E timedout=T other=O" and PROBE-OK; a server panic shows as a hang / missing line. */
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>
#include <pthread.h>
#include <mach/mach.h>
#include <mach/semaphore.h>
#include <mach/task.h>

#define ITERS 400
static semaphore_t sem;
static volatile pthread_t target;
static volatile int armed, sigsel, hits;
static void h(int s) { hits++; }

static void *killer(void *a) {
    for (int i = 0; i < ITERS; i++) {
        while (!armed) usleep(200);
        usleep((i % 9) * 500);
        pthread_kill(target, sigsel ? SIGABRT : SIGUSR1);
        armed = 0;
    }
    return NULL;
}

int main(void) {
    setbuf(stdout, NULL);
    struct sigaction sa = {0};
    sa.sa_handler = h;
    sigaction(SIGUSR1, &sa, NULL);
    sigaction(SIGABRT, &sa, NULL);
    if (semaphore_create(mach_task_self(), &sem, SYNC_POLICY_FIFO, 0) != KERN_SUCCESS) { printf("SEMTW fail create\n"); return 1; }
    pthread_t k;
    pthread_create(&k, NULL, killer, NULL);
    target = pthread_self();
    int early = 0, to = 0, other = 0, n = 0;
    for (int s = 0; s < 1; s++) {
        sigsel = s;
        for (int i = 0; i < ITERS; i++) {
            mach_timespec_t ts = { 2, 0 };
            armed = 1;
            kern_return_t kr = semaphore_timedwait(sem, ts);
            n++;
            if (kr == KERN_ABORTED) early++;
            else if (kr == KERN_OPERATION_TIMED_OUT) to++;
            else other++;
            while (armed) usleep(200);
            if (n % 50 == 0) printf("SEMTW progress n=%d kr=0x%x\n", n, (unsigned)kr);
        }
    }
    pthread_join(k, NULL);
    printf("SEMTW iters=%d early=%d timedout=%d other=%d hits=%d\n", n, early, to, other, hits);
    printf("PROBE-OK semtw\n");
    return 0;
}
