#!/bin/sh
# run-fork-fail-probe.sh - run tests/fork-fail-probe-macho under launch-dynamic and print the verdict line
#   FORK-VERDICT pass|FAIL n=.. bad=.. [..]
# If the probe died inside a check, the verdict is composed from the log: FAIL with the check it died in.
# usr/bin is staged so the posix_spawn check finds /usr/bin/true in the guest overlay.
# Usage: sh build-freebsd/run-fork-fail-probe.sh [wait-secs]
#   Env: DARLING_SRC_DIR, DARLING_OVERLAY, DARLING_BUILD_DIR. The server is stopped by pid (never pkill).
SRC="${DARLING_SRC_DIR:?}"; BD="${DARLING_BUILD_DIR:?}"; OD="${DARLING_OVERLAY:?}"
WAIT="${1:-60}"
PATH="/usr/local/bin:/usr/local/sbin:/usr/bin:/bin:/sbin:/usr/sbin"; export PATH
[ -f "${SRC}/tests/fork-fail-probe-macho" ] || { echo "FATAL: build it with build-fork-fail-probe.sh" >&2; exit 2; }
. "$(dirname "$0")/series-guard.sh"; series_guard || exit 2
LOG="${BD}/fork-fail-probe-$(date +%m%d-%H%M%S).log"
before=$(pgrep -x darlingserver | tr "\n" " ")
# PROBE_KTRACE=<points> (letters only, e.g. cnf): trace the launcher and every guest process inside sudo
KT=; [ -n "${PROBE_KTRACE:-}" ] && KT="ktrace -i -t ${PROBE_KTRACE} -f ${LOG%.log}-ktrace.out"
( sudo timeout -k 5 $((WAIT+30)) env "DARLING_SRC_DIR=${SRC}" "DARLING_OVERLAY=${OD}" "DARLING_BUILD_DIR=${BD}" \
    "DARLING_TEST_BINARY=fork-fail-probe-macho" "DARLING_STAGING_TREES=usr/lib:System/Library/Frameworks:usr/bin" $([ "${STAGE_GATE_KEEP_STAGED:-}" = 1 ] && echo DARLING_KEEP_LOCAL=1 || echo DARLING_SMOKE_REFRESH=1) ${PROBE_ENV} \
    ${KT} "${BD}/launch-dynamic" >"${LOG}" 2>&1 & )
i=0
while [ "$i" -lt "$WAIT" ]; do
    grep -qa "FORK-VERDICT\|FATAL signal\|Terminating due to uncaught" "${LOG}" 2>/dev/null && break
    sleep 1; i=$((i+1))
done
sleep 1
for p in $(pgrep -x darlingserver); do
    case " $before " in *" $p "*) ;; *) sudo kill -9 "$p" 2>/dev/null ;; esac
done
for g in $(pgrep -f "mldr-real/mldr .*fork-fail-probe-macho"); do sudo kill -9 "$g" 2>/dev/null; done
if grep -qa "^FORK-VERDICT" "$LOG"; then
    grep -a "^FORK-VERDICT" "$LOG" | head -1
else
    last=$(grep -a "^FORK-T " "$LOG" | tail -1)
    done_n=$(grep -ac "^FORK-T .* \(ok\|bad\)" "$LOG")
    bad=$(grep -a "^FORK-T .* bad" "$LOG" | awk '{printf "%s,", $2}')
    case "$last" in "FORK-T begin "*) bad="${bad}died-or-hang-in-${last#FORK-T begin },";; *) bad="${bad}no-verdict,";; esac
    echo "FORK-VERDICT FAIL n=${done_n} bad=incomplete [${bad}]"
fi
echo "log=$LOG" >&2
