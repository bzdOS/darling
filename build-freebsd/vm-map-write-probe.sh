#!/bin/sh
# vm-map-write-probe.sh - run vm-map-write-probe-macho (vm_map write combinations)
# under launch-dynamic. One-line verdict:  VMMAP-WRITE-VERDICT pass|fail n=<checks> bad=[names].
# Usage: sh build-freebsd/vm-map-write-probe.sh [wait-secs]
#   Env: DARLING_SRC_DIR, DARLING_OVERLAY, DARLING_BUILD_DIR; DSERVER_LOG_LEVEL=debug for a server log.
# The server is stopped by pid (never pkill).
SRC="${DARLING_SRC_DIR:?}"; BD="${DARLING_BUILD_DIR:?}"; OD="${DARLING_OVERLAY:?}"
WAIT="${1:-120}"
PATH="/usr/local/bin:/usr/local/sbin:/usr/bin:/bin:/sbin:/usr/sbin"; export PATH
[ -f "${SRC}/tests/vm-map-write-probe-macho" ] || { echo "FATAL: build it with build-vm-map-write-probe.sh" >&2; exit 2; }
. "$(dirname "$0")/series-guard.sh"; series_guard || exit 2
LOG="${BD}/vm-map-write-probe-$(date +%m%d-%H%M%S).log"
before=$(pgrep -x darlingserver | tr "\n" " ")
( sudo timeout -k 5 $((WAIT+30)) env "DARLING_SRC_DIR=${SRC}" "DARLING_OVERLAY=${OD}" "DARLING_BUILD_DIR=${BD}" \
    "DARLING_TEST_BINARY=vm-map-write-probe-macho" $([ "${STAGE_GATE_KEEP_STAGED:-}" = 1 ] && echo DARLING_KEEP_LOCAL=1 || echo DARLING_SMOKE_REFRESH=1) ${PROBE_ENV} ${DSERVER_LOG_LEVEL:+"DSERVER_LOG_LEVEL=${DSERVER_LOG_LEVEL}"} \
    "${BD}/launch-dynamic" >"${LOG}" 2>&1 & )
i=0
while [ "$i" -lt "$WAIT" ]; do
    grep -qa "VMMAP-WRITE-VERDICT\|FATAL signal\|Terminating due to uncaught" "${LOG}" 2>/dev/null && break
    sleep 1; i=$((i+1))
done
sleep 1
for p in $(pgrep -x darlingserver); do
    case " $before " in *" $p "*) ;; *) sudo kill -9 "$p" 2>/dev/null ;; esac
done
for g in $(pgrep -f "mldr-real/mldr .*vm-map-write-probe-macho"); do sudo kill -9 "$g" 2>/dev/null; done
if grep -qa "^VMMAP-WRITE-VERDICT" "$LOG"; then grep -a "^VMMAP-T .* BAD\|^VMMAP-INFO" "$LOG" | head -12; grep -a "^VMMAP-WRITE-VERDICT" "$LOG" | head -1; grep -qa "^VMMAP-WRITE-VERDICT pass" "$LOG"; exit $?
else echo "VMMAP-WRITE-VERDICT fail n=0 bad=? [no verdict: $(grep -aE "FATAL signal|Terminating due to uncaught" "$LOG" | head -1 | cut -c1-120)] log=$LOG"; exit 1; fi
