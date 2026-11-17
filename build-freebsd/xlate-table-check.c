/* xlate-table-check.c - static consistency check of src/startup/xlate/bsdos_linux_xlate.h.
 * Build and run: build-freebsd/xlate-table-check.sh. One line: XLATE-TABLE-CHECK pass|FAIL n=.. bad=.. [...] */
#include <stdio.h>
#include <string.h>
#include "bsdos_linux_xlate.h"

static char bad[1024]; static int nbad, n;
#define CHECK(c, what) do { n++; if (!(c)) { nbad++; if (strlen(bad) < 900) { strcat(bad, what); strcat(bad, ","); } } } while (0)

int main(void) {
	for (int i = 0; i < BSDOS_ERRNO_COUNT; i++) {
		const struct bsdos_errno_pair *p = &bsdos_errno_table[i];
		CHECK(bsdos_errno_host_to_guest(p->host) == p->guest, "errno-h2g");
		CHECK(bsdos_errno_guest_to_host(p->guest) == p->host, "errno-g2h");
		for (int j = i + 1; j < BSDOS_ERRNO_COUNT; j++) {
			CHECK(!(bsdos_errno_table[j].host == p->host && bsdos_errno_table[j].guest != p->guest), "errno-dup-host");
			CHECK(!(bsdos_errno_table[j].guest == p->guest), "errno-dup-guest");
		}
	}
	for (int i = 0; i < BSDOS_SIGNO_COUNT; i++) {
		const struct bsdos_signo_pair *p = &bsdos_signo_table[i];
		CHECK(bsdos_signo_guest_to_host(p->guest) == p->host, "sig-g2h");
		CHECK(bsdos_signo_host_to_guest(p->host) == p->guest, "sig-h2g");
	}
	CHECK(bsdos_signo_guest_to_host(10) == SIGUSR1 && SIGUSR1 != 10, "sigusr1");
	CHECK(bsdos_signo_guest_to_host(16) == -1 && bsdos_signo_guest_to_host(30) == -1, "sig-nocounterpart");
	CHECK(bsdos_signo_guest_to_host(65) == SIGRTMIN && bsdos_signo_guest_to_host(126) == SIGRTMAX, "sig-rt");
	CHECK(PROT_READ == 1 && PROT_WRITE == 2 && PROT_EXEC == 4, "prot-same");
	int all = BSDOS_G_MAP_SHARED | BSDOS_G_MAP_PRIVATE | BSDOS_G_MAP_FIXED | BSDOS_G_MAP_ANONYMOUS | BSDOS_G_MAP_STACK;
	CHECK(bsdos_mmap_flags_host_to_guest(bsdos_mmap_flags_guest_to_host(all)) == all, "mmap-roundtrip");
	int nr = BSDOS_G_MAP_FIXED_NOREPLACE | BSDOS_G_MAP_PRIVATE | BSDOS_G_MAP_ANONYMOUS;
	CHECK(bsdos_mmap_flags_host_to_guest(bsdos_mmap_flags_guest_to_host(nr)) == nr, "mmap-noreplace");
	CHECK(bsdos_msync_flags_guest_to_host(BSDOS_G_MS_SYNC) == MS_SYNC && bsdos_msync_flags_guest_to_host(BSDOS_G_MS_ASYNC) == MS_ASYNC, "msync-g2h");
	CHECK(bsdos_msync_flags_host_to_guest(MS_SYNC) == BSDOS_G_MS_SYNC && bsdos_msync_flags_host_to_guest(MS_ASYNC | MS_INVALIDATE) == (BSDOS_G_MS_ASYNC | BSDOS_G_MS_INVALIDATE), "msync-h2g");
	CHECK(bsdos_errno_host_to_guest(EAGAIN) == 11 && bsdos_errno_host_to_guest(ETIMEDOUT) == 110 && bsdos_errno_host_to_guest(ENOSYS) == 38, "errno-spot");
	printf("XLATE-TABLE-CHECK %s n=%d bad=%d [%s]\n", nbad ? "FAIL" : "pass", n, nbad, bad);
	return nbad != 0;
}
