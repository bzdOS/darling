#!/bin/sh
# throw-trace-selftest.sh - validate throw-trace-hook.sh: tests/src/throw-trace-selftest.mm (one caught ObjC @throw, one caught C++ throw)
# runs in the guest with the hook loaded the way a Chrome run loads it (DYLD_INSERT_LIBRARIES + DARLING_THROW_TRACE=1, dylib copied
# with DARLING_GUEST_COPY). Prints: THROW-TRACE-SELFTEST pass|fail objc=0/1 cxx=0/1; exit 0 on pass.
# Usage: sh build-freebsd/throw-trace-selftest.sh [wait-secs]   Env: DARLING_SRC_DIR DARLING_OVERLAY DARLING_BUILD_DIR
# Respects .darling-run.lock. Only own darlingserver pids are stopped.
SRC="${DARLING_SRC_DIR:?}"; BD="${DARLING_BUILD_DIR:?}"; OD="${DARLING_OVERLAY:?}"; WAIT="${1:-150}"
export PATH="/usr/local/bin:/usr/local/sbin:/usr/bin:/bin:/sbin:/usr/sbin"
if [ -z "$DARLING_LOCK_HELD" ]; then exec lockf -t 0 "$BD/.darling-run.lock" env DARLING_LOCK_HELD=1 sh "$0" "$@"; fi
F0="THROW-TRACE-SELFTEST fail objc=0 cxx=0"
SDK_FLAT="$BD/bundle-principal-class-test/sdk-flat"; T=throw-trace-selftest-macho; W="$BD/throw-trace-selftest"; mkdir -p "$W"
HOOK=$(sh "$SRC/build-freebsd/throw-trace-hook.sh") || { echo "$F0 bad=[hook build]"; exit 1; }
clang++ -x objective-c++ -target x86_64-apple-macos10.12 -nostdinc -nostdinc++ -D__DARWIN_ONLY_UNIX_CONFORMANCE=1 -O0 -w -fobjc-exceptions -fexceptions \
	-I"$SRC/tests/vendor/fakesdk" -I"$SDK_FLAT/usr/include" -c "$SRC/tests/src/throw-trace-selftest.mm" -o "$W/t.o" 2>"$W/cc.err" &&
ld64.lld -arch x86_64 -platform_version macos 10.12 10.12 -syslibroot "$OD" -Z -o "$SRC/tests/$T" "$W/t.o" \
	"$OD/usr/lib/libc++.1.dylib" "$OD/usr/lib/libc++abi.dylib" "$OD/usr/lib/libobjc.A.dylib" "$OD/usr/lib/libSystem.B.dylib" 2>"$W/link.err" \
	|| { echo "$F0 bad=[build: $(tail -1 "$W/cc.err" "$W/link.err" | tr "\n" " " | cut -c1-200)]"; exit 1; }
LOG="$BD/throw-trace-selftest.log"
PRE=" $(pgrep -x darlingserver | tr "\n" " ") "
( sudo timeout -k 5 "$WAIT" env "DARLING_SRC_DIR=$SRC" "DARLING_OVERLAY=$OD" "DARLING_BUILD_DIR=$BD" "DARLING_TEST_BINARY=$T" DARLING_SMOKE_REFRESH=1 \
	DARLING_GUEST_MKDIR=/throwtrace "DARLING_GUEST_COPY=$HOOK=/throwtrace/throw-trace.dylib" \
	DYLD_INSERT_LIBRARIES=/throwtrace/throw-trace.dylib DARLING_THROW_TRACE=1 "$BD/launch-dynamic" > "$LOG" 2>&1 & )
i=0; while [ $i -lt "$WAIT" ]; do grep -qa "SELFTEST-DONE\|FATAL signal" "$LOG" 2>/dev/null && break; sleep 1; i=$((i+1)); done
sleep 1
for p in $(pgrep -x darlingserver); do case "$PRE" in *" $p "*) ;; *) sudo kill -9 "$p" 2>/dev/null;; esac; done
o=0; c=0
grep -aq "^THROW-TRACE objc " "$LOG" && o=1
grep -aq "^THROW-TRACE cxx " "$LOG" && c=1
grep -aq "^SELFTEST-DONE" "$LOG" || { echo "THROW-TRACE-SELFTEST fail objc=$o cxx=$c bad=[no-done] log=$LOG"; exit 1; }
[ "$o$c" = 11 ] && { echo "THROW-TRACE-SELFTEST pass objc=$o cxx=$c"; exit 0; }
echo "THROW-TRACE-SELFTEST fail objc=$o cxx=$c log=$LOG"; exit 1
