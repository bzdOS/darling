/* pthread-static-init-probe: statically initialised mutex and condition variable
 * (the way libc++abi's __cxa_guard_* keeps them): lock, unlock, broadcast, from
 * the main thread and from a worker; every return code is printed. */
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

static pthread_mutex_t guard_mut = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t guard_cv = PTHREAD_COND_INITIALIZER;
static int bad;

static void step(const char *who, const char *what, int rc)
{
	printf("pthread-static-init-probe: %s %s rc=%d (%s)\n", who, what, rc, strerror(rc));
	if (rc != 0)
		bad++;
}

static void run(const char *who)
{
	step(who, "mutex_lock", pthread_mutex_lock(&guard_mut));
	step(who, "cond_broadcast (locked)", pthread_cond_broadcast(&guard_cv));
	step(who, "mutex_unlock", pthread_mutex_unlock(&guard_mut));
	step(who, "cond_broadcast (unlocked)", pthread_cond_broadcast(&guard_cv));
}

static void *worker(void *a)
{
	run("thread");
	return NULL;
}

int main(void)
{
	pthread_t t;
	run("main");
	pthread_create(&t, NULL, worker, NULL);
	pthread_join(t, NULL);
	run("main-again");
	printf(bad ? "PROBE-FAIL bad=%d\n" : "PROBE-OK bad=%d\n", bad);
	return bad != 0;
}
