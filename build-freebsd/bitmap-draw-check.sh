#!/bin/sh
# bitmap-draw-check.sh - inside Darling, drawing into an NSBitmapImageRep (lockFocusOnRepresentation: and
# +[NSGraphicsContext graphicsContextWithBitmapImageRep:]) must reach the rep's bitmapData: an opaque colour is drawn, getPixel must give it.
# tests/src/bitmap-draw-probe.c is built into tests/bitmap-draw-probe-macho (AppKit + AppKitExtras.dylib) and run under launch-dynamic.
# Usage: sh build-freebsd/bitmap-draw-check.sh [wait-secs]    Env: DARLING_SRC_DIR DARLING_OVERLAY DARLING_BUILD_DIR
# One verdict line: BITMAP-DRAW-VERDICT pass|FAIL lockfocus=<r,g,b,a> ctx=<r,g,b,a> srgb=<ok|nil> srgbgray=<ok|nil> want=51,102,153,255 [reason]; exit 0 on pass.
SRC="${DARLING_SRC_DIR:-}"; BD="${DARLING_BUILD_DIR:-}"; OD="${DARLING_OVERLAY:-}"; WAIT="${1:-120}"
V="BITMAP-DRAW-VERDICT"
if [ -z "$SRC" ] || [ -z "$BD" ] || [ -z "$OD" ]; then echo "$V FAIL lockfocus=none ctx=none want=51,102,153,255 [DARLING_SRC_DIR/DARLING_BUILD_DIR/DARLING_OVERLAY not set]"; exit 1; fi
export PATH="/usr/local/bin:/usr/local/sbin:/usr/bin:/bin:/sbin:/usr/sbin"
SDK_FLAT="$BD/bundle-principal-class-test/sdk-flat"; T=bitmap-draw-probe-macho; W="$BD/bitmap-draw-check"; mkdir -p "$W"
[ -d "$SDK_FLAT/usr/include" ] || { echo "$V FAIL lockfocus=none ctx=none want=51,102,153,255 [no flattened SDK]"; exit 1; }
clang -target x86_64-apple-macos10.12 -nostdinc -D__DARWIN_ONLY_UNIX_CONFORMANCE=1 -O1 -w \
	-I"$SRC/tests/vendor/fakesdk" -I"$SDK_FLAT/usr/include" -c "$SRC/tests/src/bitmap-draw-probe.c" -o "$W/probe.o" &&
ld64.lld -arch x86_64 -platform_version macos 10.12 10.12 -syslibroot "$OD" -Z -o "$SRC/tests/$T" "$W/probe.o" \
	"$OD/usr/lib/AppKitExtras.dylib" \
	"$OD/System/Library/Frameworks/AppKit.framework/Versions/C/AppKit" \
	"$OD/System/Library/Frameworks/Foundation.framework/Versions/C/Foundation" \
	"$OD/System/Library/Frameworks/CoreFoundation.framework/Versions/A/CoreFoundation" "$OD/usr/lib/libobjc.A.dylib" \
	"$OD/usr/lib/libicucore.A.dylib" "$OD/usr/lib/libSystem.B.dylib" 2>"$W/link.err" \
	|| { echo "$V FAIL lockfocus=none ctx=none want=51,102,153,255 [build: $(tail -1 "$W/link.err")]"; exit 1; }
LOG="$BD/bitmap-draw-check.log"
PRE=" $(pgrep -x darlingserver | tr '\n' ' ') "
. "$(dirname "$0")/probe-launch.sh"
probe_launch "$LOG" "$WAIT" "BITMAP-DRAW-PROBE DONE\|FATAL signal" sudo timeout -k 5 "$WAIT" env "DARLING_SRC_DIR=$SRC" "DARLING_OVERLAY=$OD" "DARLING_BUILD_DIR=$BD" "DARLING_TEST_BINARY=$T" \
	DARLING_SMOKE_REFRESH=1 DARLING_STAGING_TREES=usr/lib:System/Library/Frameworks:System/Library/PrivateFrameworks:private/etc "$BD/launch-dynamic"
sleep 1
for p in $(pgrep -x darlingserver); do case "$PRE" in *" $p "*) ;; *) sudo kill -9 "$p" 2>/dev/null;; esac; done
L=$(grep -a "^BITMAP-DRAW-PROBE lockfocus=" "$LOG" | tail -1)
if [ -z "$L" ]; then echo "$V FAIL lockfocus=none ctx=none want=51,102,153,255 [guest died or timed out]"; exit 1; fi
lf=${L#*lockfocus=}; lf=${lf%% *}; cx=${L#*ctx=}; cx=${cx%% *}
sr=${L#*srgb=}; sr=${sr%% *}; sc=${L#*srgbgray=}; sc=${sc%% *}
why=""
[ "$lf" = "51,102,153,255" ] || why="$why lockfocus-not-drawn"
[ "$cx" = "51,102,153,255" ] || why="$why ctx-not-drawn"
[ "$sr" = ok ] || why="$why colorUsingColorSpace-nil"
[ "$sc" = ok ] || why="$why colorUsingColorSpace-gray-nil"
if [ -z "$why" ]; then echo "$V pass lockfocus=$lf ctx=$cx srgb=$sr srgbgray=$sc want=51,102,153,255"; exit 0; fi
echo "$V FAIL lockfocus=$lf ctx=$cx srgb=$sr srgbgray=$sc want=51,102,153,255 [${why# }]"; exit 1
