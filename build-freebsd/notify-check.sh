#!/bin/sh
# notify-check.sh - notify_register_dispatch/check/plain/cancel (as Chrome thermal state observer calls them) must succeed with the bootstrap shim inserted.
# Builds tests/notify-probe-macho from tests/src/notify-probe.c and runs it under launch-dynamic.
# Usage: sh build-freebsd/notify-check.sh [wait-secs]    Env: DARLING_SRC_DIR DARLING_OVERLAY DARLING_BUILD_DIR
# Prints NTFY lines and a final NTFY RESULT pass|FAIL; exit 0 on pass, 1 otherwise.
# Only the darlingserver processes this run started are stopped (by pid); a pre-existing one is left alone.
SRC="${DARLING_SRC_DIR:?}"; BD="${DARLING_BUILD_DIR:?}"; OD="${DARLING_OVERLAY:?}"; WAIT="${1:-60}"
export PATH="/usr/local/bin:/usr/local/sbin:/usr/bin:/bin:/sbin:/usr/sbin"
T=notify-probe-macho; W="$BD/sc-stubs"; mkdir -p "$W"
clang -target x86_64-apple-macos10.12 -O1 -w -fblocks -ffreestanding -c "$SRC/tests/src/notify-probe.c" -o "$W/ntfyprobe.o" &&
ld64.lld -arch x86_64 -platform_version macos 10.12 10.12 -syslibroot "$OD" -Z -o "$SRC/tests/$T" "$W/ntfyprobe.o" "$OD/usr/lib/libSystem.B.dylib" 2>/dev/null \
	|| { echo "NTFY RESULT FAIL (build: compile or link error)"; exit 1; }
LOG="$BD/notify-check.log"
PRE=" $(pgrep -x darlingserver | tr "\n" " ") "
. "$(dirname "$0")/probe-launch.sh"
probe_launch "$LOG" "$WAIT" "NTFY DONE\|FATAL signal" sudo timeout -k 5 120 env "DARLING_SRC_DIR=$SRC" "DARLING_OVERLAY=$OD" "DARLING_BUILD_DIR=$BD" "DARLING_TEST_BINARY=$T" "DARLING_INSERT_EXTRA=${NTFY_INSERT:-/usr/lib/darling-bootstrap.dylib}" \
	DARLING_SMOKE_REFRESH=1 "$BD/launch-dynamic"
sleep 1
for p in $(pgrep -x darlingserver); do case "$PRE" in *" $p "*) ;; *) sudo kill -9 "$p" 2>/dev/null;; esac; done
grep -a "^NTFY" "$LOG"
if grep -qa "^NTFY DONE fail=0" "$LOG" && ! grep -qa "^NTFY .* FAIL" "$LOG"; then echo "NTFY RESULT pass"; exit 0; fi
echo "NTFY RESULT FAIL"; exit 1
