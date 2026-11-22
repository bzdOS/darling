#!/bin/sh
# nsscreen-check.sh - NSScreen identity (stable screens/mainScreen, NSScreenNumber == CGMainDisplayID), NSWindow backingScaleFactor/occlusionState work (tests/src/nsscreen-probe.c).
# Builds tests/nsscreen-probe-macho from tests/src/nsscreen-probe.c and runs it under launch-dynamic.
# Usage: sh build-freebsd/nsscreen-check.sh [wait-secs]    Env: DARLING_SRC_DIR DARLING_OVERLAY DARLING_BUILD_DIR
# Prints NSS lines and a final NSS RESULT pass|FAIL; exit 0 on pass, 1 otherwise.
# Needs a Wayland compositor: WAYLAND_DISPLAY (default wayland-1) and XDG_RUNTIME_DIR (default /tmp/wayland-test) are passed to the guest and exposed.
# Only the darlingserver processes this run started are stopped (by pid); a pre-existing one is left alone.
SRC="${DARLING_SRC_DIR:?}"; BD="${DARLING_BUILD_DIR:?}"; OD="${DARLING_OVERLAY:?}"; WAIT="${1:-60}"
export PATH="/usr/local/bin:/usr/local/sbin:/usr/bin:/bin:/sbin:/usr/sbin"
T=nsscreen-probe-macho; W="$BD/sc-stubs"; mkdir -p "$W"
clang -target x86_64-apple-macos10.12 -O1 -w -ffreestanding -c "$SRC/tests/src/nsscreen-probe.c" -o "$W/nssprobe.o" &&
ld64.lld -arch x86_64 -platform_version macos 10.12 10.12 -syslibroot "$OD" -Z -o "$SRC/tests/$T" "$W/nssprobe.o" "$OD/usr/lib/libSystem.B.dylib" "$OD/usr/lib/libobjc.A.dylib" -needed_library "$OD/System/Library/Frameworks/CoreFoundation.framework/Versions/A/CoreFoundation" -needed_library "$OD/System/Library/Frameworks/Foundation.framework/Versions/C/Foundation" -needed_library "$OD/System/Library/Frameworks/CoreGraphics.framework/Versions/A/CoreGraphics" -needed_library "$OD/System/Library/Frameworks/AppKit.framework/Versions/C/AppKit" -needed_library "$OD/usr/lib/AppKitExtras.dylib" -needed_library "$OD/usr/lib/CoreFoundationExtras.dylib" -needed_library "$OD/usr/lib/FoundationExtras.dylib" -needed_library "$OD/usr/lib/UniformTypeIdentifiersExtras.dylib" 2>"$W/nssprobe.ld.err" \
	|| { echo "NSS RESULT FAIL (build: compile or link error)"; exit 1; }
LOG="$BD/nsscreen-check.log"
PRE=" $(pgrep -x darlingserver | tr "\n" " ") "
. "$(dirname "$0")/probe-launch.sh"
probe_launch "$LOG" "$WAIT" "NSS DONE\|FATAL signal" sudo timeout -k 5 120 env "DARLING_SRC_DIR=$SRC" "DARLING_OVERLAY=$OD" "DARLING_BUILD_DIR=$BD" "DARLING_TEST_BINARY=$T" "WAYLAND_DISPLAY=${WAYLAND_DISPLAY:-wayland-1}" "XDG_RUNTIME_DIR=${XDG_RUNTIME_DIR:-/tmp/wayland-test}" "DARLING_GUEST_EXPOSE=${XDG_RUNTIME_DIR:-/tmp/wayland-test}" \
	DARLING_SMOKE_REFRESH=1 DARLING_STAGING_TREES=usr/lib:System/Library/Frameworks:System/Library/PrivateFrameworks:private/ "$BD/launch-dynamic"
sleep 1
for p in $(pgrep -x darlingserver); do case "$PRE" in *" $p "*) ;; *) sudo kill -9 "$p" 2>/dev/null;; esac; done
grep -a "^NSS" "$LOG"
if grep -qa "^NSS DONE fail=0" "$LOG" && ! grep -qa "^NSS .* FAIL" "$LOG"; then echo "NSS RESULT pass"; exit 0; fi
echo "NSS RESULT FAIL"; exit 1
