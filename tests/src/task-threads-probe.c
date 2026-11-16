/* task-threads-probe.c - task_threads() must succeed and list the caller's own thread.
 * Phase 1: with 8 parked threads, 300 calls (verdict). Phase 2 (reported, not part of the verdict):
 * the same while another thread creates and joins short-lived threads.
 * Prints "PROBE-OK task_threads calls=N" or "PROBE-FAIL ..."; exit 0 / 1. */
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>
#include <mach/mach.h>

static volatile int stop;
static void *park(void *arg) { (void)arg; while (!stop) usleep(20000); return NULL; }
static void *shortlived(void *arg) { (void)arg; usleep(200); return NULL; }
static void *churn(void *arg) {
    (void)arg;
    for (int i = 0; i < 60 && !stop; i++) {
        pthread_t t;
        if (pthread_create(&t, NULL, shortlived, NULL) == 0) pthread_join(t, NULL);
        usleep(3000);
    }
    return NULL;
}

static int once(mach_port_t self, kern_return_t *kr_out) {
    thread_act_array_t th; mach_msg_type_number_t n = 0;
    kern_return_t kr = task_threads(mach_task_self(), &th, &n);
    if (kr != KERN_SUCCESS) { *kr_out = kr; return 0; }
    int found = 0;
    for (mach_msg_type_number_t j = 0; j < n; j++) {
        if (th[j] == self) found = 1;
        mach_port_deallocate(mach_task_self(), th[j]);
    }
    vm_deallocate(mach_task_self(), (vm_address_t)th, n * sizeof(*th));
    *kr_out = KERN_SUCCESS;
    return found;
}

int main(void) {
    setbuf(stdout, NULL);
    pthread_t p[8];
    for (int i = 0; i < 8; i++) pthread_create(&p[i], NULL, park, NULL);
    mach_port_t self = mach_thread_self();
    long calls = 0, bad = 0;
    kern_return_t kr, first = 0;
    for (int i = 0; i < 300; i++) {
        if (!once(self, &kr)) { if (!bad) first = kr; bad++; } else calls++;
    }
    if (bad) { printf("PROBE-FAIL task_threads bad=%ld ok=%ld first_kr=0x%x\n", bad, calls, first); _exit(1); }
    printf("PROBE-OK task_threads calls=%ld\n", calls);
    pthread_t c; pthread_create(&c, NULL, churn, NULL);
    long cok = 0, cbad = 0;
    for (int i = 0; i < 100; i++) { if (once(self, &kr)) cok++; else cbad++; usleep(2000); }
    printf("CHURN-DONE ok=%ld bad=%ld\n", cok, cbad);
    _exit(0);
}
