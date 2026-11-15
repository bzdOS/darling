#!/bin/sh
# chrome-run-summary.sh LOG - one line: GUEST-EXEC second, WRAPPER_RC, duration/timeout, last fatal line.
L="${1:?}"
g=$(sed -n 's/.*GUEST-EXEC pid=[0-9]* t=\([0-9]*\)s.*/\1/p' "$L" | tail -1)
rc=$(sed -n 's/^WRAPPER_RC=//p' "$L" | tail -1)
d=$(sed -n 's/^WRAPPER_DURATION=//p' "$L" | tail -1)
f=$(grep -aE 'FATAL|abort_with_payload|corrupt data pointer|SIGTRAP-AUDIT-DELIVER|FATAL signal|MLDR_TRAP_AT hit' "$L" | tail -1 | cut -c1-160)
echo "$(basename "$L") GUEST-EXEC=${g:-none}s RC=${rc:-?} $d last=[${f:-none}]"
