/* psynch-ring-probe.c - drives contended pthread_rwlock paths on known addresses so the psynch ring (server env
 * DSERVER_PSYNCH_RING=1) and the wait-queue labels in the dsdump can be checked.
 * Phase 1: N times a reader waiter and N times a writer waiter against a held write lock (every round trips the
 *          kernel: rdlock/wrlock wait, unlock). Prints "PSRING-ADDR addr=0x.. n=N".
 * Phase 2: main holds the write lock, one thread waits in rdlock, one in wrlock; prints
 *          "RWLABEL-WAIT addr=0x.. holder=T rd=T wr=T" (guest thread ids) and holds for HOLD seconds so the
 *          runner can take a dump, then releases everything. Exit 0 / PROBE-OK. Verdicts come from psynch-ring-run.sh. */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>

#define N 100
#define HOLD 15

static pthread_rwlock_t rw1 = PTHREAD_RWLOCK_INITIALIZER;
static pthread_rwlock_t rw2 = PTHREAD_RWLOCK_INITIALIZER;
static volatile int req, done, quit;
static volatile unsigned long long rd_tid, wr_tid;

static unsigned long long my_tid(void) { uint64_t t = 0; pthread_threadid_np(NULL, &t); return t; }

static void *helper(void *a) {
    (void)a;
    while (!quit) {
        if (!req) { usleep(200); continue; }
        int m = req; req = 0;
        if (m == 1) pthread_rwlock_rdlock(&rw1); else pthread_rwlock_wrlock(&rw1);
        pthread_rwlock_unlock(&rw1);
        done = 1;
    }
    return NULL;
}

static void *rd_waiter(void *a) { (void)a; rd_tid = my_tid(); pthread_rwlock_rdlock(&rw2); pthread_rwlock_unlock(&rw2); return NULL; }
static void *wr_waiter(void *a) { (void)a; wr_tid = my_tid(); pthread_rwlock_wrlock(&rw2); pthread_rwlock_unlock(&rw2); return NULL; }

int main(void) {
    setbuf(stdout, NULL);
    pthread_t h, r, w;
    if (pthread_create(&h, NULL, helper, NULL)) { printf("PROBE-FAIL psynch-ring (thread)\n"); return 1; }
    printf("PSRING-ADDR addr=%p n=%d\n", (void *)&rw1, N);
    for (int mode = 1; mode <= 2; mode++) {
        for (int i = 0; i < N; i++) {
            done = 0;
            pthread_rwlock_wrlock(&rw1);
            req = mode;
            usleep(3000);               /* let the helper block in the kernel */
            pthread_rwlock_unlock(&rw1);
            for (int k = 0; !done && k < 20000; k++) usleep(200);
        }
    }
    quit = 1;
    pthread_join(h, NULL);
    pthread_rwlock_wrlock(&rw2);
    pthread_create(&r, NULL, rd_waiter, NULL);
    usleep(300000);
    pthread_create(&w, NULL, wr_waiter, NULL);
    usleep(500000);
    printf("RWLABEL-WAIT addr=%p holder=%llu rd=%llu wr=%llu\n", (void *)&rw2, my_tid(), rd_tid, wr_tid);
    sleep(HOLD);
    pthread_rwlock_unlock(&rw2);
    pthread_join(r, NULL);
    pthread_join(w, NULL);
    printf("PROBE-OK psynch-ring\n");
    return 0;
}
