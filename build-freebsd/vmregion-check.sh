#!/bin/sh
# vmregion-check.sh - vm_region_64 at the default malloc zone, a heap, a stack and a text address returns the containing region (Chrome allocator_interception_apple.mm).
# Builds tests/vmregion-probe-macho from tests/src/vmregion-probe.c and runs it under launch-dynamic.
# Usage: sh build-freebsd/vmregion-check.sh [wait-secs]    Env: DARLING_SRC_DIR DARLING_OVERLAY DARLING_BUILD_DIR
# Prints VMR lines and a final VMR RESULT pass|FAIL; exit 0 on pass, 1 otherwise.
# Only the darlingserver processes this run started are stopped (by pid); a pre-existing one is left alone.
SRC="${DARLING_SRC_DIR:?}"; BD="${DARLING_BUILD_DIR:?}"; OD="${DARLING_OVERLAY:?}"; WAIT="${1:-60}"
export PATH="/usr/local/bin:/usr/local/sbin:/usr/bin:/bin:/sbin:/usr/sbin"
T=vmregion-probe-macho
W=$(mktemp -d "$BD/sc-stubs.XXXXXX") || { echo "FATAL: mktemp workdir failed for sc-stubs" >&2; exit 1; }
cleanup() { rm -rf "$W"; }
trap cleanup EXIT INT TERM HUP
clang -target x86_64-apple-macos10.12 -O1 -w -ffreestanding -c "$SRC/tests/src/vmregion-probe.c" -o "$W/vmrprobe.o" &&
ld64.lld -arch x86_64 -platform_version macos 10.12 10.12 -syslibroot "$OD" -Z -o "$SRC/tests/$T" "$W/vmrprobe.o" "$OD/usr/lib/libSystem.B.dylib" 2>/dev/null \
	|| { echo "VMR RESULT FAIL (build: compile or link error)"; exit 1; }
LOG="$BD/vmregion-check-$(date +%m%d-%H%M%S).log"
PRE=" $(pgrep -x darlingserver | tr "\n" " ") "
. "$(dirname "$0")/probe-launch.sh"
probe_launch "$LOG" "$WAIT" "VMR DONE\|FATAL signal" sudo timeout -k 5 120 env "DARLING_SRC_DIR=$SRC" "DARLING_OVERLAY=$OD" "DARLING_BUILD_DIR=$BD" "DARLING_TEST_BINARY=$T" \
	DARLING_SMOKE_REFRESH=1 "$BD/launch-dynamic"
sleep 1
for p in $(pgrep -x darlingserver); do case "$PRE" in *" $p "*) ;; *) sudo kill -9 "$p" 2>/dev/null;; esac; done
grep -a "^VMR" "$LOG"
if grep -qa "^VMR DONE fail=0" "$LOG" && ! grep -qa "^VMR .* FAIL" "$LOG"; then echo "VMR RESULT pass"; exit 0; fi
echo "VMR RESULT FAIL"; exit 1
