/* suspend-cv-probe.c - a thread blocked in the kernel (psynch_cvwait, pthread_cond_wait) is thread_suspend()ed by another thread,
 * as a sampler does. One mutex + one condition variable (one cv address, so the server's psynch ring can be read per address).
 *   signaller A: loop { lock; ticket++ (flag=1); cond_signal; unlock; wait until B consumed it }
 *   waiter B   : loop { lock; while (!flag) cond_wait; flag=0; consumed++; unlock }
 *   suspender X: loop { thread_suspend(B); short delay; thread_resume(B); pause }
 * libpthread turns the EINTR of an interrupted __psynch_cvwait into a spurious wakeup (no repeat of the call), which the
 * predicate loop absorbs; what must not happen is a lost signal. Invariant: a signal that was sent while B waits is consumed
 * within LOST_MS (A re-signals after that and counts one invariant_broken per late signal); the watchdog (hung=1) stops the
 * probe when B makes no progress for HANG_S seconds. spurious (wakeups without the predicate) is informational only.
 * The suspension is held across the signal: A signals while B is suspended with probability, since the gap/hold are short.
 * Env: SR_SECS (20), SR_HANG_S (10), SR_SUSP_US (200), SR_GAP_US (500), SR_LOST_MS (1000).
 * Prints:  SUSPEND-SUMMARY iters=N rdfail=0 invariant_broken=I hung=H suspends=S spurious=P first_rc=0 addr=0x..
 * The verdict comes from suspend-run.sh (SR_KIND=cv). */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <mach/mach.h>
#include <mach/thread_act.h>

#define HOLD 25

static pthread_mutex_t mx = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t cv = PTHREAD_COND_INITIALIZER;
static volatile int flag, stop_all, b_ready, b_done;
static volatile long b_iters, inv_broken, suspends, spurious;
static mach_port_t b_port;

static int envi(const char *n, int d) { const char *v = getenv(n); return (v && *v) ? atoi(v) : d; }

static void *signaller(void *p) {
    (void)p;
    unsigned r = 12345;
    int lost_ms = envi("SR_LOST_MS", 1000);
    while (!b_ready) usleep(1000);
    while (!stop_all) {
        pthread_mutex_lock(&mx);
        long want = b_iters + 1;
        flag = 1;
        pthread_cond_signal(&cv);
        pthread_mutex_unlock(&mx);
        int waited_us = 0, late = 0;
        while (b_iters < want && !stop_all) {
            usleep(50);
            waited_us += 50;
            if (waited_us >= lost_ms * 1000) {
                if (!late) { inv_broken++; late = 1; }
                pthread_mutex_lock(&mx);
                pthread_cond_signal(&cv);
                pthread_mutex_unlock(&mx);
                waited_us = 0;
                if (b_done) break;
            }
        }
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
        pthread_mutex_lock(&mx);
        while (!flag && !stop_all) {
            pthread_cond_wait(&cv, &mx);
            if (!flag) spurious++;
        }
        flag = 0;
        b_iters++;
        pthread_mutex_unlock(&mx);
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
    if (pthread_create(&tb, &at, waiter, NULL) || pthread_create(&ta, &at, signaller, NULL) || pthread_create(&tx, &at, suspender, NULL)) {
        printf("PROBE-FAIL suspend-cv (thread)\n");
        return 1;
    }
    printf("SUSPEND-ADDR addr=%p\n", (void *)&cv);
    int hung = 0;
    long last = -1;
    int still = 0;
    for (int t = 0; t < secs * 10; t++) {
        usleep(100000);
        if (b_iters != last) { last = b_iters; still = 0; } else if (++still >= hang_s * 10) { hung = 1; break; }
        if (t % 50 == 49) printf("SUSPEND-PROGRESS t=%d iters=%ld broken=%ld suspends=%ld spurious=%ld\n", (t + 1) / 10, b_iters, inv_broken, suspends, spurious);
    }
    stop_all = 1;
    pthread_mutex_lock(&mx);
    pthread_cond_broadcast(&cv);
    pthread_mutex_unlock(&mx);
    if (!hung) for (int k = 0; !b_done && k < 5000; k++) usleep(1000);
    if (!hung && !b_done) hung = 1;
    printf("SUSPEND-SUMMARY iters=%ld rdfail=0 invariant_broken=%ld hung=%d suspends=%ld spurious=%ld first_rc=0 addr=%p\n",
           b_iters, inv_broken, hung, suspends, spurious, (void *)&cv);
    sleep(HOLD);
    printf("PROBE-OK suspend-cv\n");
    return 0;
}
