#!/bin/sh
# guest-segv-catch-selftest.sh - no Chrome: a test program faults on purpose; checks guest-segv-catch.sh reports the
# fault address, the faulting thread's rip and a frame chain through selftest-image. Prints SEGV-SELFTEST pass|fail.
D=$(cd "$(dirname "$0")" && pwd)
W=$(mktemp -d "${TMPDIR:-/tmp}/segv-selftest.XXXXXX") || exit 1
trap 'rm -rf "$W"' EXIT
"${CC:-clang}" -std=gnu11 -O0 -fno-omit-frame-pointer -o "$W/crasher" "$D/guest-segv-catch-selftest.c" || { echo "SEGV-SELFTEST fail (build)"; exit 1; }
( ulimit -c 0; exec "$W/crasher" "$W/log" "$W/ready" ) & CP=$!
i=0; while [ ! -e "$W/ready" ] && [ $i -lt 50 ]; do sleep 0.1; i=$((i+1)); done
sh "$D/guest-segv-catch.sh" "$CP" "$W/log" > "$W/out" 2>&1
wait $CP 2>/dev/null
V=$(grep -a '^SEGV-VERDICT' "$W/out")
cat "$W/out" | tail -12
ok=1; why=""
chk() { eval "$1" || { ok=0; why="$why [$2]"; }; }
chk 'echo "$V" | grep -q "caught=1 "' caught
chk 'echo "$V" | grep -q "sig=11 "' sig11
chk 'echo "$V" | grep -q "addr=0x10 "' addr
chk 'echo "$V" | grep -q "rip=selftest-image+0x"' rip-module
chk '[ "$(echo "$V" | grep -o "selftest-image+0x" | wc -l)" -ge 3 ]' frames-ge3
if [ $ok = 1 ]; then echo "SEGV-SELFTEST pass: $V" | cut -c1-400; exit 0; fi
echo "SEGV-SELFTEST fail:$why $V"; exit 1
