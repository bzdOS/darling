#!/bin/sh
# font-resolve-probe.sh - in a live darling process with darling-extras.dylib injected (as Chrome runs), CTFontCreateWithName("Helvetica",12)
# and +[NSFont systemFontOfSize:12] must not be nil, and the flat lookup of CTFontCreateWithName must not land in darling-extras.dylib.
# tests/src/font-resolve-probe.c is built into tests/font-resolve-probe-macho (links the same images as font-metrics-probe.sh).
# Usage: sh build-freebsd/font-resolve-probe.sh [wait-secs]    Env: DARLING_SRC_DIR DARLING_OVERLAY DARLING_BUILD_DIR
# One verdict line: FONT-RESOLVE-VERDICT pass|fail ct=<nonnil|NIL> nsfont=<nonnil|NIL> img=<image> extras-loaded=<0|1>; exit 0 on pass.
# Only the darlingserver processes this run started are stopped (by pid).
SRC="${DARLING_SRC_DIR:?}"; BD="${DARLING_BUILD_DIR:?}"; OD="${DARLING_OVERLAY:?}"; WAIT="${1:-90}"
export PATH="/usr/local/bin:/usr/local/sbin:/usr/bin:/bin:/sbin:/usr/sbin"
SDK_FLAT="$BD/bundle-principal-class-test/sdk-flat"; T=font-resolve-probe-macho; W="$BD/font-resolve-probe"; mkdir -p "$W"
F0="FONT-RESOLVE-VERDICT fail"
[ -d "$SDK_FLAT/usr/include" ] || { echo "$F0 [no flattened SDK]"; exit 1; }
clang -target x86_64-apple-macos10.12 -nostdinc -D__DARWIN_ONLY_UNIX_CONFORMANCE=1 -O1 -w \
	-I"$SRC/tests/vendor/fakesdk" -I"$SDK_FLAT/usr/include" -c "$SRC/tests/src/font-resolve-probe.c" -o "$W/probe.o" &&
ld64.lld -arch x86_64 -platform_version macos 10.12 10.12 -syslibroot "$OD" -Z -o "$SRC/tests/$T" "$W/probe.o" \
	"$OD/usr/lib/AppKitExtras.dylib" "$OD/usr/lib/CoreTextExtras.dylib" \
	"$OD/System/Library/Frameworks/AppKit.framework/Versions/C/AppKit" \
	"$OD/System/Library/Frameworks/CoreText.framework/Versions/A/CoreText" \
	"$OD/System/Library/Frameworks/CoreGraphics.framework/Versions/A/CoreGraphics" \
	"$OD/System/Library/Frameworks/Foundation.framework/Versions/C/Foundation" \
	"$OD/System/Library/Frameworks/CoreFoundation.framework/Versions/A/CoreFoundation" "$OD/usr/lib/libobjc.A.dylib" \
	"$OD/usr/lib/libicucore.A.dylib" "$OD/usr/lib/libSystem.B.dylib" 2>"$W/link.err" \
	|| { echo "$F0 [build: $(tail -1 "$W/link.err")]"; exit 1; }
LOG="$BD/font-resolve-probe.log"
PRE=" $(pgrep -x darlingserver | tr '\n' ' ') "
. "$(dirname "$0")/probe-launch.sh"
probe_launch "$LOG" "$WAIT" "FONTRES-DONE\|FATAL signal" sudo timeout -k 5 "$WAIT" env "DARLING_SRC_DIR=$SRC" "DARLING_OVERLAY=$OD" "DARLING_BUILD_DIR=$BD" "DARLING_TEST_BINARY=$T" \
	DARLING_INSERT_EXTRA=/usr/lib/darling-extras.dylib \
	$([ "${STAGE_GATE_KEEP_STAGED:-}" = 1 ] && echo DARLING_KEEP_LOCAL=1 || echo DARLING_SMOKE_REFRESH=1) DARLING_STAGING_TREES=usr/lib:System/Library/Frameworks:System/Library/PrivateFrameworks:private/etc "$BD/launch-dynamic"
sleep 1
for p in $(pgrep -x darlingserver); do case "$PRE" in *" $p "*) ;; *) sudo kill -9 "$p" 2>/dev/null;; esac; done
L=$(grep -a "^FONTRES-R " "$LOG" | tail -1)
if [ -z "$L" ]; then echo "$F0 [no result] log=$LOG"; exit 1; fi
r=${L#FONTRES-R }
case "$r" in
*"ct=nonnil nsfont=nonnil"*"extras-loaded=1"*)
	case "$r" in *"img=darling-extras"*) echo "FONT-RESOLVE-VERDICT fail $r [flat lookup shadowed by darling-extras]"; exit 1;; esac
	echo "FONT-RESOLVE-VERDICT pass $r"; exit 0;;
esac
echo "FONT-RESOLVE-VERDICT fail $r"; exit 1
