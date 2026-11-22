#!/bin/sh
# appkit-census-check.sh - NSWindow isOnActiveSpace/collectionBehavior, NSScroller preferredScrollerStyle, NSView sortSubviewsUsingFunction:context: work (tests/src/appkit-census-probe.c).
# Builds tests/appkit-census-probe-macho from tests/src/appkit-census-probe.c and runs it under launch-dynamic.
# Usage: sh build-freebsd/appkit-census-check.sh [wait-secs]    Env: DARLING_SRC_DIR DARLING_OVERLAY DARLING_BUILD_DIR
# Prints AKC lines and a final AKC RESULT pass|FAIL; exit 0 on pass, 1 otherwise.
# Only the darlingserver processes this run started are stopped (by pid); a pre-existing one is left alone.
SRC="${DARLING_SRC_DIR:?}"; BD="${DARLING_BUILD_DIR:?}"; OD="${DARLING_OVERLAY:?}"; WAIT="${1:-60}"
export PATH="/usr/local/bin:/usr/local/sbin:/usr/bin:/bin:/sbin:/usr/sbin"
T=appkit-census-probe-macho
W=$(mktemp -d "$BD/sc-stubs.XXXXXX") || { echo "FATAL: mktemp workdir failed for sc-stubs" >&2; exit 1; }
cleanup() { rm -rf "$W"; }
trap cleanup EXIT INT TERM HUP
clang -target x86_64-apple-macos10.12 -O1 -w -ffreestanding -c "$SRC/tests/src/appkit-census-probe.c" -o "$W/akcprobe.o" &&
ld64.lld -arch x86_64 -platform_version macos 10.12 10.12 -syslibroot "$OD" -Z -o "$SRC/tests/$T" "$W/akcprobe.o" "$OD/usr/lib/libSystem.B.dylib" "$OD/usr/lib/libobjc.A.dylib" -needed_library "$OD/System/Library/Frameworks/CoreFoundation.framework/Versions/A/CoreFoundation" -needed_library "$OD/System/Library/Frameworks/Foundation.framework/Versions/C/Foundation" -needed_library "$OD/System/Library/Frameworks/AppKit.framework/Versions/C/AppKit" -needed_library "$OD/usr/lib/AppKitExtras.dylib" -needed_library "$OD/usr/lib/CoreFoundationExtras.dylib" -needed_library "$OD/usr/lib/FoundationExtras.dylib" -needed_library "$OD/usr/lib/UniformTypeIdentifiersExtras.dylib" 2>"$W/akcprobe.ld.err" \
	|| { echo "AKC RESULT FAIL (build: compile or link error)"; exit 1; }
LOG="$BD/appkit-census-check-$(date +%m%d-%H%M%S).log"
PRE=" $(pgrep -x darlingserver | tr "\n" " ") "
. "$(dirname "$0")/probe-launch.sh"
probe_launch "$LOG" "$WAIT" "AKC DONE\|FATAL signal" sudo timeout -k 5 120 env "DARLING_SRC_DIR=$SRC" "DARLING_OVERLAY=$OD" "DARLING_BUILD_DIR=$BD" "DARLING_TEST_BINARY=$T" \
	DARLING_SMOKE_REFRESH=1 DARLING_STAGING_TREES=usr/lib:System/Library/Frameworks:System/Library/PrivateFrameworks:private/ "$BD/launch-dynamic"
sleep 1
for p in $(pgrep -x darlingserver); do case "$PRE" in *" $p "*) ;; *) sudo kill -9 "$p" 2>/dev/null;; esac; done
grep -a "^AKC" "$LOG"
if grep -qa "^AKC DONE fail=0" "$LOG" && ! grep -qa "^AKC .* FAIL" "$LOG"; then echo "AKC RESULT pass"; exit 0; fi
echo "AKC RESULT FAIL"; exit 1
