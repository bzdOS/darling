#!/bin/sh
# font-families-probe.sh - in a live darling process (AppKitExtras.dylib linked and loaded), [[NSDisplay currentDisplay] allFontFamilyNames] must answer a non-empty set instead of
# raising "-allFontFamilyNames only defined for abstract class" (WaylandDisplay did not override it), and -fontTypefacesForFamilyName:
# (the next call on the font path, from +[NSFontFamily buildFontFamilies]) must answer a non-empty array.
# tests/src/font-families-probe.c is built into tests/font-families-probe-macho (links the same images as font-resolve-probe.sh).
# Usage: sh build-freebsd/font-families-probe.sh [wait-secs]    Env: DARLING_SRC_DIR DARLING_OVERLAY DARLING_BUILD_DIR
# One verdict line: FONT-FAMILIES-VERDICT pass|fail threw=<y|n> count=<n> imp=<image>; exit 0 on pass.
# and the IMP serving -allFontFamilyNames must lie in the AppKitExtras image (dladdr; behaviour of stub-frameworks/behaviors/AppKitExtras.dylib/font.c).
# Needs the deployed AppKitExtras.dylib (or the one under DARLING_OVERLAY) to carry that behaviour. Only the darlingserver processes this run started
# are stopped (by pid).
SRC="${DARLING_SRC_DIR:?}"; BD="${DARLING_BUILD_DIR:?}"; OD="${DARLING_OVERLAY:?}"; WAIT="${1:-90}"
export PATH="/usr/local/bin:/usr/local/sbin:/usr/bin:/bin:/sbin:/usr/sbin"
SDK_FLAT="$BD/bundle-principal-class-test/sdk-flat"; T=font-families-probe-macho; W="$BD/font-families-probe"; mkdir -p "$W"
F0="FONT-FAMILIES-VERDICT fail threw=n count=-1 imp=?"
[ -d "$SDK_FLAT/usr/include" ] || { echo "$F0 [no flattened SDK]"; exit 1; }
clang -target x86_64-apple-macos10.12 -nostdinc -D__DARWIN_ONLY_UNIX_CONFORMANCE=1 -O1 -w \
	-I"$SRC/tests/vendor/fakesdk" -I"$SDK_FLAT/usr/include" -c "$SRC/tests/src/font-families-probe.c" -o "$W/probe.o" &&
ld64.lld -arch x86_64 -platform_version macos 10.12 10.12 -syslibroot "$OD" -Z -o "$SRC/tests/$T" "$W/probe.o" \
	"$OD/usr/lib/AppKitExtras.dylib" \
	"$OD/System/Library/Frameworks/AppKit.framework/Versions/C/AppKit" \
	"$OD/System/Library/Frameworks/CoreText.framework/Versions/A/CoreText" \
	"$OD/System/Library/Frameworks/CoreGraphics.framework/Versions/A/CoreGraphics" \
	"$OD/System/Library/Frameworks/Foundation.framework/Versions/C/Foundation" \
	"$OD/System/Library/Frameworks/CoreFoundation.framework/Versions/A/CoreFoundation" "$OD/usr/lib/libobjc.A.dylib" \
	"$OD/usr/lib/libicucore.A.dylib" "$OD/usr/lib/libSystem.B.dylib" 2>"$W/link.err" \
	|| { echo "$F0 [build: $(tail -1 "$W/link.err")]"; exit 1; }
LOG="$BD/font-families-probe.log"
PRE=" $(pgrep -x darlingserver | tr '\n' ' ') "
( sudo timeout -k 5 "$WAIT" env "DARLING_SRC_DIR=$SRC" "DARLING_OVERLAY=$OD" "DARLING_BUILD_DIR=$BD" "DARLING_TEST_BINARY=$T" \
	DARLING_SMOKE_REFRESH=1 DARLING_STAGING_TREES=usr/lib:System/Library/Frameworks:System/Library/PrivateFrameworks:private/etc "$BD/launch-dynamic" > "$LOG" 2>&1 & )
i=0; while [ $i -lt "$WAIT" ]; do grep -qa "FAMILIES-DONE\|FATAL signal\|only defined for abstract class\|uncaught exception" "$LOG" 2>/dev/null && break; sleep 1; i=$((i+1)); done
sleep 1
for p in $(pgrep -x darlingserver); do case "$PRE" in *" $p "*) ;; *) sudo kill -9 "$p" 2>/dev/null;; esac; done
THREW=n; grep -qa "only defined for abstract class\|uncaught exception" "$LOG" && THREW=y
L=$(grep -a "^FAMILIES-R " "$LOG" | tail -1)
if [ -z "$L" ]; then
	[ "$THREW" = n ] && grep -qa "FAMILIES-START" "$LOG" && THREW=y
	echo "$F0 [no result, threw=$THREW] log=$LOG" | sed "s/threw=n count=-1 imp=? \[/threw=$THREW count=-1 imp=? [/"; exit 1
fi
CNT=$(echo "$L" | sed 's/.*count=\([-0-9]*\).*/\1/'); FACES=$(echo "$L" | sed 's/.*faces=\([-0-9]*\).*/\1/')
IMP=$(echo "$L" | sed 's/.* imp=//')
case "$IMP" in *AppKitExtras*) INX=1;; *) INX=0;; esac
if [ "$THREW" = n ] && [ "$CNT" -gt 0 ] && [ "$FACES" -gt 0 ] && [ "$INX" = 1 ]; then echo "FONT-FAMILIES-VERDICT pass threw=n count=$CNT imp=$IMP"; exit 0; fi
echo "FONT-FAMILIES-VERDICT fail threw=$THREW count=$CNT imp=$IMP [$L]"; exit 1
