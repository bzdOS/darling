#!/bin/sh
# unp fd-view watcher v5: same paired sampling as the v4 unp-table watcher,
# with two changes that answer "what does the server hold on fd 3 while the
# path is absent from the table":
#
# 1. The run's OWN server pid wins. v4 preferred `pgrep -x darlingserver`
#    and only fell back to the launch log; a leaked server of an earlier run
#    would have poisoned every sample (liveness of the wrong process, file
#    probe of the right prefix). v5 reads "[launch] DSERVER pid=" from the
#    run log first and only pgreps when no log pid exists.
#
# 2. Every sample with a live server also captures its fd table: the raw
#    `procstat -f` rows (capped) plus the process's unix-socket rows from
#    `sockstat -u`, taken in the SAME sample as file=y / rows=0. If fd 3 is
#    a bound unix socket there, the kernel's table view is the problem; if
#    fd 3 is gone or not a socket, someone on our side closed or replaced
#    the listener between bind and the guest's checkin send.
#
# usage: mldr-unp-fdwatch.sh OUT_FILE DURATION_SEC [RUN_LOG]
#
# The caller's sandbox hides the process table (measured in v3: ps -axo
# prints only the header), so this script re-execs itself once as root
# through the same sudo route the run wrapper already uses; every later
# call is a direct root call. A sudo spawn per sample costs ~0.5-1 s and
# collapses the rate inside the live window.
#
# Output lines (OUT_FILE):
#   W <sample> t=<epoch> pid=<pid|none> alive=<pid|no> file=<y|n|?> rows=<n> mine=<m> [|row|..]
#       identical to v4: netstat -f unix rows carrying ".darlingserver.sock",
#       mine counts rows under the run's own prefix (from the log's
#       send-probe line, falling back to the summary block).
#   FD <sample> t=<epoch> pid=<pid> nfd=<n>
#       followed by up to 40 raw `procstat -f <pid>` lines, and
#   SOCK <sample> t=<epoch> pid=<pid> <sockstat -u rows for the pid | none>
#       for the same sample; only emitted while the server is alive.
#   RULE t=<epoch> pid=<pid> <jail/credential/chroot/socket-state block>
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
logpid=""
while [ "$(date +%s)" -lt "$WEND" ]; do
	i=$((i+1))
	# v5: the run's own server first; pgrep only without a run log.
	if [ -z "$logpid" ] && [ -n "$RUNLOG" ] && [ -r "$RUNLOG" ]; then
		logpid=$(grep -a '^\[launch\] DSERVER pid=' "$RUNLOG" 2>/dev/null | tail -1 | sed 's/.*pid=\([0-9]*\).*/\1/')
	fi
	if [ -n "$logpid" ] && [ "$logpid" != "0" ]; then
		pid=$logpid
	else
		pid=$(pgrep -x darlingserver | head -1)
	fi
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
		# v5: the fd view for the SAME sample.
		{
			echo "FD $i t=$(date +%s) pid=$pid nfd=$(procstat -f "$pid" 2>/dev/null | wc -l)"
			procstat -f "$pid" 2>/dev/null | head -40
			echo "SOCK $i t=$(date +%s) pid=$pid $(sockstat -u 2>/dev/null | awk -v p="$pid" '$3==p' | tr '\n' ';')"
		} >> "$OUT"
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
