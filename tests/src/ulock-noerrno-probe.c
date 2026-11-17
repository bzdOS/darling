/* ulock-noerrno-probe.c - __ulock_wait with ULF_NO_ERRNO must return the error as a negative value
 * (-errno), not -1 through cerror(). A timed-out wait on an unchanged word is the real path.
 * Output: one line  ULOCK-VERDICT pass|FAIL n=<checks> bad=<failed> [<names>]  (details on ULOCK-T lines). */
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <stdint.h>

extern int __ulock_wait(uint32_t operation, void *addr, uint64_t value, uint32_t timeout_us);

#define UL_COMPARE_AND_WAIT 1
#define UL_UNFAIR_LOCK 2
#define ULF_NO_ERRNO 0x01000000

static int n, nbad;
static char badlist[256];
static void result(const char *name, int ok, int rc, int e)
{
	n++;
	if (!ok) { nbad++; strcat(badlist, name); strcat(badlist, ","); }
	printf("ULOCK-T %s %s rc=%d errno=%d\n", name, ok ? "ok" : "bad", rc, e);
	fflush(stdout);
}

int main(void)
{
	static volatile uint32_t word = 1;
	int rc, e;

	/* the guest ABI value of ETIMEDOUT is the Darwin one (60) */
	errno = 0;
	rc = __ulock_wait(UL_COMPARE_AND_WAIT | ULF_NO_ERRNO, (void *)&word, 1, 30000);
	result("no-errno-compare-and-wait-timeout", rc == -ETIMEDOUT, rc, errno);

	errno = 0;
	rc = __ulock_wait(UL_UNFAIR_LOCK | ULF_NO_ERRNO, (void *)&word, 1, 30000);
	result("no-errno-unfair-lock-timeout", rc == -ETIMEDOUT, rc, errno);

	/* without the flag the error travels through errno as usual */
	errno = 0;
	rc = __ulock_wait(UL_COMPARE_AND_WAIT, (void *)&word, 1, 30000);
	e = errno;
	result("errno-compare-and-wait-timeout", rc == -1 && e == ETIMEDOUT, rc, e);

	/* an unsupported opcode reports EINVAL the same way */
	errno = 0;
	rc = __ulock_wait(0x7f | ULF_NO_ERRNO, (void *)&word, 1, 30000);
	result("no-errno-bad-op", rc == -EINVAL, rc, errno);

	printf("ULOCK-VERDICT %s n=%d bad=%d [%s]\n", nbad ? "FAIL" : "pass", n, nbad, badlist);
	fflush(stdout);
	return nbad != 0;
}
