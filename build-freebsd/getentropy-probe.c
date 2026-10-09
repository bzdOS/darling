/*
 * getentropy-probe.c — Control #142 step 12.
 *
 * The minimal guest probe that isolates the death from Chrome: the same
 * thread calls getentropy(buf, 8) twice and nothing else.  Step 11 resolved
 * the dying init cascade down to exactly this shape:
 *
 *     0x212aa00 -> 0xccdb60 (constructor) -> 0x64d020 -> _getentropy x2
 *
 * with esi = 8 (eight bytes of entropy per call) and the two 8-byte results
 * stored into one 64-bit member at this+0x28.  The hypothesis under test is
 * that the guest dies on the SECOND entry into the getentropy/arc4random path
 * by the same thread (known signature: SIGILL ud2
 * __os_unfair_lock_recursive_abort, libsystem_platform+0x8237).  A probe that
 * only does the two calls answers it with no Chrome in the picture: the same
 * abort means the root cause is confirmed outside Chrome, a clean exit means
 * re-entry is not the cause.
 *
 * Output goes through write(2) only, one line per milestone, so a guest that
 * dies mid-way leaves the prefix that was reached in the log.
 */

extern int getentropy(void *buf, unsigned long buflen);
extern long write(int fd, const void *buf, unsigned long count);

#define ENTROPY_BYTES 8

static void emit(const char *s)
{
	unsigned long n = 0;
	while (s[n] != '\0')
		n++;
	(void)write(2, s, n);
}

static void emit_hex(const char *tag, const unsigned char *p, unsigned long n)
{
	static const char digits[] = "0123456789abcdef";
	unsigned long i;
	emit(tag);
	for (i = 0; i < n; i++) {
		char pair[3];
		pair[0] = digits[(p[i] >> 4) & 0xf];
		pair[1] = digits[p[i] & 0xf];
		pair[2] = '\n';
		(void)write(2, pair, 3);
	}
}

int main(void)
{
	unsigned char first[ENTROPY_BYTES];
	unsigned char second[ENTROPY_BYTES];
	int rc1, rc2;

	emit("probe: before first getentropy\n");
	rc1 = getentropy(first, ENTROPY_BYTES);
	emit(rc1 == 0 ? "probe: first getentropy rc=0\n"
		      : "probe: first getentropy rc!=0\n");
	emit_hex("probe: first ", first, ENTROPY_BYTES);

	emit("probe: before second getentropy (same thread)\n");
	rc2 = getentropy(second, ENTROPY_BYTES);
	emit(rc2 == 0 ? "probe: second getentropy rc=0\n"
		      : "probe: second getentropy rc!=0\n");
	emit_hex("probe: second ", second, ENTROPY_BYTES);

	emit("probe: both calls returned, exiting 0\n");
	return 0;
}
