#!/bin/sh
# Self-test for wait-pid.sh. Prints WAIT-PID-TEST pass|fail.
D=$(cd "$(dirname "$0")" && pwd)
W="$D/wait-pid.sh"
fail=0
chk() { # name expected actual
	if [ "$2" = "$3" ]; then echo "ok   $1 rc=$3"; else echo "FAIL $1 want=$2 got=$3"; fail=1; fi
}

sleep 1 &
sh "$W" $! 10; chk a-exits 0 $?

sleep 60 &
p=$!
sh "$W" "$p" 2; chk b-timeout 1 $?
ps -o pid,ppid,lstart,args -p "$p"
kill "$p"
wait "$p" 2>/dev/null

# c: script whose own command line contains "wait-pid"
sh -c 'sleep 2; : wait-pid' &
sh "$W" $! 10; chk c-selfpattern 0 $?

sh "$W" >/dev/null 2>&1; chk d-noargs 2 $?
sh "$W" abc 5 >/dev/null 2>&1; chk d-badpid 2 $?
sh "$W" 1 x >/dev/null 2>&1; chk d-badtmo 2 $?

if [ "$fail" = 0 ]; then echo "WAIT-PID-TEST pass"; else echo "WAIT-PID-TEST fail"; fi
[ "$fail" = 0 ]
