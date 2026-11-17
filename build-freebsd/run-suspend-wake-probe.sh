#!/bin/sh
# run-suspend-wake-probe.sh - run tests/suspend-wake-probe-macho under launch-dynamic and print the verdict line
#   SUSPEND-WAKE-VERDICT pass|fail n=<round trips> stalls=<lost wakeups>   (rc 0 pass, 1 fail, 2 setup/lock)
# Env SWP_ITERS / SWP_TIMEOUT_S / SWP_MAX_STALLS go through PROBE_ENV (e.g. PROBE_ENV="SWP_ITERS=50000").
# Takes the shared run lock (.darling-run.lock, waits SWP_LOCK_WAIT s, default 300) so it never overlaps a Chrome run.
# Usage: sh build-freebsd/run-suspend-wake-probe.sh [wait-secs]
#   Env: DARLING_SRC_DIR, DARLING_OVERLAY, DARLING_BUILD_DIR. The server is stopped by pid (never pkill).
SRC="${DARLING_SRC_DIR:?}"; BD="${DARLING_BUILD_DIR:?}"; OD="${DARLING_OVERLAY:?}"
WAIT="${1:-300}"
PATH="/usr/local/bin:/usr/local/sbin:/usr/bin:/bin:/sbin:/usr/sbin"; export PATH
[ -f "${SRC}/tests/suspend-wake-probe-macho" ] || { echo "FATAL: build it with build-suspend-wake-probe.sh" >&2; exit 2; }
if [ -z "$DARLING_LOCK_HELD" ]; then
    DARLING_LOCK_HELD=1 lockf -t "${SWP_LOCK_WAIT:-300}" "$BD/.darling-run.lock" sh "$0" "$@"
    rc=$?; [ $rc -eq 75 ] && { echo "SUSPEND-WAKE-VERDICT fail n=0 stalls=0 (lock busy)"; exit 2; }
    exit $rc
fi
LOG="${BD}/suspend-wake-probe-$(date +%m%d-%H%M%S).log"
before=$(pgrep -x darlingserver | tr "\n" " ")
( sudo timeout -k 5 $((WAIT+30)) env "DARLING_SRC_DIR=${SRC}" "DARLING_OVERLAY=${OD}" "DARLING_BUILD_DIR=${BD}" \
    "DARLING_TEST_BINARY=suspend-wake-probe-macho" DARLING_SMOKE_REFRESH=1 ${PROBE_ENV} \
    "${BD}/launch-dynamic" >"${LOG}" 2>&1 & )
i=0
while [ "$i" -lt "$WAIT" ]; do
    grep -qa "SUSPEND-WAKE-VERDICT\|FATAL signal\|Terminating due to uncaught" "${LOG}" 2>/dev/null && break
    sleep 1; i=$((i+1))
done
sleep 1
for p in $(pgrep -x darlingserver); do
    case " $before " in *" $p "*) ;; *) sudo kill -9 "$p" 2>/dev/null ;; esac
done
for g in $(pgrep -f "mldr-real/mldr .*suspend-wake-probe-macho"); do sudo kill -9 "$g" 2>/dev/null; done
if grep -qa "^SUSPEND-WAKE-VERDICT" "$LOG"; then
    grep -a "^SUSPEND-WAKE-VERDICT" "$LOG" | head -1
    grep -a "^SWP-" "$LOG" | head -6 >&2
    echo "log=$LOG" >&2
    grep -aq "^SUSPEND-WAKE-VERDICT pass" "$LOG"; exit $?
else
    st=$(grep -ac "^SWP-STALL" "$LOG")
    echo "SUSPEND-WAKE-VERDICT fail n=? stalls=${st} (no verdict: probe died or hung)"
fi
echo "log=$LOG" >&2
exit 1
