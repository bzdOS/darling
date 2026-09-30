#!/bin/sh
# shm-flags-contract.sh — the host-side contract that the shim breaks.
#
# Usage: sh build-freebsd/shm-flags-contract.sh
#   No root, no guest, no emulation in the picture: this is the host's own
#   shm_open called with the arguments the shim hands it.
#
# WHY THIS EXISTS
# ---------------
# The window probe reached a `WaylandWindow` and then stopped at
#
#     WaylandWindow: shm allocation failed for 640x480 buffer: Invalid argument
#
# and the cause was found in the emulation layer, at
# src/external/xnu/darling/.../impl/wrapped/shm_open.c:20:
#
#     ret = elfcalls()->shm_open(name, oflags_bsd_to_linux(oflag), mode);
#
# `elfcalls()->shm_open` is the HOST's shm_open — mldr fills that table from
# the host's own symbols (src/startup/mldr/elfcalls/elfcalls.c:118, into the
# slot at src/startup/mldr/elfcalls/elfcalls.h:51) — so the flags are
# translated BSD → Linux and then handed to a function that expects BSD. The
# caller's O_CREAT becomes a bit the host reads as O_ASYNC, and every call
# carrying O_CREAT fails with EINVAL.
#
# This is that claim as a test, and it is a contract test rather than a test of
# the shim: what it asserts is the contract those elfcalls slots are filled
# under — **a host function filled into that table takes host flags, not
# Linux ones** — and it asserts it by calling that host function with each
# value and printing what comes back.
#
# WHAT IT CAN AND CANNOT SHOW
# ---------------------------
# It CAN show the arithmetic: which values the host accepts, which it refuses,
# and therefore exactly what the shim must pass down and must not. That is the
# whole of the fix, and it is checkable without a guest.
#
# It CANNOT show that the shim is fixed. The shim is a prebuilt dylib in the
# overlay and the emulation source is a submodule this repository does not
# build; see WORKAROUND-344.md §13. So the rows below are the ORACLE — after a
# fix, the guest's shm_open must agree with this output, and if it does not, the
# fix is not in the place §13 says it is.
#
# The two O_CREAT rows are the ones that matter. Before the fix they cannot
# succeed for any value the caller can pass, because O_CREAT does not survive
# the trip down; after it they are ordinary successful calls.
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
export PATH="/usr/local/bin:/usr/local/sbin:/usr/bin:/bin:/sbin:/usr/sbin"

TMP="$(mktemp -d)"
trap 'rm -rf "${TMP}"' EXIT

cat >"${TMP}/contract.c" <<'EOF'
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <string.h>
#include <errno.h>

/* One row: a value, and what the HOST's shm_open does with it. The value is
 * passed exactly as given, because the point is that the shim should be
 * passing it exactly as given. */
static int row(const char *label, const char *name, int flags)
{
	int fd = shm_open(name, flags, 0600);
	int e = fd < 0 ? errno : 0;
	printf("  %-44s 0x%03x -> %2d  errno=%-2d %s\n", label, flags, fd, e,
	       e ? strerror(e) : "-");
	if (fd >= 0) { close(fd); shm_unlink(name); }
	return fd >= 0 ? 0 : e;
}

int main(void)
{
	int failures = 0;

	puts("the values a caller can pass, given to the host's shm_open as-is:");
	puts("  (BSD O_RDONLY=0x000 O_RDWR=0x002 O_CREAT=0x200 O_EXCL=0x800)");
	puts("");

	/* These three MUST work. O_CREAT cannot be honoured if the shim
	 * translates the flags, because the bit the caller set becomes one the
	 * host reads as O_ASYNC. */
	failures += row("O_RDWR|O_CREAT",              "/.contract-a", 0x202) != 0;
	failures += row("O_CREAT|O_EXCL",               "/.contract-b", 0xa00) != 0;
	failures += row("O_RDWR|O_CREAT|O_EXCL",       "/.contract-c", 0xa02) != 0;

	/* O_RDWR with no O_CREAT must NOT create anything: ENOENT is the correct
	 * answer for a name that is not there, and a shim that answered
	 * anything else would be worse, not better. */
	{
		int e = row("O_RDWR, no O_CREAT (want ENOENT)", "/.contract-d", 0x002);
		if (e != ENOENT) {
			printf("  FAIL: O_RDWR on a missing name gave errno %d,"
			       " ENOENT is the correct answer\n", e);
			failures++;
		}
	}

	/* The value oflags_bsd_to_linux produces from the row above. The host
	 * must REFUSE it, and that refusal is the bug's fingerprint: it is
	 * what the shim was producing, so it is what the guest used to see. */
	{
		int e = row("translated 0x0c2 (want EINVAL)", "/.contract-e", 0x0c2);
		if (e != EINVAL) {
			printf("  NOTE: 0x0c2 gave errno %d, not EINVAL — the"
			       " fingerprint has moved, re-derive it before"
			       " trusting the rest of this file\n", e);
		}
	}

	puts("");
	if (failures == 0)
		puts("contract holds: O_CREAT reaches the host and is honoured.");
	else
		printf("contract VIOLATED: %d row(s) that must work did not.\n", failures);
	return failures == 0 ? 0 : 1;
}
EOF

cc -o "${TMP}/contract" "${TMP}/contract.c"

echo "=== host contract, measured on this machine ==="
"${TMP}/contract"
rc=$?

echo
echo "=== what the shim must therefore do ==="
echo "  elfcalls()->shm_open(name, oflag, mode)   -- oflag UNCHANGED"
echo "  elfcalls()->sem_open(name, oflag, mode, value) -- oflag UNCHANGED"
echo "  the openat(2) path via LINUX_SYSCALL keeps oflags_bsd_to_linux: that"
echo "  one is a different contract and it is correct as it stands."
exit ${rc}
