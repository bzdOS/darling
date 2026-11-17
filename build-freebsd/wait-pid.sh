#!/bin/sh
# wait-pid.sh <pid> <timeout_s>
# Wait until process <pid> exits. Does not use pgrep (no self-matching).
# rc 0: process gone, rc 1: timeout, rc 2: bad arguments.
case "$#" in 2) ;; *) echo "usage: wait-pid.sh <pid> <timeout_s>" >&2; exit 2;; esac
pid=$1
tmo=$2
case "$pid" in ''|*[!0-9]*) echo "wait-pid: bad pid: $pid" >&2; exit 2;; esac
case "$tmo" in ''|*[!0-9]*) echo "wait-pid: bad timeout: $tmo" >&2; exit 2;; esac
[ "$pid" -gt 0 ] || { echo "wait-pid: bad pid: $pid" >&2; exit 2; }
i=0
while :; do
	# kill -0 fails with EPERM for foreign-owned live pids; ps -p covers that
	kill -0 "$pid" 2>/dev/null || ps -p "$pid" >/dev/null 2>&1 || exit 0
	[ "$i" -ge "$tmo" ] && exit 1
	sleep 1
	i=$((i + 1))
done
