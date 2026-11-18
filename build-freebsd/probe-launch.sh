#!/bin/sh
# probe-launch.sh - sourced by the darling probes (. "$(dirname "$0")/probe-launch.sh").
# probe_launch LOG WAIT PATTERN cmd...: truncate LOG synchronously, run cmd in the background appending to LOG,
# wait until PATTERN is in LOG, cmd is gone, or WAIT seconds pass. Status: 0 pattern seen, 1 cmd exited without it, 2 timeout.
# Truncating here (not inside the launching subshell) stops a wait loop from matching the previous run's log.
probe_launch() {
	_pl_log=$1; _pl_wait=$2; _pl_pat=$3; shift 3
	: > "$_pl_log"
	"$@" >> "$_pl_log" 2>&1 &
	_pl_pid=$!
	_pl_i=0
	while [ "$_pl_i" -lt "$_pl_wait" ]; do
		grep -qa "$_pl_pat" "$_pl_log" 2>/dev/null && return 0
		case "$(ps -o state= -p "$_pl_pid" 2>/dev/null)" in
			""|Z*) grep -qa "$_pl_pat" "$_pl_log" 2>/dev/null && return 0; return 1;;
		esac
		sleep 1; _pl_i=$((_pl_i+1))
	done
	return 2
}
