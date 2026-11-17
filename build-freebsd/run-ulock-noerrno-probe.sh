#!/bin/sh
# run-ulock-noerrno-probe.sh - run tests/ulock-noerrno-probe-macho under launch-dynamic and print the verdict line
#   ULOCK-VERDICT pass|FAIL n=.. bad=.. [..]
# If the probe died inside a check, the verdict is composed from the log: FAIL with the check it died in.
# Usage: sh build-freebsd/run-ulock-noerrno-probe.sh [wait-secs]
#   Env: DARLING_SRC_DIR, DARLING_OVERLAY, DARLING_BUILD_DIR. The server is stopped by pid (never pkill).
SRC="${DARLING_SRC_DIR:?}"; BD="${DARLING_BUILD_DIR:?}"; OD="${DARLING_OVERLAY:?}"
WAIT="${1:-120}"
PATH="/usr/local/bin:/usr/local/sbin:/usr/bin:/bin:/sbin:/usr/sbin"; export PATH
[ -f "${SRC}/tests/ulock-noerrno-probe-macho" ] || { echo "FATAL: build it with build-ulock-noerrno-probe.sh" >&2; exit 2; }
. "$(dirname "$0")/series-guard.sh"; series_guard || exit 2
LOG="${BD}/ulock-noerrno-probe-$(date +%m%d-%H%M%S).log"
before=$(pgrep -x darlingserver | tr "\n" " ")
( sudo timeout -k 5 $((WAIT+30)) env "DARLING_SRC_DIR=${SRC}" "DARLING_OVERLAY=${OD}" "DARLING_BUILD_DIR=${BD}" \
    "DARLING_TEST_BINARY=ulock-noerrno-probe-macho" DARLING_SMOKE_REFRESH=1 ${PROBE_ENV} \
    "${BD}/launch-dynamic" >"${LOG}" 2>&1 & )
i=0
while [ "$i" -lt "$WAIT" ]; do
    grep -qa "ULOCK-VERDICT\|FATAL signal\|Terminating due to uncaught" "${LOG}" 2>/dev/null && break
    sleep 1; i=$((i+1))
done
sleep 1
for p in $(pgrep -x darlingserver); do
    case " $before " in *" $p "*) ;; *) sudo kill -9 "$p" 2>/dev/null ;; esac
done
for g in $(pgrep -f "mldr-real/mldr .*ulock-noerrno-probe-macho"); do sudo kill -9 "$g" 2>/dev/null; done
if grep -qa "^ULOCK-VERDICT" "$LOG"; then
    grep -a "^ULOCK-VERDICT" "$LOG" | head -1
else
    last=$(grep -a "^ULOCK-T " "$LOG" | tail -1)
    done_n=$(grep -ac "^ULOCK-T .* \(ok\|bad\)" "$LOG")
    bad=$(grep -a "^ULOCK-T .* bad" "$LOG" | awk '{printf "%s,", $2}')
    case "$last" in "ULOCK-T begin "*) bad="${bad}died-in-${last#ULOCK-T begin },";; *) bad="${bad}no-verdict,";; esac
    echo "ULOCK-VERDICT FAIL n=${done_n} bad=incomplete [${bad}]"
fi
echo "log=$LOG" >&2
