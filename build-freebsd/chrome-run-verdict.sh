#!/bin/sh
# chrome-run-verdict.sh LOG - the verdict of one Chrome run, parsed from the log itself, not from rc.
# A wrapper rc=124 only says the harness timed out; the run's own life is in GUEST-EXIT.
# Prints one line: GUEST-EXIT=<status>/<t>s macho=<n> unimpl=<n> windows=<w>
#   GUEST-EXIT : the guest's own exit status and lifetime, from "[launch] GUEST-EXIT ... status=0x.. t=..s"
#   macho      : guest log lines of the AppKit stage ("chrome-macho[<pid>] ..."), 0 when the guest never got there
#   unimpl     : count of "unimplemented" lines (AppKit selectors the guest actually reached)
#   windows    : the series' sway window count from "SNAP windows=<n>", "unknown" when the snapshot never ran
# Exit 0 when the log is readable, 2 otherwise.
L="${1:?usage: chrome-run-verdict.sh LOG}"
[ -r "$L" ] || { echo "GUEST-EXIT=none/0s macho=0 unimpl=0 windows=unknown" >&2; exit 2; }
ge=$(grep -a 'GUEST-EXIT' "$L" | tail -1)
st=$(printf '%s\n' "$ge" | sed -n 's/.*status=\(0x[0-9a-f][0-9a-f]*\).*/\1/p')
t=$(printf '%s\n' "$ge" | sed -n 's/.* t=\([0-9][0-9]*\)s.*/\1/p')
# START_CHROME_VERDICT
# Contract: a guest that never printed its own "chrome-macho[pid]" line has macho=0 and unimpl=0, and a
# series whose snapshot never ran reports windows=unknown - a missing count is never silently turned into 0.
macho=$(grep -ac 'chrome-macho\[' "$L")
unimpl=$(grep -ac 'unimplemented' "$L")
w=$(sed -n 's/^SNAP windows=\([0-9][0-9]*\).*/\1/p' "$L" | tail -1)
[ -n "$w" ] || w=unknown
# END_CHROME_VERDICT
echo "GUEST-EXIT=${st:-none}/${t:-0}s macho=$macho unimpl=$unimpl windows=$w"
exit 0