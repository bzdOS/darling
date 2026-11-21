#!/bin/sh
# sig-reentry-probe-run.sh - run tests/sig-reentry-probe-macho until N valid runs, under launch-dynamic.
# usage: sig-reentry-probe-run.sh PREFIX N [TIMEOUT] [SUSPENDS] [SPAWNS] [IDLE_WORKER]  (TIMEOUT default: $PROBE_TIMEOUT or 240 s)
# Env: DARLING_SRC_DIR DARLING_OVERLAY DARLING_BUILD_DIR; PROBE_TRAP_LOG=1 turns the guest trap log on.
# PROBE_EXTRA_ENV="A=1 B=2" adds env to the guest run. PROBE_BINARY=<tests/ name> runs another probe binary.
# PROBE_SERVER_LOG=<level> (or CHROME_SR_SERVER_LOG) starts darlingserver with DSERVER_LOG_LEVEL=<level> and saves
# its log as PREFIX-<i>-dserver.txt (last 100 MB), to catch the server dropping its socket (-111).
# Logs: $DARLING_BUILD_DIR/PREFIX-<i>.log, never overwritten. A run without GUEST-EXEC (never reached the
# guest, e.g. slow staging) is INVALID and not counted. One line per run, then a SUMMARY line.
# exclusive run lock: one probe/Chrome run at a time; nested calls (DARLING_LOCK_HELD) do not re-lock
if [ -z "$DARLING_LOCK_HELD" ]; then
	: "${DARLING_BUILD_DIR:?}"
	DARLING_LOCK_HELD=1 lockf -t 0 "$DARLING_BUILD_DIR/.darling-run.lock" sh "$0" "$@"
	rc=$?
	[ $rc -eq 75 ] && { echo "LOCK busy"; exit 1; }
	exit $rc
fi
P=${1:?}; N=${2:?}; T=${3:-${PROBE_TIMEOUT:-240}}; SU=${4:-3000}; SP=${5:-300}; IW=${6:-0}
: "${DARLING_SRC_DIR:?}" "${DARLING_OVERLAY:?}" "${DARLING_BUILD_DIR:?}"
PB=${PROBE_BINARY:-sig-reentry-probe-macho}; SL=${PROBE_SERVER_LOG:-$CHROME_SR_SERVER_LOG}
PROBE_ENV=PROBE_RUN=1; [ -n "$PROBE_TRAP_LOG" ] && PROBE_ENV=DARLING_TRAP_LOG=1
KT=; [ -n "$PROBE_KTRACE" ] && KT="ktrace -i -t cs -f $DARLING_BUILD_DIR/$P-ktr.out"
. "$DARLING_SRC_DIR/build-freebsd/dserver-prefix.sh"
ok=0; bad=0; valid=0; i=1
while [ $valid -lt "$N" ] && [ $i -le $((N*3)) ]; do
	LOG=$DARLING_BUILD_DIR/$P-$i.log
	[ -e "$LOG" ] && { echo "exists $LOG"; exit 1; }
	dsp_new || exit 1
	START=$(date +%s)
	/usr/local/bin/sudo -n env DARLING_PREFIX="$DARLING_PREFIX" DARLING_SRC_DIR="$DARLING_SRC_DIR" DARLING_OVERLAY="$DARLING_OVERLAY" \
	  DARLING_BUILD_DIR="$DARLING_BUILD_DIR" DARLING_TEST_BINARY="$PB" DARLING_KEEP_LOCAL=1 \
	  DARLING_TEST_ARGS="$SU $SP $IW" $PROBE_ENV ${SL:+DSERVER_LOG_LEVEL=$SL} $PROBE_EXTRA_ENV \
	  $KT timeout -k 5 "$T" "$DARLING_BUILD_DIR/launch-dynamic" > "$LOG" 2>&1
	rc=$?
	[ -n "$SL" ] && /usr/local/bin/sudo -n tail -c 100000000 "$DARLING_PREFIX/private/var/log/dserver.log" > "$DARLING_BUILD_DIR/$P-$i-dserver.txt" 2>/dev/null
	dsp_reap
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
