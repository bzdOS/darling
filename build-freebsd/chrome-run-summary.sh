#!/bin/sh
# chrome-run-summary.sh LOG [LOG...] - one line per Chrome run: the run verdict first, the harness fields after.
# The verdict is GUEST-EXIT status/t, macho/unimpl and windows, parsed from the log itself
# (chrome-run-verdict.sh); WRAPPER_RC=124 is only the harness timeout and stays a secondary field.
# Several logs in one call print one line each (a series summary); one log stays one line. A missing
# log still gets its own line ("MISSING"), so a series summary always has one line per requested run.
[ -n "${1:-}" ] || { echo "usage: chrome-run-summary.sh LOG [LOG...]" >&2; exit 2; }
for L in "$@"; do
	if [ ! -r "$L" ]; then echo "$(basename "$L") MISSING"; continue; fi
	g=$(sed -n 's/.*GUEST-EXEC pid=[0-9]* t=\([0-9]*\)s.*/\1/p' "$L" | tail -1)
	rc=$(sed -n 's/^WRAPPER_RC=//p' "$L" | tail -1)
	d=$(sed -n 's/^WRAPPER_DURATION=//p' "$L" | tail -1)
	f=$(grep -aE 'FATAL|abort_with_payload|corrupt data pointer|SIGTRAP-AUDIT-DELIVER|FATAL signal|MLDR_TRAP_AT hit' "$L" | tail -1 | cut -c1-160)
	# START_CHROME_RUN_VERDICT
	# Contract: the run's own verdict comes from the log (chrome-run-verdict.sh), never from rc; a helper
	# that cannot read a log still yields one verdict line, so the summary never loses a run to a missing verdict.
	v=$(sh "$(dirname "$0")/chrome-run-verdict.sh" "$L")
	# END_CHROME_RUN_VERDICT
	echo "$(basename "$L") $v RC=${rc:-?} GUEST-EXEC=${g:-none}s $d last=[${f:-none}]"
done