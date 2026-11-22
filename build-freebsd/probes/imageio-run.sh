#!/bin/sh
# imageio-run.sh - builds and runs tests/imageio-probe-macho in a live darling process with the deployed ImageIO and AppKit.
# Prints two lines:
#   IMAGEIO-DECODE pass|FAIL w= h= want=4x3 count= garbage=<null|obj>   (8-bit PNG decoded, size matches, garbage gives a NULL source)
#   IMGREP-NULL pass|FAIL rc=<n> canInit=<0|1|none>                     (+[NSBitmapImageRep canInitWithData:] on garbage: NO, no crash, rc=0)
# Usage: sh build-freebsd/probes/imageio-run.sh [wait-secs]   Env: DARLING_SRC_DIR DARLING_OVERLAY DARLING_BUILD_DIR
# Only the darlingserver processes this run started are stopped (by pid).
SRC="${DARLING_SRC_DIR:?}"; BD="${DARLING_BUILD_DIR:?}"; OD="${DARLING_OVERLAY:?}"; WAIT="${1:-90}"
export PATH="/usr/local/bin:/usr/local/sbin:/usr/bin:/bin:/sbin:/usr/sbin"
SDK_FLAT="$BD/bundle-principal-class-test/sdk-flat"; T=imageio-probe-macho
W=$(mktemp -d "$BD/imageio-probe.XXXXXX") || { echo "FATAL: mktemp workdir failed for imageio-probe" >&2; exit 1; }
cleanup() { rm -rf "$W"; }
trap cleanup EXIT INT TERM HUP
F1="IMAGEIO-DECODE FAIL"; F2="IMGREP-NULL FAIL"
[ -d "$SDK_FLAT/usr/include" ] || { echo "$F1 [no flattened SDK]"; echo "$F2 [no flattened SDK]"; exit 1; }
clang -target x86_64-apple-macos10.12 -nostdinc -D__DARWIN_ONLY_UNIX_CONFORMANCE=1 -O1 -w \
	-I"$SRC/tests/vendor/fakesdk" -I"$SDK_FLAT/usr/include" -c "$SRC/build-freebsd/probes/imageio-probe.c" -o "$W/probe.o" &&
ld64.lld -arch x86_64 -platform_version macos 10.12 10.12 -syslibroot "$OD" -Z -o "$SRC/tests/$T" "$W/probe.o" \
	"$OD/System/Library/Frameworks/ImageIO.framework/Versions/A/ImageIO" \
	"$OD/System/Library/Frameworks/AppKit.framework/Versions/C/AppKit" \
	"$OD/System/Library/Frameworks/CoreGraphics.framework/Versions/A/CoreGraphics" \
	"$OD/System/Library/Frameworks/Foundation.framework/Versions/C/Foundation" \
	"$OD/System/Library/Frameworks/CoreFoundation.framework/Versions/A/CoreFoundation" "$OD/usr/lib/libobjc.A.dylib" \
	"$OD/usr/lib/libicucore.A.dylib" "$OD/usr/lib/libSystem.B.dylib" 2>"$W/link.err" \
	|| { echo "$F1 [build: $(tail -1 "$W/link.err")]"; echo "$F2 [build]"; exit 1; }
LOG="$BD/imageio-probe-$(date +%m%d-%H%M%S).log"
PRE=" $(pgrep -x darlingserver | tr '\n' ' ') "
( sudo timeout -k 5 "$WAIT" env "DARLING_SRC_DIR=$SRC" "DARLING_OVERLAY=$OD" "DARLING_BUILD_DIR=$BD" "DARLING_TEST_BINARY=$T" \
	$([ "${STAGE_GATE_KEEP_STAGED:-}" = 1 ] && echo DARLING_KEEP_LOCAL=1 || echo DARLING_SMOKE_REFRESH=1) DARLING_STAGING_TREES=usr/lib:System/Library/Frameworks:System/Library/PrivateFrameworks:private/etc "$BD/launch-dynamic" > "$LOG" 2>&1 & )
i=0; while [ $i -lt "$WAIT" ]; do grep -qa "IMAGEIO-PROBE-DONE\|FATAL signal" "$LOG" 2>/dev/null && break; grep -qa "IMGREP-BEGIN" "$LOG" 2>/dev/null && [ $i -gt 20 ] && break; sleep 1; i=$((i+1)); done
sleep 2
for p in $(pgrep -x darlingserver); do case "$PRE" in *" $p "*) ;; *) sudo kill -9 "$p" 2>/dev/null;; esac; done
L=$(grep -a "^IMAGEIO-R " "$LOG" | tail -1)
if [ -z "$L" ]; then echo "$F1 [no result] log=$LOG"; echo "$F2 [no result]"; exit 1; fi
w=${L#*w=}; w=${w%% *}; h=${L#*h=}; h=${h%% *}; c=${L#*count=}; c=${c%% *}; g=${L#*garbage=}
if [ "$w" = 4 ] && [ "$h" = 3 ] && [ "$c" -ge 1 ] && [ "$g" = null ]; then R1=0; echo "IMAGEIO-DECODE pass w=$w h=$h want=4x3 count=$c garbage=$g"
else R1=1; echo "$F1 w=$w h=$h want=4x3 count=$c garbage=$g"; fi
K=$(grep -a "^IMGREP-R " "$LOG" | tail -1)
if [ -n "$K" ] && grep -qa "IMAGEIO-PROBE-DONE" "$LOG" && [ "${K#*canInit=}" = 0 ]; then echo "IMGREP-NULL pass rc=0 canInit=0"
else echo "$F2 rc=crash canInit=${K#*canInit=} [$(grep -a 'FATAL signal\|SIGILL\|SIGTRAP\|int3' "$LOG" | tail -1)]"; fi
exit $R1
