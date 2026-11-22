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
# The diag ring is always on with at least 4M entries (a hang that lasts tens of seconds wraps a smaller ring and loses the
# start of the stuck thread: kind D, planck-1517). An explicit DSERVER_DIAG_RING in PROBE_EXTRA_ENV below 4194304 is raised.
case " $PROBE_EXTRA_ENV " in
	*" DSERVER_DIAG_RING="*)
		PROBE_EXTRA_ENV=$(printf '%s' "$PROBE_EXTRA_ENV" | awk '{ for (i = 1; i <= NF; i++) if ($i ~ /^DSERVER_DIAG_RING=/) { v = substr($i, 19) + 0; if (v < 4194304) $i = "DSERVER_DIAG_RING=4194304" } print }')
		;;
	*) PROBE_EXTRA_ENV="$PROBE_EXTRA_ENV DSERVER_DIAG_RING=4194304" ;;
esac
PROBE_ENV=PROBE_RUN=1; [ -n "$PROBE_TRAP_LOG" ] && PROBE_ENV=DARLING_TRAP_LOG=1
KT=; [ -n "$PROBE_KTRACE" ] && KT="ktrace -i -t cs -f $DARLING_BUILD_DIR/$P-ktr.out"
. "$DARLING_SRC_DIR/build-freebsd/dserver-prefix.sh"
ok=0; bad=0; valid=0; i=1; th=0; st=0; STALL=${PROBE_STALL_S:-60}
while [ $valid -lt "$N" ] && [ $i -le $((N*3)) ]; do
	STAMP=$(date +%m%d-%H%M%S); LOG=$DARLING_BUILD_DIR/$P-$STAMP.log
	[ -e "$LOG" ] && { echo "exists $LOG"; exit 1; }
	dsp_new || exit 1
	START=$(date +%s)
	# Host-state snapshot of a stalled guest process (before the timeout kill): when the run log has been quiet for PROBE_SNAP_S seconds
	# (default 30) the watcher writes procstat -kk / -t and ps -H of every guest process (match: the probe binary name) to
	# PREFIX-<stamp>-procstat.txt, once. It shows whether the whole process is stopped (SIGSTOP, task_suspend hitting the wrong thread) or
	# where each thread sits (kernel wait channel, sigsuspend). Nothing is written when the log never goes quiet.
	SNAP="$DARLING_BUILD_DIR/$P-$STAMP-procstat.txt"
	GONE="$DARLING_BUILD_DIR/$P-$STAMP-srvgone.txt"
	( _sp=
	  while :; do
		sleep 2
		[ -f "$LOG" ] || continue
		# the server process disappearing while the guest is still alive (the -111 failures: clients get ECONNREFUSED because the
		# server is gone): note it at once, with the listener path, before the timeout kill tears everything down
		[ -n "$_sp" ] || _sp=$(sed -n 's/^\[launch\] DSERVER pid=\([0-9]*\).*/\1/p' "$LOG" | head -1)
		if [ -n "$_sp" ] && [ ! -e "$GONE" ] && ! ps -p "$_sp" >/dev/null 2>&1 && [ -n "$(pgrep -f "$PB")" ]; then
			{
				echo "# darlingserver pid $_sp is gone while the guest is alive, at $(date +%s)"
				_sock=$(sed -n 's/^darlingserver socket ready: //p' "$LOG" | head -1)
				[ -n "$_sock" ] && { echo "-- listener path $_sock"; ls -l "$_sock" 2>&1; }
				echo "-- last 20 lines of the run log"; tail -n 20 "$LOG"
			} > "$GONE" 2>&1
		fi
		lm=$(stat -f %m "$LOG" 2>/dev/null) || continue
		if [ $(( $(date +%s) - lm )) -ge "${PROBE_SNAP_S:-15}" ] && [ ! -e "$SNAP" ]; then
			{
				echo "# snapshot at $(date +%s), log quiet for >= ${PROBE_SNAP_S:-15} s"
				for gp in $(pgrep -f "$PB"); do
					echo "== pid $gp"; ps -o pid,state,wchan,command -p "$gp"
					echo "-- procstat -t"; /usr/local/bin/sudo -n procstat -t "$gp"
					echo "-- procstat -kk"; /usr/local/bin/sudo -n procstat -kk "$gp"
					echo "-- ps -H"; ps -H -o pid,tid,state,wchan,comm -p "$gp"
				done
				# the darlingserver itself: where its threads sit (a hung main loop) and which sockets it holds (the listener)
				_sps=$(sed -n 's/^\[launch\] DSERVER pid=\([0-9]*\).*/\1/p' "$LOG" | head -1)
				[ -n "$_sps" ] && ! ps -p "$_sps" >/dev/null 2>&1 && _sps=""
				[ -n "$_sps" ] || echo "== darlingserver process not found (exited before the snapshot)"
				_sock=$(sed -n 's/^darlingserver socket ready: //p' "$LOG" | head -1)
				[ -n "$_sock" ] && { echo "-- listener path $_sock"; ls -l "$_sock" 2>&1; }
				for sp in $_sps; do
					echo "== darlingserver pid $sp"; ps -o pid,state,wchan,command -p "$sp"
					echo "-- procstat -t"; /usr/local/bin/sudo -n procstat -t "$sp"
					echo "-- procstat -kk"; /usr/local/bin/sudo -n procstat -kk "$sp"
					echo "-- fstat"; /usr/local/bin/sudo -n fstat -p "$sp" | grep -i 'sock\|pipe\| 0 \| 1 \| 2 ' | head -40
				done
			} > "$SNAP" 2>&1
		fi
	done ) &
	SNAPW=$!
	/usr/local/bin/sudo -n env DARLING_PREFIX="$DARLING_PREFIX" DARLING_SRC_DIR="$DARLING_SRC_DIR" DARLING_OVERLAY="$DARLING_OVERLAY" \
	  DARLING_BUILD_DIR="$DARLING_BUILD_DIR" DARLING_TEST_BINARY="$PB" DARLING_KEEP_LOCAL=1 \
	  DARLING_TEST_ARGS="$SU $SP $IW" $PROBE_ENV ${SL:+DSERVER_LOG_LEVEL=$SL} ${DSERVER_SOCKET_RCVBUF:+DSERVER_SOCKET_RCVBUF=$DSERVER_SOCKET_RCVBUF} $PROBE_EXTRA_ENV \
	  $KT timeout -k 5 "$T" "$DARLING_BUILD_DIR/launch-dynamic" > "$LOG" 2>&1
	rc=$?
	kill "$SNAPW" 2>/dev/null; wait "$SNAPW" 2>/dev/null
	END=$(date +%s); LM=$(stat -f %m "$LOG" 2>/dev/null || echo "$END")
	# exit code always in the run log and in the per-rc tally of the SUMMARY line (rc0/rc3/rc124/rc137/other)
	echo "PROBE-RC rc=$rc" >> "$LOG"
	grep -aq '^PROBE-WARN: worker progress' "$LOG" && wnp=$((wnp+1))
	# WORKER-STUCK: WORKER-SNAP shows the same stage and stage counters for >= 20 s while suspend rounds still advance
	# (a suspend round line or "suspender done" after the stuck span began)
	if awk '/^WORKER-SNAP/ { k=$2" "$4; t=$NF; sub("t=","",t); if (k!=pk) { pk=k; t0=t } else if (t-t0>=20) { st=t0 } }
	        /^suspend round|^suspender done/ { t=$NF; sub("t=","",t); if (t>ts) ts=t }
	        END { exit !(st && ts>st) }' "$LOG"; then echo "WORKER-STUCK" >> "$LOG"; wst=$((wst+1)); fi
	case "$rc" in 0) r0=$((r0+1));; 3) r3=$((r3+1));; 124) r124=$((r124+1));; 137) r137=$((r137+1));; *) rx=$((rx+1));; esac
	# The server's stderr is inherited by the run log: on a failed run keep its own lines (exit messages, lifetime pipe) next to it
	if [ "$rc" != 0 ]; then
		grep -a '^darlingserver\|Server exited\|lifetime pipe\|^\[dserver\|^dserver' "$LOG" > "$DARLING_BUILD_DIR/$P-$STAMP-server-stderr.txt" 2>/dev/null
		[ -s "$DARLING_BUILD_DIR/$P-$STAMP-server-stderr.txt" ] || echo "(no server stderr lines in the run log)" > "$DARLING_BUILD_DIR/$P-$STAMP-server-stderr.txt"
	fi
	# server log only for a failed run (rc != 0): a passing run's log is never written, so it costs no disk.
	# PROBE_SERVER_LOG_HEAD=1: also the first 100 MB (start of a suspend/resume loop the tail has lost); the whole log from the start is always saved as -dserver-full.txt (cap PROBE_SERVER_LOG_MAX)
	if [ -n "$SL" ] && [ "$rc" != 0 ]; then
		/usr/local/bin/sudo -n tail -c 100000000 "$DARLING_PREFIX/private/var/log/dserver.log" > "$DARLING_BUILD_DIR/$P-$STAMP-dserver.txt" 2>/dev/null
		# the whole log from its start (up to PROBE_SERVER_LOG_MAX bytes, default 1000000000): the start of a stuck thread
		# (its checkin) is not in the 100 MB tail
		/usr/local/bin/sudo -n head -c "${PROBE_SERVER_LOG_MAX:-1000000000}" "$DARLING_PREFIX/private/var/log/dserver.log" > "$DARLING_BUILD_DIR/$P-$STAMP-dserver-full.txt" 2>/dev/null
		[ -n "$PROBE_SERVER_LOG_HEAD" ] && /usr/local/bin/sudo -n head -c 100000000 "$DARLING_PREFIX/private/var/log/dserver.log" > "$DARLING_BUILD_DIR/$P-$STAMP-dserver-head.txt" 2>/dev/null
	fi
	# PROBE-HANG or timeout: one HANG-CLASS line per thread from the diag ring (needs DSERVER_DIAG_RING in PROBE_EXTRA_ENV):
	# saved_reply=yes is livelock kind A, saved_reply=no kind B. Appended to the run log, before the prefix is reaped.
	RING="$DARLING_PREFIX/private/var/log/dserver-diag-ring.bin"
	# Every run with a ring: keep it as PREFIX-<stamp>-ring.bin and append a RING-COUNTS line (k8/k9 > 0: the ring is written).
	if /usr/local/bin/sudo -n test -f "$RING"; then
		/usr/local/bin/sudo -n cat "$RING" > "$DARLING_BUILD_DIR/$P-$STAMP-ring.bin" 2>/dev/null
		python3 "$DARLING_SRC_DIR/build-freebsd/diag-ring-decode.py" --counts "$DARLING_BUILD_DIR/$P-$STAMP-ring.bin" >> "$LOG" 2>&1
		# SUSPEND-BALANCE: every suspender round is either signalled (kind 5) or coalesced into a signal already in flight
		# (kind 6): requested = signalled + coalesced must equal the rounds the suspender completed; resent = watchdog re-sends (kind 14)
		python3 "$DARLING_SRC_DIR/build-freebsd/diag-ring-decode.py" --hang-class "$DARLING_BUILD_DIR/$P-$STAMP-ring.bin" 2>/dev/null | grep -a '^HANG-CLASS-SUSP-TOTAL' | head -1 \
		  | awk -v rounds="$(sed -n 's/^suspender done rounds=\([0-9]*\).*/\1/p' "$LOG" | head -1)" '{ for (i = 1; i <= NF; i++) { split($i, kv, "="); v[kv[1]] = kv[2] }
		      req = v["signalled5"] + v["held6"]; print "SUSPEND-BALANCE rounds=" (rounds == "" ? "none" : rounds) " signalled=" v["signalled5"] " coalesced=" v["held6"] " requested=" req " balanced=" ((rounds != "" && req == rounds) ? "yes" : "no") }' >> "$LOG"
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
		sig=$(grep -aE 'PROBE-HANG|FATAL signal|genuine SIGILL|SIGILL RE-RAISE|SIGILL-FAULT info|unfair|BAD (SEND|RECEIVE)|abort_with_payload|interrupt_enter failed' "$LOG" | head -1 | cut -c1-170)
		# rc != 0 with no classification (no PROBE-HANG naming a role, no crash signature): UNCLASSIFIED, counted
		# apart and in neither OK nor BAD, so a blind classifier cannot move the frequencies
		if grep -aq '^PROBE-OK' "$LOG"; then v=OK; ok=$((ok+1))
		elif [ "$rc" != 0 ] && [ -z "$sig" ]; then v=UNCLASSIFIED; uc=$((uc+1))
		else v=BAD; bad=$((bad+1)); fi
		ev=
		if [ $v = BAD ] && { [ $rc = 124 ] || [ $rc = 137 ]; }; then
			if [ $((END - LM)) -ge "$STALL" ]; then ev=" stall_progress0 quiet=$((END - LM))s"; st=$((st+1))
			else ev=" timeout_hit"; th=$((th+1)); fi
			ev="$ev last=[$(grep -a 'suspend round' "$LOG" | tail -1)]"
		fi
		echo "$P-$i rc=$rc ${d}s/${T}s $v [${sig:-none}]$ev"
		grep -a "^RING-COUNTS" "$LOG" | head -1
		grep -a "^SUSPEND-BALANCE" "$LOG" | head -1
		grep -a "^SIG-CYCLES" "$LOG" | head -1
		grep -a "^MEMACCESS-" "$LOG" | head -2
		grep -a "^PUSH-REPLY-ACK" "$LOG" | head -1
		grep -a "^SIG-LOSS-STATE" "$LOG" | head -6
		grep -a "^OUTBOX" "$LOG" | head -1
		grep -a "^HANG-CLASS" "$LOG" | head -8
	fi
	i=$((i+1))
done
echo "SUMMARY $P: N=$N valid=$valid ok=$ok bad=$bad timeout_hits=$th stall_progress0=$st unclassified=${uc:-0} rc0=${r0:-0} rc3=${r3:-0} rc124=${r124:-0} rc137=${r137:-0} rc_other=${rx:-0} WORKER-NOPROGRESS=${wnp:-0} WORKER-STUCK=${wst:-0} (all runs, INVALID included)"
