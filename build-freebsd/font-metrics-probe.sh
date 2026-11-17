#!/bin/sh
# font-metrics-probe.sh - inside Darling, the font metric accessors a browser's font code calls (NSFont ascender, descender,
# capHeight, leading, xHeight, pointSize and the CTFontGet*/CTFontCopy* behind them) must not raise, must not answer NULL, and must
# answer sane values. tests/src/font-metrics-probe.c is built into tests/font-metrics-probe-macho (AppKit, CoreText, AppKitExtras,
# CoreTextExtras linked, so the stub behaviours are in the process).
# Usage: sh build-freebsd/font-metrics-probe.sh [wait-secs]    Env: DARLING_SRC_DIR DARLING_OVERLAY DARLING_BUILD_DIR
# One verdict line: FONT-METRICS-VERDICT pass|fail n=<checks> bad=[names]; exit 0 on pass. Per-check lines are in the log.
# Only the darlingserver processes this run started are stopped (by pid).
SRC="${DARLING_SRC_DIR:?}"; BD="${DARLING_BUILD_DIR:?}"; OD="${DARLING_OVERLAY:?}"; WAIT="${1:-90}"
export PATH="/usr/local/bin:/usr/local/sbin:/usr/bin:/bin:/sbin:/usr/sbin"
SDK_FLAT="$BD/bundle-principal-class-test/sdk-flat"; T=font-metrics-probe-macho; W="$BD/font-metrics-probe"; mkdir -p "$W"
F0="FONT-METRICS-VERDICT fail n=0"
[ -d "$SDK_FLAT/usr/include" ] || { echo "$F0 bad=[no flattened SDK]"; exit 1; }
clang -target x86_64-apple-macos10.12 -nostdinc -D__DARWIN_ONLY_UNIX_CONFORMANCE=1 -O1 -w \
	-I"$SRC/tests/vendor/fakesdk" -I"$SDK_FLAT/usr/include" -c "$SRC/tests/src/font-metrics-probe.c" -o "$W/probe.o" &&
ld64.lld -arch x86_64 -platform_version macos 10.12 10.12 -syslibroot "$OD" -Z -o "$SRC/tests/$T" "$W/probe.o" \
	"$OD/usr/lib/AppKitExtras.dylib" "$OD/usr/lib/CoreTextExtras.dylib" \
	"$OD/System/Library/Frameworks/AppKit.framework/Versions/C/AppKit" \
	"$OD/System/Library/Frameworks/CoreText.framework/Versions/A/CoreText" \
	"$OD/System/Library/Frameworks/CoreGraphics.framework/Versions/A/CoreGraphics" \
	"$OD/System/Library/Frameworks/Foundation.framework/Versions/C/Foundation" \
	"$OD/System/Library/Frameworks/CoreFoundation.framework/Versions/A/CoreFoundation" "$OD/usr/lib/libobjc.A.dylib" \
	"$OD/usr/lib/libicucore.A.dylib" "$OD/usr/lib/libSystem.B.dylib" 2>"$W/link.err" \
	|| { echo "$F0 bad=[build: $(tail -1 "$W/link.err")]"; exit 1; }
LOG="$BD/font-metrics-probe.log"
PRE=" $(pgrep -x darlingserver | tr '\n' ' ') "
( sudo timeout -k 5 "$WAIT" env "DARLING_SRC_DIR=$SRC" "DARLING_OVERLAY=$OD" "DARLING_BUILD_DIR=$BD" "DARLING_TEST_BINARY=$T" \
	DARLING_SMOKE_REFRESH=1 DARLING_STAGING_TREES=usr/lib:System/Library/Frameworks:System/Library/PrivateFrameworks:private/etc "$BD/launch-dynamic" > "$LOG" 2>&1 & )
i=0; while [ $i -lt "$WAIT" ]; do grep -qa "FONTM-PROBE DONE\|FONTM-SUMMARY\|FATAL signal" "$LOG" 2>/dev/null && break; sleep 1; i=$((i+1)); done
sleep 1
for p in $(pgrep -x darlingserver); do case "$PRE" in *" $p "*) ;; *) sudo kill -9 "$p" 2>/dev/null;; esac; done
L=$(grep -a "^FONTM-SUMMARY " "$LOG" | tail -1)
if [ -z "$L" ]; then
	last=$(grep -a "^FONTM-T " "$LOG" | tail -1 | awk '{print $2}')
	echo "FONT-METRICS-VERDICT fail n=0 bad=[died-after-${last:-start}] log=$LOG"; exit 1
fi
c=${L#*n=}; c=${c%% *}; b=${L#*bad=}; b=${b%% *}
if [ "$b" = "-" ]; then echo "FONT-METRICS-VERDICT pass n=$c bad=[]"; exit 0; fi
exc=$(grep -a "^FONTM-EXC" "$LOG" | tail -1 | cut -c1-200)
echo "FONT-METRICS-VERDICT fail n=$c bad=[$b]${exc:+ $exc}"; exit 1
