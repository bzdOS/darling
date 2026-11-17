# series-guard.sh - sourced with `.`; defines series_guard.
#   series_guard returns 0 when no series is running (or DARLING_LOCK_HELD=1: the caller holds the run lock),
#   otherwise prints "FATAL: a series is running" to stderr and returns 2.
#   SERIES_LOCK_FILE: lock file path to look for in a lockf command line.
#   Default: any lockf on darling-run.lock (the run lock in DARLING_BUILD_DIR).
series_guard() {
	[ "${DARLING_LOCK_HELD:-}" = 1 ] && return 0
	_sg_pat="${SERIES_LOCK_FILE:-darling-run.lock}"
	if pgrep -f "lockf.*${_sg_pat}" >/dev/null 2>&1; then
		echo "FATAL: a series is running" >&2
		return 2
	fi
	return 0
}
