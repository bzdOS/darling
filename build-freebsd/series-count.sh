#!/bin/sh
# series-count.sh - count the runs of one chrome-series prefix. Sourced by chrome-series.sh.
# A run's log is exactly $DIR/chrome-sr-$P$i.log, i = 1..N. A glob "chrome-sr-$P*.log" also took the
# batch's own summary log (chrome-sr-$P.log) and every other series whose prefix starts with $P
# (fix3 matched fix3.log, fix31..fix310: valid=11 for N=10).
# Sets: valid surv rcs wins fwdmiss fwdlist
#   fwdmiss = number of unique "OBJC-FWD-MISS -[Class sel]" / "+[Class sel]" lines over the valid runs' logs,
#   fwdlist = those selectors, one per line, sorted.
series_count() {
	_dir=$1; _p=$2; _n=$3
	valid=0; surv=0; rcs=""; wins=0; fwdmiss=0; fwdlist=""; _fl=""
	_i=1
	while [ "$_i" -le "$_n" ]; do
		L="$_dir/chrome-sr-$_p$_i.log"; _i=$((_i+1))
		[ -f "$L" ] || continue
		grep -aq GUEST-EXEC "$L" || continue
		valid=$((valid+1)); _fl="$_fl $L"
		rc=$(sed -n 's/^WRAPPER_RC=//p' "$L" | tail -1)
		d=$(sed -n 's/^WRAPPER_DURATION=//p' "$L" | tail -1)
		d=${d%%[!0-9]*}
		[ "${d:-0}" -gt 20 ] && surv=$((surv+1))
		rcs="$rcs ${rc:-?}"
		w=$(sed -n 's/^SNAP windows=\([0-9]*\).*/\1/p' "$L" | tail -1)
		[ "${w:-0}" -gt 0 ] && wins=$((wins+1))
	done
	if [ -n "$_fl" ]; then
		# shellcheck disable=SC2086
		fwdlist=$(cat $_fl | sed -n 's/^.*OBJC-FWD-MISS \([-+]\[[^]]*\]\).*$/\1/p' | sort -u)
		[ -n "$fwdlist" ] && fwdmiss=$(printf '%s\n' "$fwdlist" | grep -c .)
	fi
	if [ "$valid" -gt "$_n" ]; then echo "SERIES-COUNT-BUG valid=$valid > N=$_n"; return 1; fi
	return 0
}
