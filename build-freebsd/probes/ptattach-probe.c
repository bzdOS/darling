/* ptattach-probe.c - parallel guest-memory copyin through the server (PT_ATTACH/PT_IO/PT_DETACH bracket).
 * 8 threads x 200 mach_port_construct calls with an options struct in guest memory: the server must read it
 * (copyin) concurrently without EBUSY surfacing as kr=0x10000002 (MACH_SEND_INVALID_DATA).
 * Prints "PTATTACH pass|fail calls=N fail=K" (plus "first_kr=0x..." on failure); exit 0 / 1. */
#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <mach/mach.h>
#include <mach/mach_port.h>

#define THREADS 8
#define PER_THREAD 200

static volatile int go;
static int fails[THREADS];
static kern_return_t first_kr[THREADS];

static void *worker(void *arg) {
    int id = (int)(long)arg;
    while (!go) { }
    for (int i = 0; i < PER_THREAD; i++) {
        mach_port_options_t opts;
        mach_port_name_t name = MACH_PORT_NULL;
        memset(&opts, 0, sizeof(opts));
        opts.flags = MPO_INSERT_SEND_RIGHT;
        kern_return_t kr = mach_port_construct(mach_task_self(), &opts, 0, &name);
        if (kr != KERN_SUCCESS) {
            if (!fails[id]) first_kr[id] = kr;
            fails[id]++;
            continue;
        }
        mach_port_destruct(mach_task_self(), name, -1, 0);
    }
    return NULL;
}

int main(void) {
    setbuf(stdout, NULL);
    pthread_t t[THREADS];
    int started = 0, failed = 0;
    kern_return_t kr0 = KERN_SUCCESS;
    for (int i = 0; i < THREADS; i++)
        if (pthread_create(&t[i], NULL, worker, (void *)(long)i) == 0) started++;
    go = 1;
    for (int i = 0; i < started; i++) pthread_join(t[i], NULL);
    for (int i = 0; i < THREADS; i++) { if (fails[i] && !failed) kr0 = first_kr[i]; failed += fails[i]; }
    int calls = started * PER_THREAD;
    int ok = started == THREADS && failed == 0;
    printf("PTATTACH %s calls=%d fail=%d", ok ? "pass" : "fail", calls, failed);
    if (failed) printf(" first_kr=0x%x", (unsigned)kr0);
    printf("\n");
    printf(ok ? "PROBE-OK ptattach\n" : "PROBE-FAIL ptattach\n");
    return ok ? 0 : 1;
}
