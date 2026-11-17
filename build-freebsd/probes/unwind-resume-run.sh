#!/bin/sh
# unwind-resume-run.sh - one Chrome run with the unwind-resume probe loaded (DARLING_INSERT_EXTRA); prints the UNWPROBE lines of the log.
# usage: sh build-freebsd/probes/unwind-resume-run.sh PREFIX [N] [TIMEOUT]   Env: DARLING_SRC_DIR DARLING_OVERLAY DARLING_BUILD_DIR CHROME_APP
# chrome-sr-batch.sh takes the exclusive run lock itself (prints "LOCK busy" and exits 1 when another run holds it).
P=${1:?}; N=${2:-1}; T=${3:-120}
: "${DARLING_SRC_DIR:?}" "${DARLING_BUILD_DIR:?}" "${DARLING_OVERLAY:?}"
HOOK=$(sh "$DARLING_SRC_DIR/build-freebsd/probes/unwind-resume-hook.sh") || exit 1
CHROME_SR_COPY="$HOOK=/unwindresume/unwind-resume.dylib" \
CHROME_SR_EXTRA="DARLING_INSERT_EXTRA=/unwindresume/unwind-resume.dylib DARLING_UNWIND_PROBE=1" \
	nice -n 19 sh "$DARLING_SRC_DIR/build-freebsd/chrome-sr-batch.sh" "$P" "$N" "$T"
i=1
while [ $i -le "$N" ]; do
	L="$DARLING_BUILD_DIR/chrome-sr-$P$i.log"
	grep -a "UNWPROBE" "$L" 2>/dev/null | cut -c1-400 | head -150
	i=$((i+1))
done
