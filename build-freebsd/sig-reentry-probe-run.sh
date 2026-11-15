#!/bin/sh
# sig-reentry-probe-run.sh - run tests/sig-reentry-probe-macho until N valid runs, under launch-dynamic.
# usage: sig-reentry-probe-run.sh PREFIX N [TIMEOUT] [SUSPENDS] [SPAWNS] [IDLE_WORKER]
# Env: DARLING_SRC_DIR DARLING_OVERLAY DARLING_BUILD_DIR; PROBE_TRAP_LOG=1 turns the guest trap log on.
# PROBE_EXTRA_ENV="A=1 B=2" adds env to the guest run.
# Logs: $DARLING_BUILD_DIR/PREFIX-<i>.log, never overwritten. A run without GUEST-EXEC (never reached the
# guest, e.g. slow staging) is INVALID and not counted. One line per run, then a SUMMARY line.
P=${1:?}; N=${2:?}; T=${3:-90}; SU=${4:-3000}; SP=${5:-300}; IW=${6:-0}
: "${DARLING_SRC_DIR:?}" "${DARLING_OVERLAY:?}" "${DARLING_BUILD_DIR:?}"
PROBE_ENV=PROBE_RUN=1; [ -n "$PROBE_TRAP_LOG" ] && PROBE_ENV=DARLING_TRAP_LOG=1
KT=; [ -n "$PROBE_KTRACE" ] && KT="ktrace -i -t cs -f $DARLING_BUILD_DIR/$P-ktr.out"
ok=0; bad=0; valid=0; i=1
while [ $valid -lt "$N" ] && [ $i -le $((N*3)) ]; do
	LOG=$DARLING_BUILD_DIR/$P-$i.log
	[ -e "$LOG" ] && { echo "exists $LOG"; exit 1; }
	START=$(date +%s)
	/usr/local/bin/sudo -n env DARLING_SRC_DIR="$DARLING_SRC_DIR" DARLING_OVERLAY="$DARLING_OVERLAY" \
	  DARLING_BUILD_DIR="$DARLING_BUILD_DIR" DARLING_TEST_BINARY=sig-reentry-probe-macho DARLING_KEEP_LOCAL=1 \
	  DARLING_TEST_ARGS="$SU $SP $IW" $PROBE_ENV $PROBE_EXTRA_ENV \
	  $KT timeout --foreground -k 5 "$T" "$DARLING_BUILD_DIR/launch-dynamic" > "$LOG" 2>&1
	rc=$?
	d=$(( $(date +%s) - START ))
	if ! grep -aq 'GUEST-EXEC' "$LOG"; then
		echo "$P-$i rc=$rc ${d}s/${T}s INVALID (no GUEST-EXEC)"
	else
		valid=$((valid+1))
		if grep -aq '^PROBE-OK' "$LOG"; then v=OK; ok=$((ok+1)); else v=BAD; bad=$((bad+1)); fi
		sig=$(grep -aE 'PROBE-HANG|FATAL signal|genuine SIGILL|SIGILL RE-RAISE|SIGILL-FAULT info|unfair|BAD (SEND|RECEIVE)|abort_with_payload|interrupt_enter failed' "$LOG" | head -1 | cut -c1-170)
		echo "$P-$i rc=$rc ${d}s/${T}s $v [${sig:-none}]"
	fi
	i=$((i+1))
done
echo "SUMMARY $P: N=$N valid=$valid ok=$ok bad=$bad"
