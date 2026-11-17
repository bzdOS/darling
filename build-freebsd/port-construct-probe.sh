#!/bin/sh
# port-construct-probe.sh - mach_port_construct (RPC to darlingserver) must always get a reply: tests/src/port-construct-probe.c
# (MPO_* flag combos, explicit name + collision, bad args, 24 concurrent threads with S2C pressure and a lock held across the call)
# is built into tests/port-construct-probe-macho and run; a watchdog counts workers that never finish.
# Usage: sh build-freebsd/port-construct-probe.sh [wait-secs]    Env: DARLING_SRC_DIR DARLING_OVERLAY DARLING_BUILD_DIR
# One verdict line: PORT-CONSTRUCT-VERDICT pass|fail n=<checks> hung=<workers> bad=[names]; exit 0 on pass. Only own darlingserver pids are stopped.
SRC="${DARLING_SRC_DIR:?}"; BD="${DARLING_BUILD_DIR:?}"; OD="${DARLING_OVERLAY:?}"; WAIT="${1:-90}"
export PATH="/usr/local/bin:/usr/local/sbin:/usr/bin:/bin:/sbin:/usr/sbin"
SDK_FLAT="$BD/bundle-principal-class-test/sdk-flat"; T=port-construct-probe-macho; W="$BD/port-construct-probe"; mkdir -p "$W"
F0="PORT-CONSTRUCT-VERDICT fail n=0 hung=0"
[ -d "$SDK_FLAT/usr/include" ] || { echo "$F0 bad=[no flattened SDK]"; exit 1; }
clang -target x86_64-apple-macos10.12 -nostdinc -D__DARWIN_ONLY_UNIX_CONFORMANCE=1 -O1 -w \
	-I"$SRC/tests/vendor/fakesdk" -I"$SDK_FLAT/usr/include" -c "$SRC/tests/src/port-construct-probe.c" -o "$W/probe.o" 2>"$W/cc.err" &&
ld64.lld -arch x86_64 -platform_version macos 10.12 10.12 -syslibroot "$OD" -Z -o "$SRC/tests/$T" "$W/probe.o" "$OD/usr/lib/libSystem.B.dylib" 2>"$W/link.err" \
	|| { echo "$F0 bad=[build: $(tail -1 "$W/cc.err" "$W/link.err" | tr '\n' ' ')]"; exit 1; }
LOG="$BD/port-construct-probe.log"
PRE=" $(pgrep -x darlingserver | tr '\n' ' ') "
( sudo timeout -k 5 "$WAIT" env "DARLING_SRC_DIR=$SRC" "DARLING_OVERLAY=$OD" "DARLING_BUILD_DIR=$BD" "DARLING_TEST_BINARY=$T" \
	DARLING_SMOKE_REFRESH=1 "$BD/launch-dynamic" > "$LOG" 2>&1 & )
i=0; while [ $i -lt "$WAIT" ]; do grep -qa "PC-SUMMARY\|FATAL signal" "$LOG" 2>/dev/null && break; sleep 1; i=$((i+1)); done
sleep 1
for p in $(pgrep -x darlingserver); do case "$PRE" in *" $p "*) ;; *) sudo kill -9 "$p" 2>/dev/null;; esac; done
L=$(grep -a "^PC-SUMMARY " "$LOG" | tail -1)
if [ -z "$L" ]; then echo "PORT-CONSTRUCT-VERDICT fail n=0 hung=all bad=[no-summary: probe stalled or died] log=$LOG"; exit 1; fi
c=${L#*n=}; c=${c%% *}; h=${L#*hung=}; h=${h%% *}; b=${L#*bad=}; b=${b%% *}
if [ "$b" = "-" ] && [ "$h" = "0" ]; then echo "PORT-CONSTRUCT-VERDICT pass n=$c hung=$h bad=[]"; exit 0; fi
echo "PORT-CONSTRUCT-VERDICT fail n=$c hung=$h bad=[$b]"; exit 1
