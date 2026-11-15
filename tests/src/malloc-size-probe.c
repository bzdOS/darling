/* malloc-size-probe: malloc_size() of small calloc/malloc blocks, main thread
 * and a worker thread; reports every block whose reported size is smaller than
 * the request or whose zone is unknown. */
#include <stdio.h>
#include <stdlib.h>
#include <malloc/malloc.h>
#include <pthread.h>

static int bad;

static void run(const char *who)
{
	static const size_t sizes[] = { 8, 16, 24, 32, 48, 64, 96, 128, 200, 256, 300, 1000 };
	for (unsigned s = 0; s < sizeof sizes / sizeof sizes[0]; s++) {
		for (int i = 0; i < 2000; i++) {
			void *p = (i & 1) ? calloc(1, sizes[s]) : malloc(sizes[s]);
			size_t sz = malloc_size(p);
			malloc_zone_t *z = malloc_zone_from_ptr(p);
			if (sz < sizes[s] || z == NULL) {
				if (bad++ < 20)
					printf("%s BAD req=%zu ptr=%p malloc_size=%zu zone=%p\n", who, sizes[s], p, sz, (void *)z);
			}
			if (i == 0)
				printf("%s req=%zu ptr=%p malloc_size=%zu zone=%p (%s)\n", who, sizes[s], p, sz, (void *)z,
				       z ? malloc_get_zone_name(z) : "none");
		}
	}
}

static void *worker(void *a) { run("thread"); return NULL; }

int main(void)
{
	run("main");
	pthread_t t;
	pthread_create(&t, NULL, worker, NULL);
	pthread_join(t, NULL);
	printf("malloc-size-probe: bad=%d\n", bad);
	printf(bad ? "PROBE-FAIL\n" : "PROBE-OK\n");
	return 0;
}
