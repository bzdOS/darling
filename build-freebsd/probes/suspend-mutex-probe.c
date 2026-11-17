/* suspend-mutex-probe.c - a thread blocked in the kernel (psynch_mutexwait, pthread_mutex_lock) is thread_suspend()ed by another
 * thread, as a sampler does. One mutex (one address, so the server's psynch ring can be read per address).
 *   holder A   : loop { lock; win++; sleep longer than the longest suspension; win--; unlock; short pause }
 *   waiter B   : loop { rc = pthread_mutex_lock; if rc==0 check win==0 (twice, across a spin); unlock }
 *   suspender X: loop { thread_suspend(B); short delay; thread_resume(B); pause }
 * Invariant: lock returned 0 => no holder inside (a holder inside while B "has" the lock = invariant_broken). The EINTR of the
 * interrupted __psynch_mutexwait is not visible from pthread_mutex_lock, so eintr and retry_same_seq are read by the runner from
 * the server's psynch ring. A watchdog stops the probe when B makes no progress for HANG_S seconds (hung=1).
 * Env: SR_SECS (20), SR_HANG_S (10), SR_SUSP_US (200), SR_GAP_US (500).
 * Prints:  SUSPEND-SUMMARY iters=N rdfail=F invariant_broken=I hung=H suspends=S first_rc=R addr=0x..
 * The verdict comes from suspend-run.sh (SR_KIND=mutex). */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <mach/mach.h>
#include <mach/thread_act.h>

#define HOLD 25

static pthread_mutex_t lk = PTHREAD_MUTEX_INITIALIZER;
static volatile int win, stop_all, b_ready, b_done;
static volatile long b_iters, rdfail, inv_broken, suspends;
static volatile int first_rc;
static mach_port_t b_port;

static int envi(const char *n, int d) { const char *v = getenv(n); return (v && *v) ? atoi(v) : d; }
static void spin(int n) { for (volatile int i = 0; i < n; i++) ; }

static void *holder(void *p) {
    (void)p;
    unsigned r = 12345;
    int whold = 3 * envi("SR_SUSP_US", 200) + 300;
    while (!stop_all) {
        pthread_mutex_lock(&lk);
        __sync_fetch_and_add(&win, 1);
        usleep(whold);
        __sync_fetch_and_sub(&win, 1);
        pthread_mutex_unlock(&lk);
        r = r * 1103515245 + 12345;
        usleep(20 + (r >> 16) % 200);
    }
    return NULL;
}

static void *waiter(void *p) {
    (void)p;
    b_port = mach_thread_self();
    b_ready = 1;
    while (!stop_all) {
        int rc = pthread_mutex_lock(&lk);
        if (rc != 0) {
            rdfail++;
            if (!first_rc) first_rc = rc;
            b_iters++;
            continue;
        }
        int a = win;
        spin(300);
        int b = win;
        if (a != 0 || b != 0) inv_broken++;
        pthread_mutex_unlock(&lk);
        b_iters++;
        usleep(30);                         /* let the holder in, so that B blocks in the kernel on the next lock */
    }
    b_done = 1;
    return NULL;
}

static void *suspender(void *p) {
    (void)p;
    int hold = envi("SR_SUSP_US", 200), gap = envi("SR_GAP_US", 500);
    unsigned r = 777;
    while (!b_ready) usleep(1000);
    while (!stop_all) {
        if (thread_suspend(b_port) != KERN_SUCCESS) { usleep(1000); continue; }
        r = r * 1103515245 + 12345;
        if (hold) usleep((r >> 16) % (hold + 1));
        thread_resume(b_port);
        suspends++;
        r = r * 1103515245 + 12345;
        usleep(gap / 2 + (r >> 16) % (gap + 1));
    }
    return NULL;
}

int main(void) {
    setbuf(stdout, NULL);
    int secs = envi("SR_SECS", 20), hang_s = envi("SR_HANG_S", 10);
    pthread_attr_t at;
    pthread_attr_init(&at);
    pthread_attr_setdetachstate(&at, PTHREAD_CREATE_DETACHED);
    pthread_t ta, tb, tx;
    if (pthread_create(&tb, &at, waiter, NULL) || pthread_create(&ta, &at, holder, NULL) || pthread_create(&tx, &at, suspender, NULL)) {
        printf("PROBE-FAIL suspend-mutex (thread)\n");
        return 1;
    }
    printf("SUSPEND-ADDR addr=%p\n", (void *)&lk);
    int hung = 0;
    long last = -1;
    int still = 0;
    for (int t = 0; t < secs * 10; t++) {
        usleep(100000);
        if (b_iters != last) { last = b_iters; still = 0; } else if (++still >= hang_s * 10) { hung = 1; break; }
        if (t % 50 == 49) printf("SUSPEND-PROGRESS t=%d iters=%ld rdfail=%ld broken=%ld suspends=%ld\n", (t + 1) / 10, b_iters, rdfail, inv_broken, suspends);
    }
    stop_all = 1;
    if (!hung) for (int k = 0; !b_done && k < 5000; k++) usleep(1000);
    if (!hung && !b_done) hung = 1;
    printf("SUSPEND-SUMMARY iters=%ld rdfail=%ld invariant_broken=%ld hung=%d suspends=%ld first_rc=%d addr=%p\n",
           b_iters, rdfail, inv_broken, hung, suspends, first_rc, (void *)&lk);
    sleep(HOLD);
    printf("PROBE-OK suspend-mutex\n");
    return 0;
}
