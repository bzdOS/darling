#!/bin/sh
# ds-microthreads-dump.sh PID [maxframes]
# Attach lldb to a running darlingserver, dump every microthread (see ds_microthreads_dump.py), detach.
# Output on stdout. The server is stopped for the duration (about a second); behaviour is not changed.
# Needs root (or ptrace rights) and lldb with the darlingserver binary's debug info.
pid=${1:?usage: ds-microthreads-dump.sh PID [maxframes]}
n=${2:-40}
d=$(cd "$(dirname "$0")" && pwd)
exe=$(procstat -b "$pid" 2>/dev/null | awk 'NR==2{print $4}')
exec lldb -b ${exe:+--file "$exe"} \
	-o "process attach --pid $pid" \
	-o "command script import $d/ds_microthreads_dump.py" \
	-o "dsdump $n" \
	-o "process detach"
