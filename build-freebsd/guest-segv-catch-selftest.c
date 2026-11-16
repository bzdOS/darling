/* guest-segv-catch-selftest.c - stands in for a guest: writes a fake dyld-trace line naming its own text, waits for the
 * catcher to attach, then faults in a three-deep call chain. argv[1] = fake log path, argv[2] = ready file. */
#include <stdint.h>
#include <stdio.h>
#include <unistd.h>
__attribute__((noinline)) int crash_leaf(volatile int *p) { *p = 1; return 1; }
__attribute__((noinline)) int crash_mid(int x) { return crash_leaf((volatile int *)0x10) + x; }
__attribute__((noinline)) int crash_top(int x) { return crash_mid(x) + 1; }
int main(int argc, char **argv) {
	if (argc < 3) return 2;
	FILE *f = fopen(argv[1], "w");
	if (!f) return 2;
	fprintf(f, "[dyld-trace]  [0] mh=0x%lx selftest-image\n", (unsigned long)((uintptr_t)&main & ~0xfffffUL));
	fclose(f);
	f = fopen(argv[2], "w"); if (f) fclose(f);
	for (int i = 0; i < 100; i++) usleep(100000);	/* catcher attaches inside this window */
	return crash_top(argc);
}
