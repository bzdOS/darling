#!/bin/sh
# series-guard-test.sh - checks series_guard against a temporary lock file; Darling is not started.
#   Prints SERIES-GUARD-TEST pass|fail
HERE="$(cd "$(dirname "$0")" && pwd)"
. "${HERE}/series-guard.sh"
TMP="$(mktemp /tmp/series-guard.XXXXXX)" || { echo "SERIES-GUARD-TEST fail"; exit 1; }
SERIES_LOCK_FILE="$TMP"; export SERIES_LOCK_FILE
lockf "$TMP" sleep 30 &
LPID=$!
# wait (bounded, no foreground sleep) until the lockf command line is visible
i=0
while [ $i -lt 50 ] && ! pgrep -f "lockf.*${TMP}" >/dev/null 2>&1; do i=$((i+1)); timeout 0.1 tail -f /dev/null >/dev/null 2>&1; done
ok=1
DARLING_LOCK_HELD=1 series_guard 2>/dev/null; rc_a=$?
[ $rc_a -eq 0 ] || ok=0
( unset DARLING_LOCK_HELD; series_guard 2>/dev/null ); rc_b=$?
[ $rc_b -ne 0 ] || ok=0
echo "held=1 rc=${rc_a} (want 0); unset rc=${rc_b} (want nonzero)"
ps -o pid,ppid,lstart,args -p "$LPID"
ps -o pid,ppid,lstart,args -p "$(pgrep -P "$LPID" 2>/dev/null | head -1)" 2>/dev/null
SPID="$(pgrep -P "$LPID" 2>/dev/null | head -1)"
ls -ld "$TMP"
[ -n "$SPID" ] && kill "$SPID"
kill "$LPID" 2>/dev/null
wait "$LPID" 2>/dev/null
rm -f "$TMP"
[ $ok -eq 1 ] && echo "SERIES-GUARD-TEST pass" || { echo "SERIES-GUARD-TEST fail"; exit 1; }
