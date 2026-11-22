#!/bin/sh
# tz-check.sh - inside Darling, time zone setup must terminate and work: tzset, localtime_r, CFTimeZoneCopySystem on the
# main thread and on a 64 KiB-stack thread (tests/src/tz-probe.c, built to tests/tz-probe-macho, run under launch-dynamic).
# Before the zone data fix the guest opened /etc/localtime and /var/db/timezone/zoneinfo/UTC (absent) over and over until the stack ran out.
# Usage: sh build-freebsd/tz-check.sh [wait-secs]    Env: DARLING_SRC_DIR DARLING_OVERLAY DARLING_BUILD_DIR
# Prints one verdict line "TZ-VERDICT pass ..." or "TZ-VERDICT FAIL ..."; exit 0 on pass. Only darlingserver processes this run started are stopped (by pid).
SRC="${DARLING_SRC_DIR:?}"; BD="${DARLING_BUILD_DIR:?}"; OD="${DARLING_OVERLAY:?}"; WAIT="${1:-90}"
export PATH="/usr/local/bin:/usr/local/sbin:/usr/bin:/bin:/sbin:/usr/sbin"
SDK_FLAT="$BD/bundle-principal-class-test/sdk-flat"; T=tz-probe-macho
W=$(mktemp -d "$BD/tz-check.XXXXXX") || { echo "FATAL: mktemp workdir failed for tz-check" >&2; exit 1; }
cleanup() { rm -rf "$W"; }
trap cleanup EXIT INT TERM HUP
[ -d "$SDK_FLAT/usr/include" ] || { echo "TZ-VERDICT FAIL [no flattened SDK]"; exit 1; }
clang -target x86_64-apple-macos10.12 -nostdinc -D__DARWIN_ONLY_UNIX_CONFORMANCE=1 -O1 -w \
	-I"$SRC/tests/vendor/fakesdk" -I"$SDK_FLAT/usr/include" -c "$SRC/tests/src/tz-probe.c" -o "$W/probe.o" &&
ld64.lld -arch x86_64 -platform_version macos 10.12 10.12 -syslibroot "$OD" -Z -o "$SRC/tests/$T" "$W/probe.o" \
	"$OD/System/Library/Frameworks/CoreFoundation.framework/Versions/A/CoreFoundation" "$OD/usr/lib/libobjc.A.dylib" "$OD/usr/lib/libicucore.A.dylib" "$OD/usr/lib/libSystem.B.dylib" 2>/dev/null \
	|| { echo "TZ-VERDICT FAIL [build]"; exit 1; }
LOG="$BD/tz-check-$(date +%m%d-%H%M%S).log"
PRE=" $(pgrep -x darlingserver | tr '\n' ' ') "
. "$(dirname "$0")/probe-launch.sh"
probe_launch "$LOG" "$WAIT" "^TZ-VERDICT\|FATAL signal" sudo timeout -k 5 "$WAIT" env "DARLING_SRC_DIR=$SRC" "DARLING_OVERLAY=$OD" "DARLING_BUILD_DIR=$BD" "DARLING_TEST_BINARY=$T" \
	DARLING_SMOKE_REFRESH=1 DARLING_STAGING_TREES=usr/lib:System/Library/Frameworks:System/Library/PrivateFrameworks:private/etc:private/var/db "$BD/launch-dynamic"
sleep 1
for p in $(pgrep -x darlingserver); do case "$PRE" in *" $p "*) ;; *) sudo kill -9 "$p" 2>/dev/null;; esac; done
grep -a "^TZPROBE" "$LOG" | head
if grep -qa "^TZ-VERDICT" "$LOG"; then
	grep -a "^TZ-VERDICT" "$LOG" | tail -1
	grep -qa "^TZ-VERDICT pass" "$LOG" && exit 0; exit 1
fi
echo "TZ-VERDICT FAIL [guest died or timed out, no verdict]"; exit 1
