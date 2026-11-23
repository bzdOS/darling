#!/bin/sh
# rendezvous-bootstrap-probe-check.sh - guest probe for Chrome r23 rendezvous hypotheses.
# Distinguishes: (A) getppid mismatch in guest fork, (B) bootstrap namespace not shared across fork.
# Builds tests/rendezvous-bootstrap-probe-macho from tests/src/rendezvous-bootstrap-probe.c and runs it
# under launch-dynamic with bootstrap shim (unless DARLING_NO_BOOTSTRAP_SHIM=1).
# Usage: sh build-freebsd/rendezvous-bootstrap-probe-check.sh [wait-secs]
# Env: DARLING_SRC_DIR DARLING_OVERLAY DARLING_BUILD_DIR
SRC="${DARLING_SRC_DIR:?}"; BD="${DARLING_BUILD_DIR:?}"; OD="${DARLING_OVERLAY:?}"; WAIT="${1:-60}"
export PATH="/usr/local/bin:/usr/local/sbin:/usr/bin:/bin:/sbin:/usr/sbin"
SDK_FLAT="$BD/bundle-principal-class-test/sdk-flat"; T=rendezvous-bootstrap-probe-macho
W=$(mktemp -d "$BD/rendezvous-bootstrap-probe.XXXXXX") || { echo "FATAL: mktemp workdir failed" >&2; exit 1; }
cleanup() { rm -rf "$W"; }
trap cleanup EXIT INT TERM HUP
[ -d "$SDK_FLAT/usr/include" ] || { echo "RBP RESULT fail (no flattened SDK)"; exit 1; }
clang -target x86_64-apple-macos10.12 -nostdinc -D__DARWIN_ONLY_UNIX_CONFORMANCE=1 -O1 -w \
	-I"$SRC/tests/vendor/fakesdk" -I"$SDK_FLAT/usr/include" -c "$SRC/tests/src/rendezvous-bootstrap-probe.c" -o "$W/probe.o" &&
ld64.lld -arch x86_64 -platform_version macos 10.12 10.12 -syslibroot "$OD" -Z -o "$SRC/tests/$T" "$W/probe.o" "$OD/System/Library/Frameworks/CoreFoundation.framework/Versions/A/CoreFoundation" "$OD/usr/lib/libobjc.A.dylib" "$OD/usr/lib/libicucore.A.dylib" "$OD/usr/lib/libSystem.B.dylib" 2>/dev/null \
	|| { echo "RBP RESULT fail (build)"; exit 1; }
LOG="$BD/rendezvous-bootstrap-probe-$(date +%m%d-%H%M%S).log"
PRE=" $(pgrep -x darlingserver | tr '\n' ' ') "
. "$(dirname "$0")/probe-launch.sh"
probe_launch "$LOG" "$WAIT" "RBP RESULT\|FATAL signal" sudo timeout -k 5 120 env "DARLING_SRC_DIR=$SRC" "DARLING_OVERLAY=$OD" "DARLING_BUILD_DIR=$BD" "DARLING_TEST_BINARY=$T" ${DARLING_NO_BOOTSTRAP_SHIM:+DARLING_NO_BOOTSTRAP_SHIM=1} \
	DARLING_SMOKE_REFRESH=1 "$BD/launch-dynamic"
sleep 1
for p in $(pgrep -x darlingserver); do case "$PRE" in *" $p "*) ;; *) sudo kill -9 "$p" 2>/dev/null;; esac; done
grep -a "^RBP" "$LOG"
if grep -qa "^RBP RESULT pass" "$LOG"; then echo "LOG=$LOG"; exit 0; fi
grep -qa "^RBP RESULT" "$LOG" || echo "RBP RESULT fail (no result line, probe did not finish)"
echo "LOG=$LOG"
exit 1
