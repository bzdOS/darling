#!/bin/sh
# fwdmiss-check.sh - the Foundation/UTType/NSConstantDictionary API and the com.apple.dock defaults domain that Chrome startup uses (see tests/src/fwdmiss-probe.c).
# Builds tests/fwdmiss-probe-macho from tests/src/fwdmiss-probe.c and runs it under launch-dynamic.
# Usage: sh build-freebsd/fwdmiss-check.sh [wait-secs]    Env: DARLING_SRC_DIR DARLING_OVERLAY DARLING_BUILD_DIR
# Prints FWDM lines and a final FWDM RESULT pass|FAIL; exit 0 on pass, 1 otherwise.
# Only the darlingserver processes this run started are stopped (by pid); a pre-existing one is left alone.
SRC="${DARLING_SRC_DIR:?}"; BD="${DARLING_BUILD_DIR:?}"; OD="${DARLING_OVERLAY:?}"; WAIT="${1:-60}"
export PATH="/usr/local/bin:/usr/local/sbin:/usr/bin:/bin:/sbin:/usr/sbin"
T=fwdmiss-probe-macho
W=$(mktemp -d "$BD/sc-stubs.XXXXXX") || { echo "FATAL: mktemp workdir failed for sc-stubs" >&2; exit 1; }
cleanup() { rm -rf "$W"; }
trap cleanup EXIT INT TERM HUP
clang -target x86_64-apple-macos10.12 -O1 -w -ffreestanding -c "$SRC/tests/src/fwdmiss-probe.c" -o "$W/fwdprobe.o" &&
ld64.lld -arch x86_64 -platform_version macos 10.12 10.12 -syslibroot "$OD" -Z -o "$SRC/tests/$T" "$W/fwdprobe.o" "$OD/usr/lib/libSystem.B.dylib" "$OD/usr/lib/libobjc.A.dylib" -needed_library "$OD/System/Library/Frameworks/CoreFoundation.framework/Versions/A/CoreFoundation" -needed_library "$OD/System/Library/Frameworks/Foundation.framework/Versions/C/Foundation" -needed_library "$OD/usr/lib/CoreFoundationExtras.dylib" -needed_library "$OD/usr/lib/FoundationExtras.dylib" -needed_library "$OD/usr/lib/UniformTypeIdentifiersExtras.dylib" 2>"$W/fwdprobe.ld.err" \
	|| { echo "FWDM RESULT FAIL (build: compile or link error)"; exit 1; }
LOG="$BD/fwdmiss-check-$(date +%m%d-%H%M%S).log"
PRE=" $(pgrep -x darlingserver | tr "\n" " ") "
. "$(dirname "$0")/probe-launch.sh"
probe_launch "$LOG" "$WAIT" "FWDM DONE\|FATAL signal" sudo timeout -k 5 120 env "DARLING_SRC_DIR=$SRC" "DARLING_OVERLAY=$OD" "DARLING_BUILD_DIR=$BD" "DARLING_TEST_BINARY=$T" \
	DARLING_SMOKE_REFRESH=1 "$BD/launch-dynamic"
sleep 1
for p in $(pgrep -x darlingserver); do case "$PRE" in *" $p "*) ;; *) sudo kill -9 "$p" 2>/dev/null;; esac; done
grep -a "^FWDM" "$LOG"
if grep -qa "^FWDM DONE fail=0" "$LOG" && ! grep -qa "^FWDM .* FAIL" "$LOG"; then echo "FWDM RESULT pass"; exit 0; fi
echo "FWDM RESULT FAIL"; exit 1
