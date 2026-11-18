#!/bin/sh
# nscgcolor-check.sh - inside Darling, every +[NSColor xxxColor] of the system catalogue answers -CGColor with a real
# CGColorRef (CGColorGetNumberOfComponents > 0) and CGColorGetNumberOfComponents(NULL) does not crash.
# tests/src/nscgcolor-probe.c is built into tests/nscgcolor-probe-macho (linked against AppKit, CoreGraphics and
# AppKitExtras.dylib, so the behaviour of stub-frameworks/behaviors/AppKitExtras.dylib/color.c is in the process).
# Usage: sh build-freebsd/nscgcolor-check.sh [wait-secs]    Env: DARLING_SRC_DIR DARLING_OVERLAY DARLING_BUILD_DIR
# One verdict line: NSCGCOLOR-VERDICT pass|FAIL colors=<n> bad=<n> first=<name> null=<yes|no> [reason]; exit 0 on pass.
# Only the darlingserver processes this run started are stopped (by pid).
SRC="${DARLING_SRC_DIR:?}"; BD="${DARLING_BUILD_DIR:?}"; OD="${DARLING_OVERLAY:?}"; WAIT="${1:-120}"
export PATH="/usr/local/bin:/usr/local/sbin:/usr/bin:/bin:/sbin:/usr/sbin"
SDK_FLAT="$BD/bundle-principal-class-test/sdk-flat"; T=nscgcolor-probe-macho; W="$BD/nscgcolor-check"; mkdir -p "$W"
F0="NSCGCOLOR-VERDICT FAIL colors=0 bad=0 first=none null=no"
[ -d "$SDK_FLAT/usr/include" ] || { echo "$F0 [no flattened SDK]"; exit 1; }
clang -target x86_64-apple-macos10.12 -nostdinc -D__DARWIN_ONLY_UNIX_CONFORMANCE=1 -O1 -w \
	-I"$SRC/tests/vendor/fakesdk" -I"$SDK_FLAT/usr/include" -c "$SRC/tests/src/nscgcolor-probe.c" -o "$W/probe.o" &&
ld64.lld -arch x86_64 -platform_version macos 10.12 10.12 -syslibroot "$OD" -Z -o "$SRC/tests/$T" "$W/probe.o" \
	"$OD/usr/lib/AppKitExtras.dylib" \
	"$OD/System/Library/Frameworks/AppKit.framework/Versions/C/AppKit" \
	"$OD/System/Library/Frameworks/CoreGraphics.framework/Versions/A/CoreGraphics" \
	"$OD/System/Library/Frameworks/Foundation.framework/Versions/C/Foundation" \
	"$OD/System/Library/Frameworks/CoreFoundation.framework/Versions/A/CoreFoundation" "$OD/usr/lib/libobjc.A.dylib" \
	"$OD/usr/lib/libicucore.A.dylib" "$OD/usr/lib/libSystem.B.dylib" 2>"$W/link.err" \
	|| { echo "$F0 [build: $(tail -1 "$W/link.err")]"; exit 1; }
LOG="$BD/nscgcolor-check.log"
PRE=" $(pgrep -x darlingserver | tr '\n' ' ') "
. "$(dirname "$0")/probe-launch.sh"
probe_launch "$LOG" "$WAIT" "NSCGCOLOR-PROBE DONE\|FATAL signal" sudo timeout -k 5 "$WAIT" env "DARLING_SRC_DIR=$SRC" "DARLING_OVERLAY=$OD" "DARLING_BUILD_DIR=$BD" "DARLING_TEST_BINARY=$T" \
	DARLING_SMOKE_REFRESH=1 DARLING_STAGING_TREES=usr/lib:System/Library/Frameworks:System/Library/PrivateFrameworks:private/etc "$BD/launch-dynamic"
sleep 1
for p in $(pgrep -x darlingserver); do case "$PRE" in *" $p "*) ;; *) sudo kill -9 "$p" 2>/dev/null;; esac; done
L=$(grep -a "^NSCGCOLOR-SUMMARY " "$LOG" | tail -1)
if [ -z "$L" ]; then echo "$F0 [guest died or timed out]"; exit 1; fi
col=${L#*colors=}; col=${col%% *}; bad=${L#*bad=}; bad=${bad%% *}; first=${L#*first=}; first=${first%% *}; nul=${L#*null=}; nul=${nul%% *}
if [ "$bad" = 0 ] && [ "$nul" = yes ]; then echo "NSCGCOLOR-VERDICT pass colors=$col bad=$bad first=$first null=$nul"; exit 0; fi
echo "NSCGCOLOR-VERDICT FAIL colors=$col bad=$bad first=$first null=$nul"; exit 1
