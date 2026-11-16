#!/bin/sh
# crash-probe-race-selftest.sh - guarded reads on one thread must not steal or kill a genuine fault on another.
# Builds crash-probe-race-selftest.c against src/startup/mldr/crash_dump.c; no root, no guest.
# Prints the program's RACE-SELFTEST line; if the program dies of a signal, prints "RACE-SELFTEST FAIL: died of signal N".
D=$(cd "$(dirname "$0")" && pwd)
W=$(mktemp -d "${TMPDIR:-/tmp}/race-selftest.XXXXXX") || exit 1
trap 'rm -rf "$W"' EXIT
"${CC:-clang}" -std=gnu11 -O1 -g -Wall -o "$W/race" "$D/crash-probe-race-selftest.c" -lpthread || { echo "RACE-SELFTEST FAIL: build"; exit 1; }
( ulimit -c 0; "$W/race" "${1:-20000}" ) > "$W/out" 2>&1; rc=$?
if grep -q '^RACE-SELFTEST' "$W/out"; then grep '^RACE-SELFTEST' "$W/out"; exit $rc; fi
echo "RACE-SELFTEST FAIL: died of signal $((rc>128 ? rc-128 : rc)) (no result line)"; exit 1
