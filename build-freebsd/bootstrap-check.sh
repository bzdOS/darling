#!/bin/sh
# bootstrap-check.sh - inside Darling, the task has a live bootstrap port, bootstrap_check_in of a unique name
# succeeds and bootstrap_look_up of that name returns a port (what Chrome MachPortRendezvousServer needs).
# Builds tests/bootstrap-probe-macho from tests/src/bootstrap-probe.c and runs it under launch-dynamic.
# Usage: sh build-freebsd/bootstrap-check.sh [wait-secs]    Env: DARLING_SRC_DIR DARLING_OVERLAY DARLING_BUILD_DIR
# Prints BOOTCHK lines and a final "BOOTCHK RESULT pass|fail"; exit 0 on pass, 1 otherwise.
# Only the darlingserver processes this run started are stopped (by pid); a pre-existing one is left alone.
SRC="${DARLING_SRC_DIR:?}"; BD="${DARLING_BUILD_DIR:?}"; OD="${DARLING_OVERLAY:?}"; WAIT="${1:-60}"
export PATH="/usr/local/bin:/usr/local/sbin:/usr/bin:/bin:/sbin:/usr/sbin"
SDK_FLAT="$BD/bundle-principal-class-test/sdk-flat"; T=bootstrap-probe-macho
W=$(mktemp -d "$BD/bootstrap-probe.XXXXXX") || { echo "FATAL: mktemp workdir failed for bootstrap-probe" >&2; exit 1; }
cleanup() { rm -rf "$W"; }
trap cleanup EXIT INT TERM HUP
[ -d "$SDK_FLAT/usr/include" ] || { echo "BOOTCHK RESULT FAIL (no flattened SDK)"; exit 1; }
clang -target x86_64-apple-macos10.12 -nostdinc -D__DARWIN_ONLY_UNIX_CONFORMANCE=1 -O1 -w \
	-I"$SRC/tests/vendor/fakesdk" -I"$SDK_FLAT/usr/include" -c "$SRC/tests/src/bootstrap-probe.c" -o "$W/probe.o" &&
ld64.lld -arch x86_64 -platform_version macos 10.12 10.12 -syslibroot "$OD" -Z -o "$SRC/tests/$T" "$W/probe.o" "$OD/System/Library/Frameworks/CoreFoundation.framework/Versions/A/CoreFoundation" "$OD/usr/lib/libobjc.A.dylib" "$OD/usr/lib/libicucore.A.dylib" "$OD/usr/lib/libSystem.B.dylib" 2>/dev/null \
	|| { echo "BOOTCHK RESULT FAIL (build)"; exit 1; }
LOG="$BD/bootstrap-check-$(date +%m%d-%H%M%S).log"
PRE=" $(pgrep -x darlingserver | tr '\n' ' ') "
. "$(dirname "$0")/probe-launch.sh"
probe_launch "$LOG" "$WAIT" "BOOTCHK RESULT\|FATAL signal" sudo timeout -k 5 120 env "DARLING_SRC_DIR=$SRC" "DARLING_OVERLAY=$OD" "DARLING_BUILD_DIR=$BD" "DARLING_TEST_BINARY=$T" ${DARLING_NO_BOOTSTRAP_SHIM:+DARLING_NO_BOOTSTRAP_SHIM=1} \
	DARLING_SMOKE_REFRESH=1 "$BD/launch-dynamic"
sleep 1
for p in $(pgrep -x darlingserver); do case "$PRE" in *" $p "*) ;; *) sudo kill -9 "$p" 2>/dev/null;; esac; done
grep -a "^BOOTCHK" "$LOG"
if grep -qa "^BOOTCHK RESULT pass" "$LOG"; then exit 0; fi
grep -qa "^BOOTCHK RESULT" "$LOG" || echo "BOOTCHK RESULT fail (no result line, probe did not finish)"
exit 1
