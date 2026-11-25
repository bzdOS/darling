#!/bin/sh
# unp-table watcher v3: sample the AF_UNIX table at >=20 Hz, pairing every
# table sample with the server's liveness in the SAME sample. A single
# snapshot cannot answer "was the endpoint in the table while the server
# lived": the server's whole life can be shorter than one second.
#
# The caller's sandbox hides the process table entirely (measured: ps -axo
# prints only the header; pgrep -x init finds nothing), so this script
# re-execs itself once as root through the same sudo route the run wrapper
# already uses, and then samples with direct root calls. v2 lesson: a sudo
# spawn per sample costs ~0.5-1 s and collapses the rate to ~2 Hz inside the
# live window, and log-based pid discovery lagged until the server's death.
#
# usage: mldr-unp-table-watcher.sh OUT_FILE DURATION_SEC [RUN_LOG]
#
# v4: every sample also tests the run's own socket FILE. The launcher polls
# that file with fstatat and starts the guest only after it exists, and the
# server unlinks/binds it itself (trace: dserver unlink -> bind(3) -> 0), so
# file=yes pins the sample to AFTER the bind while the file survives until
# the server's shutdown. A sample with file=y AND alive=<pid> AND rows=0 is
# therefore "post-bind, server alive, endpoint absent from the table" — no
# second-granularity ambiguity. The prefix comes from the run log's
# send-probe line (printed at guest start, inside the live window), not from
# the end-of-run summary block.
#
# Output lines (OUT_FILE):
#   W <sample> t=<epoch> pid=<pid|none> alive=<pid|no> file=<y|n|?> rows=<n> mine=<m> [|row|..]
#       rows are every netstat -f unix line carrying ".darlingserver.sock";
#       mine counts rows under the run's own prefix (from RUN_LOG's send-probe
#       line, falling back to the summary block), so foreign leaked prefixes
#       are separated per sample.
#       alive is the root procstat verdict for the SAME sample.
#       file is test -S on <prefix>/.darlingserver.sock for the SAME sample
#       (? = prefix not discovered yet).
#   RULE t=<epoch> pid=<pid> <jail/credential/chroot/socket-state block>
#       fired once at the first sample with a live server pid.
#   W-END samples=<n> live=<k>
#
# Never pgrep -f: it matches this watcher's own argv and finds the watcher.

if [ "$(id -u)" != "0" ]; then
	exec sudo -n sh "$0" "$@"
fi
OUT=${1:?OUT_FILE}; DUR=${2:?DURATION_SEC}; RUNLOG=${3:-}
WEND=$(( $(date +%s) + DUR ))
i=0
live=0
ruled=0
prefix=""
pid=""
while [ "$(date +%s)" -lt "$WEND" ]; do
	i=$((i+1))
	pid=$(pgrep -x darlingserver | head -1)
	[ -z "$pid" ] && [ -n "$RUNLOG" ] && [ -r "$RUNLOG" ] && \
		pid=$(grep -a '^\[launch\] DSERVER pid=' "$RUNLOG" 2>/dev/null | tail -1 | sed 's/.*pid=\([0-9]*\).*/\1/')
	[ -z "$prefix" ] && [ -n "$RUNLOG" ] && [ -r "$RUNLOG" ] && \
		prefix=$(grep -a 'send-probe.*path=' "$RUNLOG" 2>/dev/null | tail -1 | sed 's/.*path=//; s/\.darlingserver\.sock.*//; s:/$::')
	[ -z "$prefix" ] && [ -n "$RUNLOG" ] && [ -r "$RUNLOG" ] && \
		prefix=$(grep -a '^prefix  *: /tmp/darling-run\.' "$RUNLOG" 2>/dev/null | tail -1 | sed 's/^prefix  *: //')
	rows=$(netstat -f unix 2>/dev/null | grep -F '.darlingserver.sock')
	n=$(printf '%s\n' "$rows" | grep -c '\.darlingserver\.sock')
	m=0
	[ -n "$prefix" ] && m=$(printf '%s' "$rows" | grep -cF "$prefix")
	fl="?"
	[ -n "$prefix" ] && { [ -S "$prefix/.darlingserver.sock" ] && fl="y" || fl="n"; }
	alive=no
	if [ -n "$pid" ]; then
		lv=$(procstat basic "$pid" 2>/dev/null | awk 'NR==2{print $1}')
		[ "$lv" = "$pid" ] && alive="$pid"
	fi
	if [ "$alive" != "no" ]; then
		live=$((live+1))
		if [ "$ruled" = 0 ]; then
			ruled=1
			{
				echo "RULE t=$(date +%s) pid=$pid"
				echo "jls:"
				jls
				echo "credential:"
				procstat credential "$pid"
				echo "pwdx (a chroot shows a path under the guest root):"
				procstat pwdx "$pid"
				echo "fd rows named root/cwd:"
				procstat -f "$pid" | grep -E "(^|[[:space:]])(root|cwd)([[:space:]]|$)" || echo "(none)"
				echo "sockstat -u for this pid:"
				sockstat -u | awk -v p="$pid" '$3==p'
				echo "ps flags:"
				ps -p "$pid" -o pid,flags
			} >> "$OUT"
		fi
	fi
	if [ "$n" -gt 0 ]; then
		printf 'W %d t=%s pid=%s alive=%s file=%s rows=%d mine=%d %s\n' "$i" "$(date +%s)" "${pid:-none}" "$alive" "$fl" "$n" "$m" "$(printf '%s' "$rows" | tr '\n' '|')" >> "$OUT"
	else
		printf 'W %d t=%s pid=%s alive=%s file=%s rows=0 mine=0\n' "$i" "$(date +%s)" "${pid:-none}" "$alive" "$fl" >> "$OUT"
	fi
	sleep 0.02
done
echo "W-END samples=$i live=$live" >> "$OUT"
