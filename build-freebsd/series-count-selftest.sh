#!/bin/sh
# series-count-selftest.sh - valid must never exceed N: prefix collisions and the batch summary log
# are not runs; fwdmiss counts the unique OBJC-FWD-MISS selectors of the runs only.
# Verdict: SERIES-COUNT-SELFTEST pass|FAIL ...
HERE=$(cd "$(dirname "$0")" && pwd)
. "$HERE/series-count.sh"
T=$(mktemp -d); trap 'rm -rf "$T"' EXIT
for f in fix3 fix31 fix32 fix33 fix34 fix35 fix36 fix37 fix38 fix39 fix310; do
	printf 'GUEST-EXEC\nWRAPPER_RC=139\nWRAPPER_DURATION=5\n' > "$T/chrome-sr-$f.log"
done
# two runs with misses (one selector repeated in both, one log line with a prefix), and a miss in the batch summary log
printf 'OBJC-FWD-MISS -[NSColor foo]\nOBJC-FWD-MISS +[NSImage bar:baz:]\n' >> "$T/chrome-sr-fix31.log"
printf '[x] OBJC-FWD-MISS -[NSColor foo]\nOBJC-FWD-MISS -[NSColor foo]\nOBJC-FWD-MISS -[NSView qux]\n' >> "$T/chrome-sr-fix32.log"
printf 'OBJC-FWD-MISS -[NSOnlyInSummary x]\n' >> "$T/chrome-sr-fix3.log"
old=0; for L in "$T"/chrome-sr-fix3*.log; do grep -aq GUEST-EXEC "$L" && old=$((old+1)); done
series_count "$T" fix3 10
if [ "$valid" -eq 10 ] && [ "$old" -eq 11 ] && [ "$fwdmiss" -eq 3 ] && ! printf '%s\n' "$fwdlist" | grep -q OnlyInSummary \
	&& printf '%s\n' "$fwdlist" | grep -qx '+\[NSImage bar:baz:\]'; then
	echo "SERIES-COUNT-SELFTEST pass N=10 valid=$valid fwdmiss=$fwdmiss (old glob counted $old)"
else
	echo "SERIES-COUNT-SELFTEST FAIL N=10 valid=$valid old=$old fwdmiss=$fwdmiss"; exit 1
fi
# window count: the textual count over get_tree JSON includes a floating node (modal panel) next to a tiled one (same expression as chrome-sr-batch.sh)
TREE_J="{\"nodes\":[{\"type\":\"workspace\",\"nodes\":[{\"type\":\"con\",\"shell\":\"xdg_shell\",\"nodes\":[],\"floating_nodes\":[]}],\"floating_nodes\":[{\"type\":\"floating_con\",\"shell\":\"xdg_shell\",\"nodes\":[],\"floating_nodes\":[]}]}]}"
fw=$(printf "%s\n" "$TREE_J" | tr "," "\n" | grep -c "\"shell\"")
if [ "$fw" -eq 2 ]; then echo "WINDOW-COUNT-SELFTEST pass tiled+floating=$fw"; else echo "WINDOW-COUNT-SELFTEST FAIL got=$fw want=2"; exit 1; fi
