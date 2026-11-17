/* eintr-prepost-probe.c - a pthread_rwlock waiter blocked in the kernel (psynch_rw_rdlock/wrlock) is interrupted by a
 * signal (handler installed without SA_RESTART), then the holder unlocks after a varying delay. A lock is taken per
 * iteration (distinct address), so the psynch ring (server env DSERVER_PSYNCH_RING=1) can be read per iteration.
 * early = iterations where the waiter returned from the lock call before the holder had unlocked (EINTR escaping as success).
 * lost = iterations where the waiter did not get the lock within TMO_MS after the unlock.
 * Prints: EINTR-LOST i= addr= mode= delay_us=   per lost iteration (first ones)
 *         EINTR-SUMMARY iters=N lost=K early=E sigs=S firstlost=0x.. lastaddr=0x..
 * then holds HOLD seconds so the runner can take a dump. Exit 0. The verdict comes from eintr-prepost-run.sh. */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <pthread.h>

#ifndef N
#define N 300
#endif
#define HOLD 25
#define TMO_MS 1000

static pthread_rwlock_t locks[N];
static volatile int inwait[N], done[N], unl[N], early[N];
static volatile int sigs;
static const int delays[] = { 0, 5, 20, 50, 100, 200, 500, 1000, 2000 };

static void on_sig(int s) { (void)s; sigs++; }

struct arg { int i; int mode; };
static struct arg args[N];

static void *waiter(void *p) {
    struct arg *a = p;
    inwait[a->i] = 1;
    if (a->mode == 1) pthread_rwlock_rdlock(&locks[a->i]); else pthread_rwlock_wrlock(&locks[a->i]);
    if (!unl[a->i]) early[a->i] = 1;     /* returned from the lock call while the holder still held it */
    pthread_rwlock_unlock(&locks[a->i]);
    done[a->i] = 1;
    return NULL;
}

int main(void) {
    setbuf(stdout, NULL);
    struct sigaction sa;
    memset(&sa, 0, sizeof sa);
    sa.sa_handler = on_sig;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;                     /* no SA_RESTART */
    sigaction(SIGUSR1, &sa, NULL);
    pthread_attr_t at;
    pthread_attr_init(&at);
    pthread_attr_setdetachstate(&at, PTHREAD_CREATE_DETACHED);
    int lost = 0, shown = 0, nearly = 0;
    void *firstlost = NULL;
    for (int i = 0; i < N; i++) {
        pthread_rwlock_init(&locks[i], NULL);
        args[i].i = i; args[i].mode = 1 + (i & 1);       /* odd i: writer waiter */
        int d = delays[(i >> 1) % (sizeof delays / sizeof delays[0])];
        pthread_t t;
        pthread_rwlock_wrlock(&locks[i]);
        if (pthread_create(&t, &at, waiter, &args[i])) { printf("PROBE-FAIL eintr-prepost (thread)\n"); return 1; }
        for (int k = 0; !inwait[i] && k < 5000; k++) usleep(200);
        usleep(3000);                    /* let the waiter block in the kernel */
        pthread_kill(t, SIGUSR1);        /* interrupt the wait */
        if (d) usleep(d);
        unl[i] = 1;
        pthread_rwlock_unlock(&locks[i]);
        int k;
        for (k = 0; !done[i] && k < TMO_MS * 5; k++) usleep(200);
        if (early[i]) nearly++;
        if (!done[i]) {
            lost++;
            if (!firstlost) firstlost = &locks[i];
            if (shown++ < 10) printf("EINTR-LOST i=%d addr=%p mode=%s delay_us=%d\n", i, (void *)&locks[i], args[i].mode == 1 ? "rd" : "wr", d);
        }
    }
    printf("EINTR-SUMMARY iters=%d lost=%d early=%d sigs=%d firstlost=%p lastaddr=%p\n", N, lost, nearly, sigs, firstlost, (void *)&locks[N - 1]);
    sleep(HOLD);
    printf("PROBE-OK eintr-prepost\n");
    return 0;
}
