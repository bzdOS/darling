#!/bin/sh
# run-fsops-probe.sh - run fsops-probe-macho (SQLite-style file system calls)
# under launch-dynamic. One-line verdict:  FSOPS-VERDICT pass|FAIL n=<checks> bad=<failed> [names].
# Usage: sh build-freebsd/run-fsops-probe.sh [wait-secs]
#   Env: DARLING_SRC_DIR, DARLING_OVERLAY, DARLING_BUILD_DIR; DSERVER_LOG_LEVEL=debug for a server log.
# The server is stopped by pid (never pkill).
SRC="${DARLING_SRC_DIR:?}"; BD="${DARLING_BUILD_DIR:?}"; OD="${DARLING_OVERLAY:?}"
WAIT="${1:-120}"
PATH="/usr/local/bin:/usr/local/sbin:/usr/bin:/bin:/sbin:/usr/sbin"; export PATH
[ -f "${SRC}/tests/fsops-probe-macho" ] || { echo "FATAL: build it with build-fsops-probe.sh" >&2; exit 2; }
if [ -z "$DARLING_LOCK_HELD" ] && pgrep -f "lockf.*darling-run.lock" >/dev/null 2>&1; then echo "FATAL: a series is running" >&2; exit 2; fi
LOG="${BD}/fsops-probe-$(date +%m%d-%H%M%S).log"
before=$(pgrep -x darlingserver | tr "\n" " ")
( sudo timeout -k 5 $((WAIT+30)) env "DARLING_SRC_DIR=${SRC}" "DARLING_OVERLAY=${OD}" "DARLING_BUILD_DIR=${BD}" \
    "DARLING_TEST_BINARY=fsops-probe-macho" DARLING_SMOKE_REFRESH=1 ${PROBE_ENV} ${DSERVER_LOG_LEVEL:+"DSERVER_LOG_LEVEL=${DSERVER_LOG_LEVEL}"} \
    "${BD}/launch-dynamic" >"${LOG}" 2>&1 & )
i=0
while [ "$i" -lt "$WAIT" ]; do
    grep -qa "FSOPS-VERDICT\|FATAL signal\|Terminating due to uncaught" "${LOG}" 2>/dev/null && break
    sleep 1; i=$((i+1))
done
sleep 1
for p in $(pgrep -x darlingserver); do
    case " $before " in *" $p "*) ;; *) sudo kill -9 "$p" 2>/dev/null ;; esac
done
for g in $(pgrep -f "mldr-real/mldr .*fsops-probe-macho"); do sudo kill -9 "$g" 2>/dev/null; done
if grep -qa "^FSOPS-VERDICT" "$LOG"; then grep -a "^FSOPS-T .* BAD\|^FSOPS-INFO" "$LOG" | head -12; grep -a "^FSOPS-VERDICT" "$LOG" | head -1; grep -qa "^FSOPS-VERDICT pass" "$LOG"; exit $?
else echo "FSOPS-VERDICT FAIL n=0 bad=? [no verdict: $(grep -aE "FATAL signal|Terminating due to uncaught" "$LOG" | head -1 | cut -c1-120)] log=$LOG"; exit 1; fi
