#!/bin/sh
# bitmap-check.sh - inside Darling, -[NSBitmapImageRep getPixel:atX:y:] and -colorAtX:y: must return the pixel and be fast.
# tests/src/bitmap-probe.c is built into tests/bitmap-probe-macho (linked against AppKit and AppKitExtras.dylib, so the
# behaviour of stub-frameworks/behaviors/AppKitExtras.dylib/bitmap.c is in the process) and run under launch-dynamic.
# Usage: sh build-freebsd/bitmap-check.sh [wait-secs]    Env: DARLING_SRC_DIR DARLING_OVERLAY DARLING_BUILD_DIR
# One verdict line: BITMAP-VERDICT pass|FAIL got=<r,g,b,a> want=10,20,30,255 color=<ok|nil> ms_per_call=<n> [reason]; exit 0 on pass.
# Only the darlingserver processes this run started are stopped (by pid).
SRC="${DARLING_SRC_DIR:?}"; BD="${DARLING_BUILD_DIR:?}"; OD="${DARLING_OVERLAY:?}"; WAIT="${1:-120}"
export PATH="/usr/local/bin:/usr/local/sbin:/usr/bin:/bin:/sbin:/usr/sbin"
SDK_FLAT="$BD/bundle-principal-class-test/sdk-flat"; T=bitmap-probe-macho
W=$(mktemp -d "$BD/bitmap-check.XXXXXX") || { echo "FATAL: mktemp workdir failed for bitmap-check" >&2; exit 1; }
cleanup() { rm -rf "$W"; }
trap cleanup EXIT INT TERM HUP
[ -d "$SDK_FLAT/usr/include" ] || { echo "BITMAP-VERDICT FAIL got=none want=10,20,30,255 color=nil ms_per_call=0 [no flattened SDK]"; exit 1; }
clang -target x86_64-apple-macos10.12 -nostdinc -D__DARWIN_ONLY_UNIX_CONFORMANCE=1 -O1 -w \
	-I"$SRC/tests/vendor/fakesdk" -I"$SDK_FLAT/usr/include" -c "$SRC/tests/src/bitmap-probe.c" -o "$W/probe.o" &&
ld64.lld -arch x86_64 -platform_version macos 10.12 10.12 -syslibroot "$OD" -Z -o "$SRC/tests/$T" "$W/probe.o" \
	"$OD/usr/lib/AppKitExtras.dylib" \
	"$OD/System/Library/Frameworks/AppKit.framework/Versions/C/AppKit" \
	"$OD/System/Library/Frameworks/Foundation.framework/Versions/C/Foundation" \
	"$OD/System/Library/Frameworks/CoreFoundation.framework/Versions/A/CoreFoundation" "$OD/usr/lib/libobjc.A.dylib" \
	"$OD/usr/lib/libicucore.A.dylib" "$OD/usr/lib/libSystem.B.dylib" 2>"$W/link.err" \
	|| { echo "BITMAP-VERDICT FAIL got=none want=10,20,30,255 color=nil ms_per_call=0 [build: $(tail -1 "$W/link.err")]"; exit 1; }
LOG="$BD/bitmap-check-$(date +%m%d-%H%M%S).log"
PRE=" $(pgrep -x darlingserver | tr '\n' ' ') "
. "$(dirname "$0")/probe-launch.sh"
probe_launch "$LOG" "$WAIT" "BITMAP-PROBE DONE\|FATAL signal" sudo timeout -k 5 "$WAIT" env "DARLING_SRC_DIR=$SRC" "DARLING_OVERLAY=$OD" "DARLING_BUILD_DIR=$BD" "DARLING_TEST_BINARY=$T" \
	DARLING_SMOKE_REFRESH=1 DARLING_STAGING_TREES=usr/lib:System/Library/Frameworks:System/Library/PrivateFrameworks:private/etc "$BD/launch-dynamic"
sleep 1
for p in $(pgrep -x darlingserver); do case "$PRE" in *" $p "*) ;; *) sudo kill -9 "$p" 2>/dev/null;; esac; done
L=$(grep -a "^BITMAP-PROBE got=" "$LOG" | tail -1)
if [ -z "$L" ]; then echo "BITMAP-VERDICT FAIL got=none want=10,20,30,255 color=nil ms_per_call=0 [guest died or timed out]"; exit 1; fi
got=${L#*got=}; got=${got%% *}; col=${L#*color=}; col=${col%% *}
calls=${L#*calls=}; calls=${calls%% *}; ms=${L#*ms=}; ms=${ms%% *}
per=$(awk -v m="$ms" -v c="$calls" 'BEGIN{printf "%.3f", c ? m / c : 999}')
why=""
[ "$got" = "10,20,30,255" ] || why="$why pixel-not-returned"
[ "$col" = ok ] || why="$why colorAtX-nil"
awk -v p="$per" 'BEGIN{exit !(p > 1.0)}' && why="$why slow"
if [ -z "$why" ]; then echo "BITMAP-VERDICT pass got=$got want=10,20,30,255 color=$col ms_per_call=$per"; exit 0; fi
echo "BITMAP-VERDICT FAIL got=$got want=10,20,30,255 color=$col ms_per_call=$per [${why# }]"; exit 1
