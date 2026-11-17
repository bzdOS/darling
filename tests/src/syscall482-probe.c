/* syscall482-probe.c - BSD syscall 482 (thread_selfusage) must be implemented: it returns the calling thread CPU
 * time (nonzero after work, nondecreasing, grows with busy work) and must not print "Unimplemented syscall (482)".
 * Output: S482-T lines and one S482-SUMMARY n=<checks> bad=<names|-> */
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <sys/syscall.h>
static int n, nbad; static char bad[128];
static void chk(const char *nm, int ok, unsigned long long v) { n++; if (!ok) { nbad++; { strncat(bad, nm, 20); strcat(bad, ","); } } printf("S482-T %s %s v=%llu\n", nm, ok ? "ok" : "bad", v); fflush(stdout); }
int main(void)
{
	volatile unsigned long x = 0; unsigned long long a, b, c; long i;
	a = (unsigned long long)syscall(482);
	for (i = 0; i < 20000000; i++) x += i;
	b = (unsigned long long)syscall(482);
	for (i = 0; i < 20000000; i++) x += i;
	c = (unsigned long long)syscall(482);
	chk("nonzero", a != 0 && a != (unsigned long long)-1, a);
	chk("monotonic", b >= a && c >= b, c);
	chk("grows", c > a, c - a);
	printf("S482-SUMMARY n=%d bad=%s\n", n, nbad ? bad : "-");
	return 0;
}
