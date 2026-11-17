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
	while [ "$_i" -le $((2*_n)) ]; do
		L="$_dir/chrome-sr-$_p$_i.log"; _i=$((_i+1))
		[ -f "$L" ] || continue
		grep -aq GUEST-EXEC "$L" || continue
		grep -aq '^RUN-INVALID' "$L" && continue
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

# series_summary_write BUILD_DIR PREFIX N LINE...  - durable summary file $SERIES_SUMMARY_DIR/<prefix>.txt
# (default <parent of the build dir>/series-summaries): the SERIES (or SERIES-ABORT) line(s) plus a per-run table
# "run <i> <valid|invalid:reason|noexec> rc=<rc> dur=<s>". Overwrites the file; returns 1 if it cannot be written.
series_summary_write() {
	_bd=$1; _sd=${SERIES_SUMMARY_DIR:-$(dirname "$_bd")/series-summaries}; _p=$2; _n=$3; shift 3
	mkdir -p "$_sd" 2>/dev/null || return 1
	{
		date -u '+# %Y-%m-%dT%H:%M:%SZ'
		for _l in "$@"; do echo "$_l"; done
		_i=1
		while [ "$_i" -le $((2*_n)) ]; do
			_L="$_bd/chrome-sr-$_p$_i.log"
			if [ -f "$_L" ]; then
				if ! grep -aq GUEST-EXEC "$_L"; then _st=noexec
				elif grep -aq '^RUN-INVALID' "$_L"; then _st=invalid:$(sed -n 's/^RUN-INVALID reason=//p' "$_L" | tail -1)
				else _st=valid; fi
				echo "run $_i $_st rc=$(sed -n 's/^WRAPPER_RC=//p' "$_L" | tail -1) dur=$(sed -n 's/^WRAPPER_DURATION=\([0-9]*\).*/\1/p' "$_L" | tail -1)"
			fi
			_i=$((_i+1))
		done
	} > "$_sd/$_p.txt" 2>/dev/null
}
