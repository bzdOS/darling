/* suspend-rwlock-probe.c - a reader blocked in the kernel (psynch_rw_rdlock) is thread_suspend()ed by another thread, as a
 * sampler does. One rwlock (one address, so the server's psynch ring, DSERVER_PSYNCH_RING=1, can be read per address).
 *   writer A  : loop { wrlock; win++ ; short spin; win-- ; unlock; short pause }      (keeps B blocking in the kernel)
 *   reader B  : loop { rc = pthread_rwlock_rdlock; if rc==0 check win==0 (twice, across a spin); unlock }
 *   suspender X: loop { thread_suspend(B); short delay; thread_resume(B); pause }     (Mach API, like a sampler)
 * Guest side, per iteration: the rdlock return code (rc != 0 counted as rdfail) and the invariant "rdlock returned 0 =>
 * no writer inside" (a writer inside while B holds the read lock = invariant_broken). The EINTR of the interrupted
 * __psynch_rw_rdlock is not visible from pthread_rwlock_rdlock (libpthread retries it or, if the call comes back as a
 * success, swallows it), so eintr and retry_same_seq are read by the runner from the server's psynch ring.
 * A watchdog stops the probe when B makes no progress for HANG_S seconds (hung=1); the probe then holds HOLD seconds for the dump.
 * Env: SR_SECS (default 20, run length), SR_HANG_S (default 10), SR_SUSP_US (suspend hold, default 200), SR_GAP_US (gap between
 * X's resume and next suspend, default 500).
 * Prints:  SUSPEND-SUMMARY iters=N rdfail=F invariant_broken=I hung=H suspends=S addr=0x..
 * Exit 0. The verdict comes from suspend-rwlock-run.sh. */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <mach/mach.h>
#include <mach/thread_act.h>

#define HOLD 25

static pthread_rwlock_t lk;
static volatile int win, stop_all, b_ready, b_done;
static volatile long b_iters, rdfail, inv_broken, suspends;
static volatile int first_rc;
static mach_port_t b_port;

static int envi(const char *n, int d) { const char *v = getenv(n); return (v && *v) ? atoi(v) : d; }
static void spin(int n) { for (volatile int i = 0; i < n; i++) ; }

static void *writer(void *p) {
    (void)p;
    unsigned r = 12345;
    int whold = 3 * envi("SR_SUSP_US", 200) + 300;
    while (!stop_all) {
        pthread_rwlock_wrlock(&lk);
        __sync_fetch_and_add(&win, 1);
        usleep(whold);          /* inside longer than the longest suspension (SR_SUSP_US), so an early return is seen */
        __sync_fetch_and_sub(&win, 1);
        pthread_rwlock_unlock(&lk);
        r = r * 1103515245 + 12345;
        usleep(20 + (r >> 16) % 200);
    }
    return NULL;
}

static void *reader(void *p) {
    (void)p;
    b_port = mach_thread_self();
    b_ready = 1;
    while (!stop_all) {
        int rc = pthread_rwlock_rdlock(&lk);
        if (rc != 0) {
            rdfail++;
            if (!first_rc) first_rc = rc;
            b_iters++;
            continue;                       /* did not take the lock: no unlock */
        }
        int a = win;
        spin(300);
        int b = win;
        if (a != 0 || b != 0) inv_broken++;
        pthread_rwlock_unlock(&lk);
        b_iters++;
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
    pthread_rwlock_init(&lk, NULL);
    pthread_attr_t at;
    pthread_attr_init(&at);
    pthread_attr_setdetachstate(&at, PTHREAD_CREATE_DETACHED);
    pthread_t ta, tb, tx;
    if (pthread_create(&tb, &at, reader, NULL) || pthread_create(&ta, &at, writer, NULL) || pthread_create(&tx, &at, suspender, NULL)) {
        printf("PROBE-FAIL suspend-rwlock (thread)\n");
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
    if (!hung) for (int k = 0; !b_done && k < 5000; k++) usleep(1000);       /* B may be stuck in a blocking call */
    if (!hung && !b_done) hung = 1;
    printf("SUSPEND-SUMMARY iters=%ld rdfail=%ld invariant_broken=%ld hung=%d suspends=%ld first_rc=%d addr=%p\n",
           b_iters, rdfail, inv_broken, hung, suspends, first_rc, (void *)&lk);
    sleep(HOLD);
    printf("PROBE-OK suspend-rwlock\n");
    return 0;
}
