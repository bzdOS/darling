#!/bin/sh
# crash-probe-reentry-selftest.sh - a probe issued from a signal handler on a thread already inside its probe window must not deadlock.
# Builds crash-probe-reentry-selftest.c against src/startup/mldr/crash_dump.c; no root, no guest.
# Prints the program's REENTRY-SELFTEST line; if it dies of a signal or hangs past the watchdog, prints a FAIL line.
D=$(cd "$(dirname "$0")" && pwd)
W=$(mktemp -d "${TMPDIR:-/tmp}/reentry-selftest.XXXXXX") || exit 1
trap 'rm -rf "$W"' EXIT
"${CC:-clang}" -std=gnu11 -O1 -g -Wall -o "$W/reentry" "$D/crash-probe-reentry-selftest.c" -lpthread || { echo "REENTRY-SELFTEST FAIL: build"; exit 1; }
( ulimit -c 0; "$W/reentry" "${1:-200000}" ) > "$W/out" 2>&1; rc=$?
if grep -q '^REENTRY-SELFTEST' "$W/out"; then grep '^REENTRY-SELFTEST' "$W/out"; exit $rc; fi
echo "REENTRY-SELFTEST FAIL: died of signal $((rc>128 ? rc-128 : rc)) (no result line)"; exit 1
