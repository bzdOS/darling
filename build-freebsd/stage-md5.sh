#!/bin/sh
# stage-md5.sh - md5 of the overlay file and its staged copy (and optionally the built file).
# usage: stage-md5.sh [-b BUILT_FILE] RELPATH...   (RELPATH relative to the overlay root)
# Env: DARLING_OVERLAY; STAGED_OVERLAY (default /tmp/darling-local-overlay).
# Prints one line: MATCH or MISMATCH with the md5 values.
OD="${DARLING_OVERLAY:?}"; SO="${STAGED_OVERLAY:-/tmp/darling-local-overlay}"
B=""; [ "$1" = "-b" ] && { B=$2; shift 2; }
rc=0; out=""
for r in "$@"; do
	o=$(md5 -q "$OD/$r" 2>/dev/null); s=$(md5 -q "$SO/$r" 2>/dev/null); b=$o
	[ -n "$B" ] && b=$(md5 -q "$B" 2>/dev/null)
	[ -n "$o" ] && [ "$o" = "$s" ] && [ "$o" = "$b" ] || rc=1
	out="$out $r built=$b overlay=$o staged=$s;"
done
[ $rc = 0 ] && echo "MATCH$out" || echo "MISMATCH$out"
exit $rc
