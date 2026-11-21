#!/bin/sh
# chrome-wl-run.sh - run Chrome (Mach-O) under Darling against a Wayland
# compositor, one log per run.
#
# usage: chrome-wl-run.sh SUFFIX TIMEOUT [ENV=val ...]
#
# Required environment (no defaults, nothing machine specific here):
#   DARLING_SRC_DIR DARLING_OVERLAY DARLING_BUILD_DIR   as for launch-dynamic
#   CHROME_APP         host path of the .app bundle
#   XDG_RUNTIME_DIR    private (mode 0700) directory that holds the compositor
#                      socket; passed to the guest unchanged
#   WAYLAND_DISPLAY    socket name inside XDG_RUNTIME_DIR
# Optional:
#   DARLING_GUEST_EXPOSE  colon separated host directories linked into the guest
#                      root at the same path. Must cover XDG_RUNTIME_DIR (the
#                      socket is only connect()ed, so a link is enough).
#   DARLING_GUEST_MKDIR   colon separated guest directories created for real in
#                      the guest root (the profile directory).
#   DARLING_GUEST_COPY    colon separated host=guest file pairs copied into the
#                      guest root (the local page).
#   CHROME_ARGS        space separated argv for Chrome (launch-dynamic splits
#                      DARLING_TEST_ARGS on spaces: no quoting, so no spaces
#                      inside one argument, at most 30 arguments, 1023 bytes)
#
# How the environment reaches the guest: this script runs launch-dynamic under
# `sudo env NAME=val ...` (sudo resets the environment, only explicit env
# arguments survive); launch-dynamic only adds to what it inherited and execs
# mldr, which hands the whole environment to the guest. DARLING_TEST_ARGS
# becomes the guest's argv[1..].
#
# CORE_DIR (optional): inside sudo (which resets the core limit to 0) core-wrap.sh sets ulimit -c to
# CORE_LIMIT_KB (default and maximum 2 GiB) and cds there, so core files (kern.corefile %N.core, in the
# cwd) land outside the repo.
# CHROME_LOG_PREFIX (default chrome-wl) names the log: $DARLING_BUILD_DIR/<prefix>-SUFFIX.log
#
# Other useful NAME=val arguments: DARLING_KEEP_LOCAL=1 (do not re-stage the
# frameworks), DARLING_RPC_LOG=1 (darlingserver call/reply log).
# The log is never overwritten. WRAPPER_RC and WRAPPER_DURATION are the last
# lines; a timeout kill is reported as rc 124 and proves nothing by itself.
S=$1; T=$2; shift 2
: "${DARLING_SRC_DIR:?}" "${DARLING_OVERLAY:?}" "${DARLING_BUILD_DIR:?}"
: "${CHROME_APP:?}" "${XDG_RUNTIME_DIR:?}" "${WAYLAND_DISPLAY:?}"
LOG=$DARLING_BUILD_DIR/${CHROME_LOG_PREFIX:-chrome-wl}-$S.log
[ -e "$LOG" ] && { echo "exists $LOG"; exit 1; }
. "$DARLING_SRC_DIR/build-freebsd/dserver-prefix.sh"
dsp_new || exit 1
START=$(date +%s)
# Provenance header: a run whose binaries cannot be named is not evidence. Every batch log names the
# installed mldr and darlingserver by md5 plus the source HEAD, and repeats the build binaries that
# actually run (the installed pair may differ or be absent; MISSING says so, it is not a silent zero).
# DARLING_INSTALLED_LIBEXEC overrides the installed prefix (default: the standard Darling host prefix).
prov_md5() { [ -f "$1" ] || { echo MISSING; return; }; md5 -q "$1" 2>/dev/null || md5sum "$1" 2>/dev/null | cut -d' ' -f1; }
PROV_LIBEXEC=${DARLING_INSTALLED_LIBEXEC:-/usr/local/libexec/darling/usr/libexec/darling}
{
	echo "WRAPPER_START=$START timeout=$T"
	echo "PROVENANCE git_head=$(cd "$DARLING_SRC_DIR" 2>/dev/null && git rev-parse HEAD 2>/dev/null || echo unknown)"
	echo "PROVENANCE mldr_installed=$(prov_md5 "$PROV_LIBEXEC/mldr") darlingserver_installed=$(prov_md5 "$PROV_LIBEXEC/darlingserver")"
	echo "PROVENANCE mldr_build=$(prov_md5 "$DARLING_BUILD_DIR/dserver/mldr-real/mldr") darlingserver_build=$(prov_md5 "$DARLING_BUILD_DIR/dserver/darlingserver")"
} > "$LOG"
SUDO=${CHROME_SR_SUDO:-/usr/local/bin/sudo -n}
LAUNCH=${CHROME_SR_LAUNCH:-$DARLING_BUILD_DIR/launch-dynamic}
# DARLING_KTRACE=<file>: the launcher is run under `ktrace -i -t n -f <file>` INSIDE the sudo command below (the trace follows
# the exec of mldr and every guest process; a ktrace started outside sudo loses them at the privilege change). The file must
# lie inside DARLING_BUILD_DIR; anything else stops the run with a KTRACE FAIL line. Unset: nothing changes.
KT=""
if [ -n "$DARLING_KTRACE" ]; then
	case "$DARLING_KTRACE" in
	"$DARLING_BUILD_DIR"/*)
		# DARLING_KTRACE_POINTS: ktrace -t trace points, letters and + only (default n: path lookups); cn adds syscalls with results
		case "${DARLING_KTRACE_POINTS:-n}" in
		*[!a-zA-Z+]*) echo "KTRACE FAIL points=$DARLING_KTRACE_POINTS (allowed: letters and +)"; exit 2;;
		esac
		KT="ktrace -i -t ${DARLING_KTRACE_POINTS:-n} -f $DARLING_KTRACE";;
	*) echo "KTRACE FAIL path=$DARLING_KTRACE (not inside the build directory)"; exit 2;;
	esac
fi
BASE=$DARLING_BUILD_DIR/${CHROME_LOG_PREFIX:-chrome-wl}-$S
RCF=$BASE.rc.$$
rm -f "$RCF"
# Run in the background (own subshell, so kill -0 works on it) so that a hang can be dumped BEFORE timeout kills it.
# CHROME_SR_SERVER_LOG=<level> starts the server with DSERVER_LOG_LEVEL=<level>.
# Log cap: the run writes into a FIFO read by one awk (RDP) that drops the traplog/prctl lines (and the lone quote lines their multi-line records leave behind) and stops appending to the
# log after CHROME_SR_LOG_CAP bytes (default 200 MB; it keeps reading, so the run is never blocked or killed by the cap).
# A single reader process, so it can be stopped by pid once the run is over (daemons may keep the FIFO open).
FIFO=$BASE.fifo.$$
rm -f "$FIFO"; mkfifo "$FIFO" || exit 1
LC_ALL=C awk -v cap="${CHROME_SR_LOG_CAP:-209715200}" -v out="$LOG" \
  '!/traplog|prctl/ && $0 != "\"" { if (n < cap) { n += length($0) + 1; print >> out; fflush(out) } else if (!w) { w = 1; print "LOG-CAPPED at " cap " bytes" >> out; fflush(out) } }' < "$FIFO" &
RDP=$!
( $SUDO env \
  DARLING_SRC_DIR="$DARLING_SRC_DIR" DARLING_OVERLAY="$DARLING_OVERLAY" \
  DARLING_BUILD_DIR="$DARLING_BUILD_DIR" CORE_DIR="$CORE_DIR" CORE_LIMIT_KB="$CORE_LIMIT_KB" DARLING_PREFIX="$DARLING_PREFIX" \
  DARLING_TEST_BINARY=chrome-macho \
  DARLING_STAGING_TREES=System/Library/Frameworks:System/Library/PrivateFrameworks:usr/lib \
  DARLING_TRAP_LOG=1 \
  XDG_RUNTIME_DIR="$XDG_RUNTIME_DIR" WAYLAND_DISPLAY="$WAYLAND_DISPLAY" \
  DARLING_TEST_ARGS="$CHROME_ARGS" \
  DARLING_GUEST_EXPOSE="$DARLING_GUEST_EXPOSE" \
  DARLING_GUEST_MKDIR="$DARLING_GUEST_MKDIR" DARLING_GUEST_COPY="$DARLING_GUEST_COPY" \
  ${CHROME_SR_SERVER_LOG:+DSERVER_LOG_LEVEL=$CHROME_SR_SERVER_LOG} \
  "$@" CHROME_APP="$CHROME_APP" \
  sh -c 'if [ -n "$CORE_DIR" ]; then exec sh "$DARLING_SRC_DIR/build-freebsd/core-wrap.sh" run "$@"; fi; exec "$@"' sh \
  $KT timeout --foreground -k 5 "$T" "$LAUNCH"; echo $? > "$RCF" ) > "$FIFO" 2>&1 &
BGP=$!
# Hang dump: CHROME_SR_DUMP_LEAD seconds (default 40) before the timeout, if the run is still alive, run
# ds-microthreads-dump.sh on the server pid and guest-threads-snapshot.sh on the guest pid. Both pids come from the
# launcher's own log lines ("[launch] DSERVER pid=" and "GUEST-EXEC pid="), no process search.
# CHROME_SR_DUMP=0 disables. Results: <log base>-dsdump.txt and -guest.txt.
LEAD=${CHROME_SR_DUMP_LEAD:-40}
[ "$LEAD" -lt "$T" ] || LEAD=$((T-1))
DUMPED=0
SEGVP=""
# CHROME_SR_SEGV=1: once the launcher logs GUEST-EXEC pid=, guest-segv-catch.sh attaches to that pid (dtrace probe on that pid; no ptrace) and writes <log base>-segv.txt ending in a SEGV-VERDICT line. Off by default; the guest is otherwise unchanged.
while kill -0 $BGP 2>/dev/null; do
	if [ -z "$SEGVP" ] && [ "${CHROME_SR_SEGV:-0}" = 1 ]; then
		G2=$(grep -a 'GUEST-EXEC pid=' "$LOG" | tail -1 | sed 's/.*GUEST-EXEC pid=\([0-9]*\).*/\1/')
		if [ -n "$G2" ]; then
			sh "${CHROME_SR_SEGVCATCH:-$DARLING_SRC_DIR/build-freebsd/guest-segv-catch.sh}" "$G2" "$LOG" > "$BASE-segv.txt" 2>&1 &
			SEGVP=$!
		fi
	fi
	if [ $DUMPED -eq 0 ] && [ "${CHROME_SR_DUMP:-1}" != 0 ] && [ $(( $(date +%s) - START )) -ge $((T-LEAD)) ]; then
		DUMPED=1
		SPID=$(grep -a '^\[launch\] DSERVER pid=' "$LOG" | tail -1 | sed 's/.*pid=\([0-9]*\).*/\1/')
		GPID=$(grep -a 'GUEST-EXEC pid=' "$LOG" | tail -1 | sed 's/.*GUEST-EXEC pid=\([0-9]*\).*/\1/')
		echo "HANGDUMP server=${SPID:-none} guest=${GPID:-none} at=$(( $(date +%s) - START ))s" >> "$LOG"
		DDIR=$DARLING_SRC_DIR/build-freebsd
		[ -n "$SPID" ] && $SUDO timeout -k 5 30 sh "${CHROME_SR_DSDUMP:-$DDIR/ds-microthreads-dump.sh}" "$SPID" > "$BASE-dsdump.txt" 2>&1
		[ -n "$GPID" ] && $SUDO timeout -k 5 30 sh "${CHROME_SR_GUESTSNAP:-$DDIR/guest-threads-snapshot.sh}" "$GPID" "$LOG" > "$BASE-guest.txt" 2>&1
	fi
	sleep 1
done
wait $BGP
[ -n "$SEGVP" ] && wait $SEGVP
RC=$(cat "$RCF" 2>/dev/null); RC=${RC:-1}
rm -f "$RCF"
# let the reader drain what is already in the FIFO, then stop it by pid
i=0; while [ $i -lt 3 ] && kill -0 $RDP 2>/dev/null; do sleep 1; i=$((i+1)); done
kill $RDP 2>/dev/null; wait $RDP 2>/dev/null; rm -f "$FIFO"
# server log (only with CHROME_SR_SERVER_LOG), copied before the prefix is removed; capped at 100 MB (tail)
if [ -n "$CHROME_SR_SERVER_LOG" ]; then
	$SUDO tail -c 100000000 "$DARLING_PREFIX/private/var/log/dserver.log" > "$BASE-dserver.txt" 2>/dev/null
fi
dsp_reap
echo "WRAPPER_RC=$RC" >> "$LOG"
echo "WRAPPER_DURATION=$(( $(date +%s) - START ))s TIMEOUT=${T}s" >> "$LOG"
