#!/bin/sh
# syscall482-probe.sh - BSD syscall 482 (thread_selfusage) is implemented inside Darling: tests/src/syscall482-probe.c is built into
# tests/syscall482-probe-macho and run; the log must hold no "Unimplemented syscall (482)".
# Usage: sh build-freebsd/syscall482-probe.sh [wait-secs]    Env: DARLING_SRC_DIR DARLING_OVERLAY DARLING_BUILD_DIR
# One verdict line: SYSCALL482-VERDICT pass|fail n=<checks> bad=[names]; exit 0 on pass. Only own darlingserver pids are stopped.
SRC="${DARLING_SRC_DIR:?}"; BD="${DARLING_BUILD_DIR:?}"; OD="${DARLING_OVERLAY:?}"; WAIT="${1:-60}"
export PATH="/usr/local/bin:/usr/local/sbin:/usr/bin:/bin:/sbin:/usr/sbin"
SDK_FLAT="$BD/bundle-principal-class-test/sdk-flat"; T=syscall482-probe-macho; W="$BD/syscall482-probe"; mkdir -p "$W"
F0="SYSCALL482-VERDICT fail n=0"
[ -d "$SDK_FLAT/usr/include" ] || { echo "$F0 bad=[no flattened SDK]"; exit 1; }
clang -target x86_64-apple-macos10.12 -nostdinc -D__DARWIN_ONLY_UNIX_CONFORMANCE=1 -O1 -w \
	-I"$SRC/tests/vendor/fakesdk" -I"$SDK_FLAT/usr/include" -c "$SRC/tests/src/syscall482-probe.c" -o "$W/probe.o" &&
ld64.lld -arch x86_64 -platform_version macos 10.12 10.12 -syslibroot "$OD" -Z -o "$SRC/tests/$T" "$W/probe.o" "$OD/usr/lib/libSystem.B.dylib" 2>"$W/link.err" \
	|| { echo "$F0 bad=[build: $(tail -1 "$W/link.err")]"; exit 1; }
LOG="$BD/syscall482-probe.log"
PRE=" $(pgrep -x darlingserver | tr n  ) "
. "$(dirname "$0")/probe-launch.sh"
probe_launch "$LOG" "$WAIT" "S482-SUMMARY\|FATAL signal" sudo timeout -k 5 "$WAIT" env "DARLING_SRC_DIR=$SRC" "DARLING_OVERLAY=$OD" "DARLING_BUILD_DIR=$BD" "DARLING_TEST_BINARY=$T" \
	$([ "${STAGE_GATE_KEEP_STAGED:-}" = 1 ] && echo DARLING_KEEP_LOCAL=1 || echo DARLING_SMOKE_REFRESH=1) "$BD/launch-dynamic"
sleep 1
for p in $(pgrep -x darlingserver); do case "$PRE" in *" $p "*) ;; *) sudo kill -9 "$p" 2>/dev/null;; esac; done
L=$(grep -a "^S482-SUMMARY " "$LOG" | tail -1)
[ -n "$L" ] || { echo "SYSCALL482-VERDICT fail n=0 bad=[no-summary] log=$LOG"; exit 1; }
c=${L#*n=}; c=${c%% *}; b=${L#*bad=}; b=${b%% *}
grep -aq "Unimplemented syscall (482)" "$LOG" && { echo "SYSCALL482-VERDICT fail n=$c bad=[unimplemented-482]"; exit 1; }
[ "$b" = "-" ] && { echo "SYSCALL482-VERDICT pass n=$c bad=[]"; exit 0; }
echo "SYSCALL482-VERDICT fail n=$c bad=[$b]"; exit 1
