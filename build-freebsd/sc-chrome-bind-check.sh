#!/bin/sh
# sc-chrome-bind-check.sh - the SystemConfiguration dynamic store and IOPowerSources symbols Chrome binds from IOKitExtras.dylib
# (lib ordinal of the Chrome framework imports) are linked two-level against that image, as Chrome does, and must return
# non-NULL objects (store, notification source, empty power source snapshot, list, description).
# Builds tests/sc-chrome-bind-probe-macho from tests/src/sc-chrome-bind-probe.c and runs it under launch-dynamic.
# Usage: sh build-freebsd/sc-chrome-bind-check.sh [wait-secs]    Env: DARLING_SRC_DIR DARLING_OVERLAY DARLING_BUILD_DIR
# Prints SCBIND lines and a final "SCBIND RESULT pass|FAIL"; exit 0 on pass, 1 otherwise.
# Only the darlingserver processes this run started are stopped (by pid); a pre-existing one is left alone.
SRC="${DARLING_SRC_DIR:?}"; BD="${DARLING_BUILD_DIR:?}"; OD="${DARLING_OVERLAY:?}"; WAIT="${1:-60}"
export PATH="/usr/local/bin:/usr/local/sbin:/usr/bin:/bin:/sbin:/usr/sbin"
SDK_FLAT="$BD/bundle-principal-class-test/sdk-flat"; T=sc-chrome-bind-probe-macho; W="$BD/sc-stubs"; mkdir -p "$W"
[ -d "$SDK_FLAT/usr/include" ] || { echo "SCBIND RESULT FAIL (no flattened SDK)"; exit 1; }
clang -target x86_64-apple-macos10.12 -nostdinc -D__DARWIN_ONLY_UNIX_CONFORMANCE=1 -O1 -w \
	-I"$SRC/tests/vendor/fakesdk" -I"$SDK_FLAT/usr/include" -c "$SRC/tests/src/sc-chrome-bind-probe.c" -o "$W/probe.o" &&
ld64.lld -arch x86_64 -platform_version macos 10.12 10.12 -syslibroot "$OD" -Z -o "$SRC/tests/$T" "$W/probe.o" "$OD/System/Library/Frameworks/CoreFoundation.framework/Versions/A/CoreFoundation" "$OD/usr/lib/IOKitExtras.dylib" "$OD/usr/lib/libobjc.A.dylib" "$OD/usr/lib/libicucore.A.dylib" "$OD/usr/lib/libSystem.B.dylib" 2>/dev/null \
	|| { echo "SCBIND RESULT FAIL (build)"; exit 1; }
LOG="$BD/sc-chrome-bind-check.log"
PRE=" $(pgrep -x darlingserver | tr '\n' ' ') "
. "$(dirname "$0")/probe-launch.sh"
probe_launch "$LOG" "$WAIT" "SCBIND DONE\|FATAL signal" sudo timeout -k 5 120 env "DARLING_SRC_DIR=$SRC" "DARLING_OVERLAY=$OD" "DARLING_BUILD_DIR=$BD" "DARLING_TEST_BINARY=$T" \
	DARLING_SMOKE_REFRESH=1 "$BD/launch-dynamic"
sleep 1
for p in $(pgrep -x darlingserver); do case "$PRE" in *" $p "*) ;; *) sudo kill -9 "$p" 2>/dev/null;; esac; done
grep -a "^SCBIND" "$LOG"
if grep -qa "^SCBIND DONE fail=0" "$LOG" && ! grep -qa "^SCBIND .* NULL" "$LOG"; then echo "SCBIND RESULT pass"; exit 0; fi
echo "SCBIND RESULT FAIL"; exit 1
