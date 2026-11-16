#!/bin/sh
# kqueue-close-check.sh - inside Darling, close()/fclose() of an unrelated fd completes while threads are
# blocked in kevent64 on an EVFILT_MACHPORT (one of them inside the machport copyout, holding the kqueue lock).
# Builds tests/kqueue-close-probe-macho from tests/src/kqueue-close-probe.c and runs it under launch-dynamic.
# Usage: sh build-freebsd/kqueue-close-check.sh [wait-secs]    Env: DARLING_SRC_DIR DARLING_OVERLAY DARLING_BUILD_DIR
# Prints KQCHK lines and a final "KQCHK RESULT pass|fail"; exit 0 on pass, 1 otherwise.
# Only the darlingserver processes this run started are stopped (by pid); a pre-existing one is left alone.
SRC="${DARLING_SRC_DIR:?}"; BD="${DARLING_BUILD_DIR:?}"; OD="${DARLING_OVERLAY:?}"; WAIT="${1:-60}"
export PATH="/usr/local/bin:/usr/local/sbin:/usr/bin:/bin:/sbin:/usr/sbin"
SDK_FLAT="$BD/bundle-principal-class-test/sdk-flat"; T=kqueue-close-probe-macho; W="$BD/kqueue-close-probe"; mkdir -p "$W"
[ -d "$SDK_FLAT/usr/include" ] || { echo "KQCHK RESULT fail (no flattened SDK)"; exit 1; }
clang -target x86_64-apple-macos10.12 -nostdinc -D__DARWIN_ONLY_UNIX_CONFORMANCE=1 -O1 -w \
	-I"$SRC/tests/vendor/fakesdk" -I"$SDK_FLAT/usr/include" -c "$SRC/tests/src/kqueue-close-probe.c" -o "$W/probe.o" &&
ld64.lld -arch x86_64 -platform_version macos 10.12 10.12 -syslibroot "$OD" -Z -o "$SRC/tests/$T" "$W/probe.o" "$OD/usr/lib/libSystem.B.dylib" 2>/dev/null \
	|| { echo "KQCHK RESULT fail (build)"; exit 1; }
LOG="$BD/kqueue-close-check.log"
PRE=" $(pgrep -x darlingserver | tr '\n' ' ') "
( sudo timeout -k 5 120 env "DARLING_SRC_DIR=$SRC" "DARLING_OVERLAY=$OD" "DARLING_BUILD_DIR=$BD" "DARLING_TEST_BINARY=$T" \
	DARLING_SMOKE_REFRESH=1 "$BD/launch-dynamic" > "$LOG" 2>&1 & )
i=0; while [ $i -lt "$WAIT" ]; do grep -qa "KQCHK RESULT\|FATAL signal" "$LOG" 2>/dev/null && break; sleep 1; i=$((i+1)); done
sleep 1
for p in $(pgrep -x darlingserver); do case "$PRE" in *" $p "*) ;; *) sudo kill -9 "$p" 2>/dev/null;; esac; done
grep -a "^KQCHK" "$LOG"
if grep -qa "^KQCHK RESULT pass" "$LOG"; then exit 0; fi
grep -qa "^KQCHK RESULT" "$LOG" || echo "KQCHK RESULT fail (no result line, probe did not finish)"
exit 1
