#!/bin/sh
# keep-one-check.sh: probe runner logs/traces keep the newest per kind, older ones are probe_logs (never fail the verdict),
# and a probe trace does not push the newest series ktrace trace out of section 1c.
# The first two checks run with KEEPONE_RECENT_RUNS=1; the newest run takes the window slot, the middle one is kept as the newest of its kind, so only the oldest is a candidate; the default window (5 runs) protects all three.
D=$(dirname "$0")
R=$(mktemp -d /tmp/keepone-probe.XXXXXX) || exit 2
mkdir -p "$R/build" "$R/backup"; B=$R/build
echo x >"$B/fork-fail-probe-1120-100000.log"; touch -t 202001010000 "$B/fork-fail-probe-1120-100000.log"
echo x >"$B/fork-fail-probe-1121-100000.log"; touch -t 202001020000 "$B/fork-fail-probe-1121-100000.log"
echo x >"$B/fix37-ktrace.out"; touch -t 202001010000 "$B/fix37-ktrace.out"
echo x >"$B/fork-fail-probe-1121-100000-ktrace.out"; touch -t 202001030000 "$B/fork-fail-probe-1121-100000-ktrace.out"
echo x >"$B/fork-fail-probe-1122-100000.log"; touch -t 202001040000 "$B/fork-fail-probe-1122-100000.log"
out=$(KEEPONE_RECENT_RUNS=1 KEEPONE_ROOT="$R" sh "$D/keep-one-check.sh")
lst=$(KEEPONE_RECENT_RUNS=1 KEEPONE_ROOT="$R" sh "$D/keep-one-check.sh" --list)
v=pass
case "$out" in "KEEP-ONE verdict=pass"*"stale_logs=0 "*"probe_logs=1 "*) ;; *) v=fail; echo "$out";; esac
[ "$lst" = "$B/fork-fail-probe-1120-100000.log" ] || { v=fail; echo "list: $lst"; }
out2=$(KEEPONE_ROOT="$R" sh "$D/keep-one-check.sh")
case "$out2" in *"stale_logs=0 "*"probe_logs=0 "*) ;; *) v=fail; echo "recent: $out2";; esac
rm -f "$B"/*.log "$B"/*-ktrace.out; rmdir "$B" "$R/backup" "$R"
echo "KEEPONE-PROBE-TEST $v"
[ $v = pass ]
