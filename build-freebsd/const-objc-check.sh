#!/bin/sh
# const-objc-check.sh - inside Darling, every string constant that a stub image exports must be a valid string object:
# CFGetTypeID == CFStringGetTypeID, [obj isKindOfClass:[NSString class]], [obj length] == CFStringGetLength, no crash.
# The constant list is generated from the overlay (gen-const-objc-table.py); tests/src/const-objc-probe.c is built into
# tests/const-objc-probe-macho and run under launch-dynamic.
# Usage: sh build-freebsd/const-objc-check.sh [wait-secs]    Env: DARLING_SRC_DIR DARLING_OVERLAY DARLING_BUILD_DIR
# Prints CONSTOBJC BAD lines and one verdict line "CONST-OBJC-VERDICT pass|FAIL n=<checked> bad=<n> zero=<n> [first bad]";
# exit 0 on pass. Only the darlingserver processes this run started are stopped (by pid).
SRC="${DARLING_SRC_DIR:-}"; BD="${DARLING_BUILD_DIR:-}"; OD="${DARLING_OVERLAY:-}"; WAIT="${1:-120}"
if [ -z "$SRC" ] || [ -z "$BD" ] || [ -z "$OD" ]; then
	echo "CONST-OBJC-VERDICT FAIL n=0 bad=1 zero=0 [missing env:$([ -z "$SRC" ] && printf ' DARLING_SRC_DIR')$([ -z "$BD" ] && printf ' DARLING_BUILD_DIR')$([ -z "$OD" ] && printf ' DARLING_OVERLAY')]"; exit 1
fi
export PATH="/usr/local/bin:/usr/local/sbin:/usr/bin:/bin:/sbin:/usr/sbin"
HERE="$(cd "$(dirname "$0")" && pwd)"
SDK_FLAT="$BD/bundle-principal-class-test/sdk-flat"; T=const-objc-probe-macho; W="$BD/const-objc"; mkdir -p "$W"
[ -d "$SDK_FLAT/usr/include" ] || { echo "CONST-OBJC-VERDICT FAIL n=0 bad=1 [no flattened SDK]"; exit 1; }
python3 "$HERE/gen-const-objc-table.py" "$OD" "$W/const-objc-table.h" || { echo "CONST-OBJC-VERDICT FAIL n=0 bad=1 [table]"; exit 1; }
clang -target x86_64-apple-macos10.12 -nostdinc -D__DARWIN_ONLY_UNIX_CONFORMANCE=1 -O1 -w -I"$W" \
	-I"$SRC/tests/vendor/fakesdk" -I"$SDK_FLAT/usr/include" -c "$SRC/tests/src/const-objc-probe.c" -o "$W/probe.o" &&
ld64.lld -arch x86_64 -platform_version macos 10.12 10.12 -syslibroot "$OD" -Z -o "$SRC/tests/$T" "$W/probe.o" \
	"$OD/System/Library/Frameworks/CoreFoundation.framework/Versions/A/CoreFoundation" "$OD/usr/lib/libobjc.A.dylib" \
	"$OD/usr/lib/libicucore.A.dylib" "$OD/usr/lib/libSystem.B.dylib" $(cat "$W/const-objc-table.h.libs") 2>/dev/null \
	|| { echo "CONST-OBJC-VERDICT FAIL n=0 bad=1 [build]"; exit 1; }
LOG="$BD/const-objc-check.log"
PRE=" $(pgrep -x darlingserver | tr '\n' ' ') "
( sudo timeout -k 5 "$WAIT" env "DARLING_SRC_DIR=$SRC" "DARLING_OVERLAY=$OD" "DARLING_BUILD_DIR=$BD" "DARLING_TEST_BINARY=$T" \
	DARLING_SMOKE_REFRESH=1 DARLING_STAGING_TREES=usr/lib:System/Library/Frameworks:System/Library/PrivateFrameworks:private/etc "$BD/launch-dynamic" > "$LOG" 2>&1 & )
i=0; while [ $i -lt "$WAIT" ]; do grep -qa "CONSTOBJC DONE\|FATAL signal" "$LOG" 2>/dev/null && break; sleep 1; i=$((i+1)); done
sleep 1
for p in $(pgrep -x darlingserver); do case "$PRE" in *" $p "*) ;; *) sudo kill -9 "$p" 2>/dev/null;; esac; done
grep -a "^CONSTOBJC \(BAD\|dlopen\)" "$LOG" | head -40
if grep -qa "^CONST-OBJC-VERDICT" "$LOG"; then
	grep -a "^CONST-OBJC-VERDICT" "$LOG" | tail -1
	grep -qa "^CONST-OBJC-VERDICT pass" "$LOG" && exit 0; exit 1
fi
last=$(grep -a "^CONSTOBJC chk" "$LOG" | tail -1 | cut -d' ' -f3,4)
echo "CONST-OBJC-VERDICT FAIL n=0 bad=1 zero=0 [guest died or timed out at: ${last:-start}]"; exit 1
