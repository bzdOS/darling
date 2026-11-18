#!/bin/sh
# chrome-series-pidwait-test.sh - fake-command test of chrome-series.sh series_pidwait and its SERIES-EXIT line (no Chrome, no darling).
# usage: sh chrome-series-pidwait-test.sh        -> CHROME-SERIES-PIDWAIT-TEST pass|fail
#        CHROME_SERIES_PIDWAIT_NEGCTL=1 sh ...   -> runs the same checks against a copy of the script with the pid wait
#                                                    disabled; prints CHROME-SERIES-PIDWAIT-TEST negctl pass only if a check fails.
S=$(cd "$(dirname "$0")" && pwd)/chrome-series.sh
T=$(mktemp -d "${TMPDIR:-/tmp}/pidwait-test.XXXXXX") || exit 1
trap 'rm -rf "$T"' EXIT
if [ "$CHROME_SERIES_PIDWAIT_NEGCTL" = 1 ]; then
	sed 's/^\([[:space:]]*\)while kill -0 .*; do$/\1while true; do/' "$S" > "$T/chrome-series.sh"
	cmp -s "$S" "$T/chrome-series.sh" && { echo "CHROME-SERIES-PIDWAIT-TEST negctl fail (sed changed nothing)"; exit 1; }
	S=$T/chrome-series.sh
fi
sed -n '/^# BEGIN series_pidwait/,/^# END series_pidwait/p' "$S" > "$T/pidwait.sh"
sed -n '/^leftover_report() {/,/^}/p' "$S" > "$T/leftover.sh"
. "$T/pidwait.sh"
ok=1; why=""
chk() { eval "$1" || { ok=0; why="$why [$2]"; }; }
# check 1: a fake run that exits early ends the wait at once (max 10 s, must return in <= 4 s)
sleep 1 & p=$!
t0=$(date +%s); series_pidwait "$p" 10; r1=$?; t1=$(date +%s)
chk '[ "$r1" -eq 0 ] && [ $((t1-t0)) -le 4 ]' early-exit
# check 2: a fake run that hangs is cut off at the timeout (3 s)
sleep 60 & p=$!
t0=$(date +%s); series_pidwait "$p" 3; r2=$?; t1=$(date +%s)
chk '[ "$r2" -eq 124 ] && [ $((t1-t0)) -ge 3 ] && [ $((t1-t0)) -le 8 ]' timeout-cutoff
kill "$p" 2>/dev/null; wait "$p" 2>/dev/null
# check 3: the exit trap prints SERIES-EXIT rc=<n> pid=<series pid> as the last line (fake ps: no leftover servers)
out=$( ( ps() { :; }; . "$T/leftover.sh"; (exit 3); leftover_report ) 2>&1 )
chk '[ "$(printf "%s\n" "$out" | tail -1)" = "SERIES-EXIT rc=3 pid=$$" ]' series-exit-line
# the series itself starts the batch in the background and waits by pid
chk 'grep -q "^BP=\$!" "$S" && grep -q "series_pidwait \"\$BP\"" "$S"' batch-waited-by-pid
if [ "$CHROME_SERIES_PIDWAIT_NEGCTL" = 1 ]; then
	if [ $ok -eq 0 ]; then echo "CHROME-SERIES-PIDWAIT-TEST negctl pass (checks failed with the pid wait disabled:$why)"; exit 0; fi
	echo "CHROME-SERIES-PIDWAIT-TEST negctl fail (all checks passed with the pid wait disabled)"; exit 1
fi
if [ $ok -eq 1 ]; then echo "CHROME-SERIES-PIDWAIT-TEST pass (early exit r=$r1, hang cut off r=$r2, SERIES-EXIT printed)"; exit 0; fi
echo "CHROME-SERIES-PIDWAIT-TEST fail:$why"; exit 1
