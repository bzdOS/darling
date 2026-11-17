#!/bin/sh
# disk-guard-selftest.sh - checks the disk guards of chrome-series.sh / chrome-sr-batch.sh without a Chrome run.
#  (a) SERIES_DISK_MAX=0 -> chrome-series.sh prints SERIES-ABORT, rc=1, and starts no sway/batch.
#  (b) SERIES_DISK_STOP=0 -> the batch stops at its first loop check (after=0, no run launched), BATCH-DONE
#      and the normal SERIES summary line are still printed.
# Needs the env of chrome-series.sh (DARLING_SRC_DIR DARLING_OVERLAY DARLING_BUILD_DIR CHROME_APP).
# Prints: DISK-GUARD-SELFTEST pass|fail abort=ok|.. stop=ok|..   exit 0 on pass.
H=$(cd "$(dirname "$0")" && pwd)
: "${DARLING_SRC_DIR:=$(cd "$H/.." && pwd)}"; export DARLING_SRC_DIR
: "${DARLING_BUILD_DIR:?}" "${DARLING_OVERLAY:?}"
: "${CHROME_APP:=/nonexistent}"; export CHROME_APP
export PREFLIGHT_PROBE=0
T=$(mktemp "${TMPDIR:-/tmp}/dgst.XXXXXX") || exit 1
ab=ok; st=ok
SERIES_DISK_MAX=0 sh "$H/chrome-series.sh" dgst-a 1 5 > "$T" 2>&1; rc=$?
grep -q "^SERIES-ABORT reason=disk use=[0-9]*%" "$T" || ab="no-abort-line"
[ "$rc" -eq 1 ] || ab="$ab rc=$rc"
grep -q "^BATCH-DONE\|^SERIES " "$T" && ab="$ab batch-ran"
SERIES_DISK_MAX=100 SERIES_DISK_STOP=0 sh "$H/chrome-series.sh" dgst-b 1 5 > "$T" 2>&1
grep -q "^SERIES-STOP reason=disk use=[0-9]*% after=0" "$T" || st="no-stop-line"
grep -q "^SERIES dgst-b N=1 valid=0" "$T" || st="$st no-series-line"
ls "$DARLING_BUILD_DIR"/chrome-sr-dgst-b[0-9]*.log >/dev/null 2>&1 && st="$st run-launched"
rm -f "$T"
if [ "$ab" = ok ] && [ "$st" = ok ]; then echo "DISK-GUARD-SELFTEST pass abort=$ab stop=$st"; exit 0; fi
echo "DISK-GUARD-SELFTEST fail abort=$ab stop=$st"; exit 1
