#!/bin/sh
# series-count-selftest.sh - valid must never exceed N: prefix collisions and the batch summary log
# are not runs. Verdict: SERIES-COUNT-SELFTEST pass|FAIL ...
HERE=$(cd "$(dirname "$0")" && pwd)
. "$HERE/series-count.sh"
T=$(mktemp -d); trap 'rm -rf "$T"' EXIT
for f in fix3 fix31 fix32 fix33 fix34 fix35 fix36 fix37 fix38 fix39 fix310; do
	printf 'GUEST-EXEC\nWRAPPER_RC=139\nWRAPPER_DURATION=5\n' > "$T/chrome-sr-$f.log"
done
old=0; for L in "$T"/chrome-sr-fix3*.log; do grep -aq GUEST-EXEC "$L" && old=$((old+1)); done
series_count "$T" fix3 10
if [ "$valid" -eq 10 ] && [ "$old" -eq 11 ]; then
	echo "SERIES-COUNT-SELFTEST pass N=10 valid=$valid (old glob counted $old)"
else
	echo "SERIES-COUNT-SELFTEST FAIL N=10 valid=$valid old=$old"; exit 1
fi
