#!/bin/sh
# crash-dump-selftest.sh — run mldr's guarded crash-dump self-test.
#
# Usage: sh build-freebsd/crash-dump-selftest.sh
#
# No root, no overlay, no darlingserver, no guest: this compiles
# build-freebsd/crash-dump-selftest.c, which #includes
# src/startup/mldr/crash_dump.c — the file linked into mldr — and drives the
# failures the dump has to survive: a PROT_NONE page (mapped, not readable: the
# exact case that killed the dump in build-freebsd/SIGSEGV-SECOND-DLOPEN.md), a
# hole, and out-of-range garbage. Exit 0 only if every case passed.
#
# Worth running after any change to crash_dump.c. The property under test is
# invisible to a build: a dump that faults takes the process with it, and the
# only symptom is a crash log that stops one line after its own header.
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
SRC="$(cd "${SCRIPT_DIR}/.." && pwd)"
PATH="/usr/local/bin:/usr/local/sbin:/usr/bin:/bin:/sbin:/usr/sbin"
export PATH

OUT="$(mktemp -t crash-dump-selftest.XXXXXX)"
trap 'rm -f "${OUT}"' EXIT INT TERM

CC="${CC:-clang}"
"${CC}" -std=gnu11 -ggdb -O1 -Wall -Wextra \
	-o "${OUT}" "${SCRIPT_DIR}/crash-dump-selftest.c"
"${OUT}"
