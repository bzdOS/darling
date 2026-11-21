#!/bin/sh
# sig-reentry-probe-run.sh - run tests/sig-reentry-probe-macho until N valid runs, under launch-dynamic.
# usage: sig-reentry-probe-run.sh PREFIX N [TIMEOUT] [SUSPENDS] [SPAWNS] [IDLE_WORKER]  (TIMEOUT default: $PROBE_TIMEOUT or 240 s)
# Env: DARLING_SRC_DIR DARLING_OVERLAY DARLING_BUILD_DIR; PROBE_TRAP_LOG=1 turns the guest trap log on.
# PROBE_EXTRA_ENV="A=1 B=2" adds env to the guest run. PROBE_BINARY=<tests/ name> runs another probe binary.
# PROBE_SERVER_LOG=<level> (or CHROME_SR_SERVER_LOG) starts darlingserver with DSERVER_LOG_LEVEL=<level> and saves
# its log as PREFIX-<MMDD-HHMMSS>-dserver.txt (last 100 MB) for a failed run only, to catch the server dropping its socket (-111).
# Logs: $DARLING_BUILD_DIR/PREFIX-<MMDD-HHMMSS>.log (the <kind>-MMDD-HHMMSS form keep-one-check.sh counts as probe logs), never overwritten. A run without GUEST-EXEC (never reached the
# guest, e.g. slow staging) is INVALID and not counted. One line per run, then a SUMMARY line.
# A timed-out run (rc 124/137) is counted as one of two different events (the "progress=" value is not a
# liveness signal; it stays 0 in passing runs): stall_progress0 when the guest wrote nothing (no new
# "suspend round" line) for the last PROBE_STALL_S seconds (default 60; a 500-round step takes ~15 s), i.e. a
# real stop in suspend; otherwise timeout_hits, a live but slow run that reached the threshold.
# TIMEOUT default: PROBE_TIMEOUT from the env, else from darling.env ([env] in darling.toml), else 240.
# exclusive run lock: one probe/Chrome run at a time; nested calls (DARLING_LOCK_HELD) do not re-lock
if [ -z "$DARLING_LOCK_HELD" ]; then
	: "${DARLING_BUILD_DIR:?}"
	DARLING_LOCK_HELD=1 lockf -t 0 "$DARLING_BUILD_DIR/.darling-run.lock" sh "$0" "$@"
	rc=$?
	[ $rc -eq 75 ] && { echo "LOCK busy"; exit 1; }
	exit $rc
fi
if [ -z "$PROBE_TIMEOUT" ] && [ -f "${DARLING_ENV_FILE:-$DARLING_BUILD_DIR/darling.env}" ]; then
	PROBE_TIMEOUT=$(sed -n 's/^PROBE_TIMEOUT=//p' "${DARLING_ENV_FILE:-$DARLING_BUILD_DIR/darling.env}")
fi
P=${1:?}; N=${2:?}; T=${3:-${PROBE_TIMEOUT:-240}}; SU=${4:-3000}; SP=${5:-300}; IW=${6:-0}
: "${DARLING_SRC_DIR:?}" "${DARLING_OVERLAY:?}" "${DARLING_BUILD_DIR:?}"
PB=${PROBE_BINARY:-sig-reentry-probe-macho}; SL=${PROBE_SERVER_LOG:-$CHROME_SR_SERVER_LOG}
PROBE_ENV=PROBE_RUN=1; [ -n "$PROBE_TRAP_LOG" ] && PROBE_ENV=DARLING_TRAP_LOG=1
KT=; [ -n "$PROBE_KTRACE" ] && KT="ktrace -i -t cs -f $DARLING_BUILD_DIR/$P-ktr.out"
. "$DARLING_SRC_DIR/build-freebsd/dserver-prefix.sh"
ok=0; bad=0; valid=0; i=1; th=0; st=0; STALL=${PROBE_STALL_S:-60}
while [ $valid -lt "$N" ] && [ $i -le $((N*3)) ]; do
	STAMP=$(date +%m%d-%H%M%S); LOG=$DARLING_BUILD_DIR/$P-$STAMP.log
	[ -e "$LOG" ] && { echo "exists $LOG"; exit 1; }
	dsp_new || exit 1
	START=$(date +%s)
	/usr/local/bin/sudo -n env DARLING_PREFIX="$DARLING_PREFIX" DARLING_SRC_DIR="$DARLING_SRC_DIR" DARLING_OVERLAY="$DARLING_OVERLAY" \
	  DARLING_BUILD_DIR="$DARLING_BUILD_DIR" DARLING_TEST_BINARY="$PB" DARLING_KEEP_LOCAL=1 \
	  DARLING_TEST_ARGS="$SU $SP $IW" $PROBE_ENV ${SL:+DSERVER_LOG_LEVEL=$SL} ${DSERVER_SOCKET_RCVBUF:+DSERVER_SOCKET_RCVBUF=$DSERVER_SOCKET_RCVBUF} $PROBE_EXTRA_ENV \
	  $KT timeout -k 5 "$T" "$DARLING_BUILD_DIR/launch-dynamic" > "$LOG" 2>&1
	rc=$?
	END=$(date +%s); LM=$(stat -f %m "$LOG" 2>/dev/null || echo "$END")
	# server log only for a failed run (rc != 0): a passing run's log is never written, so it costs no disk.
	# PROBE_SERVER_LOG_HEAD=1: also the first 100 MB (start of a suspend/resume loop the tail has lost)
	if [ -n "$SL" ] && [ "$rc" != 0 ]; then
		/usr/local/bin/sudo -n tail -c 100000000 "$DARLING_PREFIX/private/var/log/dserver.log" > "$DARLING_BUILD_DIR/$P-$STAMP-dserver.txt" 2>/dev/null
		[ -n "$PROBE_SERVER_LOG_HEAD" ] && /usr/local/bin/sudo -n head -c 100000000 "$DARLING_PREFIX/private/var/log/dserver.log" > "$DARLING_BUILD_DIR/$P-$STAMP-dserver-head.txt" 2>/dev/null
	fi
	# PROBE-HANG or timeout: one HANG-CLASS line per thread from the diag ring (needs DSERVER_DIAG_RING in PROBE_EXTRA_ENV):
	# saved_reply=yes is livelock kind A, saved_reply=no kind B. Appended to the run log, before the prefix is reaped.
	RING="$DARLING_PREFIX/private/var/log/dserver-diag-ring.bin"
	# Every run with a ring: keep it as PREFIX-<stamp>-ring.bin and append a RING-COUNTS line (k8/k9 > 0: the ring is written).
	if /usr/local/bin/sudo -n test -f "$RING"; then
		/usr/local/bin/sudo -n cat "$RING" > "$DARLING_BUILD_DIR/$P-$STAMP-ring.bin" 2>/dev/null
		python3 "$DARLING_SRC_DIR/build-freebsd/diag-ring-decode.py" --counts "$DARLING_BUILD_DIR/$P-$STAMP-ring.bin" >> "$LOG" 2>&1
	fi
	# also on a timeout kill (rc 124/137): a hang the watchdog misses (worker progressing, suspender stuck) is classified too
	if grep -aq PROBE-HANG "$LOG" || [ "$rc" = 124 ] || [ "$rc" = 137 ]; then
		if /usr/local/bin/sudo -n test -f "$RING"; then
			/usr/local/bin/sudo -n cat "$RING" | python3 "$DARLING_SRC_DIR/build-freebsd/diag-ring-decode.py" --hang-class /dev/stdin >> "$LOG" 2>&1
		else echo "HANG-CLASS none (no diag ring; set DSERVER_DIAG_RING)" >> "$LOG"; fi
	fi
	dsp_reap
	d=$(( $(date +%s) - START ))
	if ! grep -aq 'GUEST-EXEC' "$LOG"; then
		echo "$P-$i rc=$rc ${d}s/${T}s INVALID (no GUEST-EXEC)"
	else
		valid=$((valid+1))
		if grep -aq '^PROBE-OK' "$LOG"; then v=OK; ok=$((ok+1)); else v=BAD; bad=$((bad+1)); fi
		sig=$(grep -aE 'PROBE-HANG|FATAL signal|genuine SIGILL|SIGILL RE-RAISE|SIGILL-FAULT info|unfair|BAD (SEND|RECEIVE)|abort_with_payload|interrupt_enter failed' "$LOG" | head -1 | cut -c1-170)
		ev=
		if [ $v = BAD ] && { [ $rc = 124 ] || [ $rc = 137 ]; }; then
			if [ $((END - LM)) -ge "$STALL" ]; then ev=" stall_progress0 quiet=$((END - LM))s"; st=$((st+1))
			else ev=" timeout_hit"; th=$((th+1)); fi
			ev="$ev last=[$(grep -a 'suspend round' "$LOG" | tail -1)]"
		fi
		echo "$P-$i rc=$rc ${d}s/${T}s $v [${sig:-none}]$ev"
		grep -a "^RING-COUNTS" "$LOG" | head -1
		grep -a "^HANG-CLASS" "$LOG" | head -8
	fi
	i=$((i+1))
done
echo "SUMMARY $P: N=$N valid=$valid ok=$ok bad=$bad timeout_hits=$th stall_progress0=$st"
