#!/bin/sh
# chrome-series.sh - N-run Chrome series with an md5 gate and a one-line SERIES summary.
# usage: chrome-series.sh PREFIX N [TIMEOUT]    logs: $DARLING_BUILD_DIR/chrome-sr-<PREFIX>*.log
# Env: DARLING_SRC_DIR DARLING_OVERLAY DARLING_BUILD_DIR CHROME_APP (as for chrome-sr-batch.sh);
#      EXPECT_MLDR_MD5  if set and different from the installed mldr, exit 1 before running.
#      CHROME_EXTRA_ARGS  extra Chrome flags (exported to chrome-sr-batch.sh, inserted before the URL).
# Valid run = has GUEST-EXEC. survived20 = valid runs with WRAPPER_DURATION > 20. top = most frequent WRAPPER_RC.
# Last line: SERIES <prefix> N=<n> valid=<v> survived20=<k> top=rc<code>x<count> mldr=<md5>
# Exit 1 if the gate fails or valid=0.
export CHROME_EXTRA_ARGS; P=${1:?}; N=${2:?}; T=${3:-120}
: "${DARLING_SRC_DIR:?}" "${DARLING_BUILD_DIR:?}"
MLDR=$DARLING_BUILD_DIR/dserver/mldr-real/mldr
DS=$DARLING_BUILD_DIR/dserver/darlingserver
M=$(md5 -q "$MLDR" 2>/dev/null || md5sum "$MLDR" | cut -d' ' -f1)
D=$(md5 -q "$DS" 2>/dev/null || md5sum "$DS" | cut -d' ' -f1)
echo "mldr md5=$M"; echo "darlingserver md5=$D"
if [ -n "$EXPECT_MLDR_MD5" ] && [ "$EXPECT_MLDR_MD5" != "$M" ]; then
	echo "GATE FAIL: expected mldr md5 $EXPECT_MLDR_MD5, installed $M"; exit 1
fi
sh "$DARLING_SRC_DIR/build-freebsd/chrome-sr-batch.sh" "$P" "$N" "$T"
valid=0; surv=0; rcs=""
for L in "$DARLING_BUILD_DIR"/chrome-sr-"$P"*.log; do
	[ -f "$L" ] || continue
	grep -aq GUEST-EXEC "$L" || continue
	valid=$((valid+1))
	rc=$(sed -n 's/^WRAPPER_RC=//p' "$L" | tail -1)
	d=$(sed -n 's/^WRAPPER_DURATION=//p' "$L" | tail -1)
	d=${d%%[!0-9]*}
	[ "${d:-0}" -gt 20 ] && surv=$((surv+1))
	rcs="$rcs ${rc:-?}"
done
top="rc?x0"
[ -n "$rcs" ] && top=$(printf '%s\n' $rcs | sort | uniq -c | sort -rn | awk 'NR==1{print "rc" $2 "x" $1}')
echo "SERIES $P N=$N valid=$valid survived20=$surv top=$top mldr=$M args=\"$CHROME_EXTRA_ARGS\""
[ "$valid" -gt 0 ]
