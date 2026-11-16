#!/bin/sh
# chrome-series.sh - N-run Chrome series with an md5 gate and a one-line SERIES summary.
# usage: chrome-series.sh PREFIX N [TIMEOUT]    logs: $DARLING_BUILD_DIR/chrome-sr-<PREFIX>*.log
# Env: DARLING_SRC_DIR DARLING_OVERLAY DARLING_BUILD_DIR CHROME_APP (as for chrome-sr-batch.sh);
#      EXPECT_MLDR_MD5  if set and different from the installed mldr, exit 1 before running.
#      env-preflight.sh runs first (EXPECT_DSERVER_MD5, PREFLIGHT_MAX_LOAD, PREFLIGHT_PROBE=0, PREFLIGHT_IGNORE); exit 1 on fail.
#      stage-gate.sh runs after the preflight: STAGECHK line, exit 1 on a built/overlay/staged md5 mismatch.
#      CHROME_EXTRA_ARGS  extra Chrome flags (exported to chrome-sr-batch.sh, inserted before the URL).
# Valid run = has GUEST-EXEC. survived20 = valid runs with WRAPPER_DURATION > 20. top = most frequent WRAPPER_RC.
# Last line: SERIES <prefix> N=<n> valid=<v> survived20=<k> top=rc<code>x<count> windows=<runs with windows> mldr=<md5>
# Exit 1 if the gate fails or valid=0.
# exclusive run lock: one probe/Chrome run at a time; nested calls (DARLING_LOCK_HELD) do not re-lock
if [ -z "$DARLING_LOCK_HELD" ]; then
	: "${DARLING_BUILD_DIR:?}"
	DARLING_LOCK_HELD=1 lockf -t 0 "$DARLING_BUILD_DIR/.darling-run.lock" sh "$0" "$@"
	rc=$?
	[ $rc -eq 75 ] && { echo "LOCK busy"; exit 1; }
	exit $rc
fi
# On every exit: count darlingservers of our prefixes (this launcher's unique /tmp/darling-run.* or the
# old fixed one); print LEFTOVER dservers=<n> last and exit 1 if n>0.
leftover_report() {
	_rc=$?
	type cores_prune >/dev/null 2>&1 && cores_prune
	LEFT=$(ps -axww -o pid,command | awk '$2 ~ /(^|\/)darlingserver$/ && ($3 ~ /^\/tmp\/darling-run\./ || $3 == "/tmp/darling-dynamic-smoke-fresh") {n++} END{print n+0}')
	echo "LEFTOVER dservers=$LEFT"
	[ "$LEFT" -eq 0 ] || exit 1
	exit $_rc
}
trap leftover_report EXIT
export CHROME_EXTRA_ARGS; P=${1:?}; N=${2:?}; T=${3:-120}
: "${DARLING_SRC_DIR:?}" "${DARLING_BUILD_DIR:?}"
# Core files (a Mach core can be 14 GB): cap the size of any core this series' processes dump (ulimit -c, KB;
# CORE_LIMIT_KB, default 2 GiB; 0 = none) and keep at most one core, the newest, found afterwards in the build dir
# or the working directory; the others are deleted at exit. Delete the kept one after use (CORES line shows it).
CORE_LIMIT_KB=${CORE_LIMIT_KB:-2097152}
ulimit -c "$CORE_LIMIT_KB" 2>/dev/null
CORE_MARK=$(mktemp "${TMPDIR:-/tmp}/series-core-mark.XXXXXX")
cores_prune() {
	_cs=$(find "$DARLING_BUILD_DIR" "$PWD" -maxdepth 1 -name '*.core' -type f -newer "$CORE_MARK" 2>/dev/null | xargs -r ls -t 2>/dev/null)
	_n=0; _kept=""
	for _f in $_cs; do
		_n=$((_n+1))
		if [ $_n -eq 1 ]; then _kept="$_f ($(du -h "$_f" | cut -f1))"; else rm -f "$_f" 2>/dev/null || sudo -n rm -f "$_f"; fi
	done
	rm -f "$CORE_MARK"
	echo "CORES new=$_n kept=${_kept:-none} limit_kb=$CORE_LIMIT_KB"
}
MLDR=$DARLING_BUILD_DIR/dserver/mldr-real/mldr
DS=$DARLING_BUILD_DIR/dserver/darlingserver
M=$(md5 -q "$MLDR" 2>/dev/null || md5sum "$MLDR" | cut -d' ' -f1)
D=$(md5 -q "$DS" 2>/dev/null || md5sum "$DS" | cut -d' ' -f1)
echo "mldr md5=$M"; echo "darlingserver md5=$D"
# environment preflight (leftover servers, dserver md5, load, N=1 probe); see env-preflight.sh
sh "$DARLING_SRC_DIR/build-freebsd/env-preflight.sh" || exit 1
# md5 gate: built vs overlay vs staged copy of mldr and every rebuilt stub image (stage-gate.sh); mismatch -> exit 1
sh "$DARLING_SRC_DIR/build-freebsd/stage-gate.sh" || exit 1
if [ -n "$EXPECT_MLDR_MD5" ] && [ "$EXPECT_MLDR_MD5" != "$M" ]; then
	echo "GATE FAIL: expected mldr md5 $EXPECT_MLDR_MD5, installed $M"; exit 1
fi
sh "$DARLING_SRC_DIR/build-freebsd/chrome-sr-batch.sh" "$P" "$N" "$T"
valid=0; surv=0; rcs=""; wins=0
for L in "$DARLING_BUILD_DIR"/chrome-sr-"$P"*.log; do
	[ -f "$L" ] || continue
	grep -aq GUEST-EXEC "$L" || continue
	valid=$((valid+1))
	rc=$(sed -n 's/^WRAPPER_RC=//p' "$L" | tail -1)
	d=$(sed -n 's/^WRAPPER_DURATION=//p' "$L" | tail -1)
	d=${d%%[!0-9]*}
	[ "${d:-0}" -gt 20 ] && surv=$((surv+1))
	rcs="$rcs ${rc:-?}"
	w=$(sed -n 's/^SNAP windows=\([0-9]*\).*/\1/p' "$L" | tail -1)
	[ "${w:-0}" -gt 0 ] && wins=$((wins+1))
done
top="rc?x0"
[ -n "$rcs" ] && top=$(printf '%s\n' $rcs | sort | uniq -c | sort -rn | awk 'NR==1{print "rc" $2 "x" $1}')
echo "SERIES $P N=$N valid=$valid survived20=$surv top=$top windows=$wins mldr=$M args=\"$CHROME_EXTRA_ARGS\""
[ "$valid" -gt 0 ]
