/* traplog-formats-test.c — host-side check that the locale-free marker
 * writers emit exactly the expected bytes (no stdio on the path under
 * test: the writers use only converters + write(2)).
 *
 * Build & run (host):  cc -I<dir> -o t traplog-formats-test.c && ./t
 * The test defines the two extern gates the header declares.
 */
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include "trap_log_nolocale.h"

int mldr_trap_log_enabled = 1;
int mldr_trap_log_handlers = 1;

static int fail;

static void expect(const char *got, int n, const char *want, const char *what)
{
	if ((int)strlen(want) != n || memcmp(got, want, (size_t)n) != 0) {
		fprintf(stderr, "FAIL %s:\n  got  [%.*s]\n  want [%s]\n",
			what, n, got, want);
		fail = 1;
	} else {
		fprintf(stderr, "ok %s: [%.*s]", what, n, got);
	}
}

int main(void)
{
	char buf[192];
	int n;
	char want[192];
	long tid = 0;

	thr_self(&tid);

	/* build path: exact layout "[traplog] <tag> tid=<d> a=<d> b=<d>\n" */
	n = mldr_tlog_build(buf, (int)sizeof(buf), "elf-exit CALL", 7, 0);
	snprintf(want, sizeof(want), "[traplog] elf-exit CALL tid=%ld a=7 b=0\n", tid);
	expect(buf, n, want, "tlog_build");

	n = mldr_tlog_build(buf, (int)sizeof(buf), "linux-unhandled", 99, -1);
	snprintf(want, sizeof(want), "[traplog] linux-unhandled tid=%ld a=99 b=-1\n", tid);
	expect(buf, n, want, "tlog_build-neg");

	n = mldr_tlog_build(buf, (int)sizeof(buf), "x", 0, 0);
	snprintf(want, sizeof(want), "[traplog] x tid=%ld a=0 b=0\n", tid);
	expect(buf, n, want, "tlog_build-zero");

	/* converter spot-checks through the public writers' build */
	n = mldr_tlog_build(buf, (int)sizeof(buf), "t", 123456789L, -42L);
	snprintf(want, sizeof(want), "[traplog] t tid=%ld a=123456789 b=-42\n", tid);
	expect(buf, n, want, "tlog_build-big");

	/* hex path: byte-exact layout "[traplog] <tag> tid=<> p=0x<hex> b=<>" */
	n = mldr_tlogx_build(buf, (int)sizeof(buf), "elf-dlsym ENTER",
	                     (const void *)(uintptr_t)0x1234abcdUL, -5);
	snprintf(want, sizeof(want),
	         "[traplog] elf-dlsym ENTER tid=%ld p=0x1234abcd b=-5\n", tid);
	expect(buf, n, want, "tlogx_build-hex");

	n = mldr_tlogx_build(buf, (int)sizeof(buf), "z", (const void *)(uintptr_t)0UL, 0);
	snprintf(want, sizeof(want), "[traplog] z tid=%ld p=0x0 b=0\n", tid);
	expect(buf, n, want, "tlogx_build-zero");

	return fail;
}
