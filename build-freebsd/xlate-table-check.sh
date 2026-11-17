#!/bin/sh
# xlate-table-check.sh - compile and run the static consistency check of the host<->guest translation table.
# Output: XLATE-TABLE-CHECK pass|FAIL n=.. bad=.. [..]
D="${DARLING_SRC_DIR:-$(cd "$(dirname "$0")/.." && pwd)}"
T=$(mktemp -d /tmp/xlate-check.XXXXXX) || exit 2
clang -O1 -I"$D/src/startup/xlate" -o "$T/chk" "$D/build-freebsd/xlate-table-check.c" || { rm -rf "$T"; exit 2; }
"$T/chk"; rc=$?
rm -rf "$T"; exit $rc
