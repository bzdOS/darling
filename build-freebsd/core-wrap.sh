#!/bin/sh
# core-wrap.sh - core-file handling shared by chrome-wl-run.sh and chrome-series.sh.
#   core-wrap.sh run CMD...   set the core limit (CORE_LIMIT_KB, default 2097152 = 2 GiB, never above),
#                             cd to CORE_DIR (kern.corefile %N.core lands in the cwd) and exec CMD.
#                             Run it INSIDE sudo: sudo resets the limit to 0.
#   core-wrap.sh prune        keep only the newest *.core in CORE_DIR, delete the rest, print a CORES line.
: "${CORE_DIR:?}"
L=${CORE_LIMIT_KB:-2097152}
case "$L" in ''|*[!0-9]*) L=2097152 ;; esac
[ "$L" -gt 2097152 ] && L=2097152
case "$1" in
run)
	shift; mkdir -p "$CORE_DIR" || exit 1
	ulimit -c "$L" || exit 1
	cd "$CORE_DIR" || exit 1
	exec "$@" ;;
prune)
	n=0; kept=""
	for f in $(ls -t "$CORE_DIR"/*.core 2>/dev/null); do
		n=$((n+1))
		if [ $n -eq 1 ]; then kept="$f ($(du -h "$f" | cut -f1))"; else rm -f "$f" 2>/dev/null || sudo -n rm -f "$f"; fi
	done
	echo "CORES found=$n kept=${kept:-none} dir=$CORE_DIR limit_kb=$L" ;;
*) echo "usage: core-wrap.sh run CMD... | prune" >&2; exit 2 ;;
esac
