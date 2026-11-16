#!/bin/sh
# sc-stubs-check.sh - inside Darling, every SystemConfiguration symbol Chrome imports at startup resolves through
# suppl.dylib and returns non-NULL (proxies dictionary, host names, key strings, reachability object, constants).
# Builds tests/sc-stubs-probe-macho from tests/src/sc-stubs-probe.c and runs it under launch-dynamic.
# Usage: sh build-freebsd/sc-stubs-check.sh [wait-secs]    Env: DARLING_SRC_DIR DARLING_OVERLAY DARLING_BUILD_DIR
# Prints SCCHK lines and a final "SCCHK RESULT pass|FAIL"; exit 0 on pass, 1 otherwise.
# Only the darlingserver processes this run started are stopped (by pid); a pre-existing one is left alone.
SRC="${DARLING_SRC_DIR:?}"; BD="${DARLING_BUILD_DIR:?}"; OD="${DARLING_OVERLAY:?}"; WAIT="${1:-60}"
export PATH="/usr/local/bin:/usr/local/sbin:/usr/bin:/bin:/sbin:/usr/sbin"
SDK_FLAT="$BD/bundle-principal-class-test/sdk-flat"; T=sc-stubs-probe-macho; W="$BD/sc-stubs"; mkdir -p "$W"
[ -d "$SDK_FLAT/usr/include" ] || { echo "SCCHK RESULT FAIL (no flattened SDK)"; exit 1; }
clang -target x86_64-apple-macos10.12 -nostdinc -D__DARWIN_ONLY_UNIX_CONFORMANCE=1 -O1 -w \
	-I"$SRC/tests/vendor/fakesdk" -I"$SDK_FLAT/usr/include" -c "$SRC/tests/src/sc-stubs-probe.c" -o "$W/probe.o" &&
ld64.lld -arch x86_64 -platform_version macos 10.12 10.12 -syslibroot "$OD" -Z -o "$SRC/tests/$T" "$W/probe.o" "$OD/System/Library/Frameworks/CoreFoundation.framework/Versions/A/CoreFoundation" "$OD/usr/lib/libobjc.A.dylib" "$OD/usr/lib/libicucore.A.dylib" "$OD/usr/lib/libSystem.B.dylib" 2>/dev/null \
	|| { echo "SCCHK RESULT FAIL (build)"; exit 1; }
LOG="$BD/sc-stubs-check.log"
PRE=" $(pgrep -x darlingserver | tr '\n' ' ') "
( sudo timeout -k 5 120 env "DARLING_SRC_DIR=$SRC" "DARLING_OVERLAY=$OD" "DARLING_BUILD_DIR=$BD" "DARLING_TEST_BINARY=$T" \
	DARLING_SMOKE_REFRESH=1 "$BD/launch-dynamic" > "$LOG" 2>&1 & )
i=0; while [ $i -lt "$WAIT" ]; do grep -qa "SCCHK DONE\|FATAL signal" "$LOG" 2>/dev/null && break; sleep 1; i=$((i+1)); done
sleep 1
for p in $(pgrep -x darlingserver); do case "$PRE" in *" $p "*) ;; *) sudo kill -9 "$p" 2>/dev/null;; esac; done
grep -a "^SCCHK" "$LOG"
if grep -qa "^SCCHK DONE fail=0" "$LOG" && ! grep -qa "^SCCHK .* NULL" "$LOG"; then echo "SCCHK RESULT pass"; exit 0; fi
echo "SCCHK RESULT FAIL"; exit 1
