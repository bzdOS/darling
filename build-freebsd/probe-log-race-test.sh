#!/bin/sh
# probe-log-race-test.sh - guards the probe launch/wait race (a wait loop matching the previous run's log).
# Usage: sh build-freebsd/probe-log-race-test.sh    Env: PROBE_LOG_RACE_NEGCTL=1 runs the negative control instead.
# Prints PROBE-LOG-RACE-TEST pass|fail, or (negctl) PROBE-LOG-RACE-TEST negctl pass|fail; exit 0 on pass.
D=$(cd "$(dirname "$0")" && pwd)
PROBES="bitmap-check.sh bitmap-draw-check.sh bootstrap-check.sh cfinit-check.sh const-objc-check.sh font-families-probe.sh font-metrics-probe.sh font-resolve-probe.sh kqueue-close-check.sh nscgcolor-check.sh port-construct-probe.sh sc-stubs-check.sh syscall482-probe.sh throw-trace-selftest.sh tz-check.sh"
T=$(mktemp -d) || { echo "PROBE-LOG-RACE-TEST fail"; exit 1; }
trap 'rm -rf "$T"' EXIT
# $1 = dir holding probe-launch.sh. An old log line matches PATTERN; the fake command writes it only after 2s.
functional() {
	(
	. "$1/probe-launch.sh" || exit 1
	L="$T/fake.log"; echo "old FAKE-DONE" > "$L"
	s=$(date +%s)
	probe_launch "$L" 10 "FAKE-DONE" sh -c 'sleep 2; echo "new FAKE-DONE"'
	rc=$?; e=$(date +%s)
	[ "$rc" = 0 ] && [ $((e - s)) -ge 2 ] && ! grep -qa "old FAKE-DONE" "$L" && grep -qa "new FAKE-DONE" "$L"
	)
}
if [ "${PROBE_LOG_RACE_NEGCTL:-}" = 1 ]; then
	sed '/^[[:space:]]*: > /d' "$D/probe-launch.sh" > "$T/probe-launch.sh"
	if grep -q '^[[:space:]]*: > ' "$T/probe-launch.sh"; then echo "PROBE-LOG-RACE-TEST negctl fail"; exit 1; fi
	if functional "$T"; then echo "PROBE-LOG-RACE-TEST negctl fail"; exit 1; fi
	echo "PROBE-LOG-RACE-TEST negctl pass"; exit 0
fi
bad=""
for p in $PROBES; do
	f="$D/$p"
	[ -f "$f" ] || { bad="$bad $p:missing"; continue; }
	grep -q 'probe-launch\.sh' "$f" || bad="$bad $p:no-source"
	grep -q '^probe_launch ' "$f" || bad="$bad $p:no-call"
	if grep -q '^( sudo' "$f" || grep -q '> "\$LOG" 2>&1 & )' "$f"; then bad="$bad $p:own-launch"; fi
done
functional "$D" || bad="$bad functional"
if [ -z "$bad" ]; then echo "PROBE-LOG-RACE-TEST pass"; exit 0; fi
echo "PROBE-LOG-RACE-TEST fail"; echo "bad:$bad"; exit 1
