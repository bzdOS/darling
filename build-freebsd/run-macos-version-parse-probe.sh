#!/bin/sh
# run-macos-version-parse-probe.sh - run macos-version-parse-probe-macho (OS-version sysctl as a browser parses it)
# under launch-dynamic. One-line verdict:  VERDICT pass|FAIL <probe line>.
# Usage: sh build-freebsd/run-macos-version-parse-probe.sh [wait-secs]
#   Env: DARLING_SRC_DIR, DARLING_OVERLAY, DARLING_BUILD_DIR; DSERVER_LOG_LEVEL=debug for a server log.
# The server is stopped by pid (never pkill).
SRC="${DARLING_SRC_DIR:?}"; BD="${DARLING_BUILD_DIR:?}"; OD="${DARLING_OVERLAY:?}"
WAIT="${1:-120}"
PATH="/usr/local/bin:/usr/local/sbin:/usr/bin:/bin:/sbin:/usr/sbin"; export PATH
[ -f "${SRC}/tests/macos-version-parse-probe-macho" ] || { echo "FATAL: build it with build-macos-version-parse-probe.sh" >&2; exit 2; }
if pgrep -f "lockf.*darling-run.lock" >/dev/null 2>&1; then echo "FATAL: a series is running" >&2; exit 2; fi
LOG="${BD}/macos-version-parse-probe-$(date +%m%d-%H%M%S).log"
before=$(pgrep -x darlingserver | tr "\n" " ")
( sudo timeout -k 5 $((WAIT+30)) env "DARLING_SRC_DIR=${SRC}" "DARLING_OVERLAY=${OD}" "DARLING_BUILD_DIR=${BD}" \
    "DARLING_TEST_BINARY=macos-version-parse-probe-macho" DARLING_SMOKE_REFRESH=1 ${PROBE_ENV} ${DSERVER_LOG_LEVEL:+"DSERVER_LOG_LEVEL=${DSERVER_LOG_LEVEL}"} \
    "${BD}/launch-dynamic" >"${LOG}" 2>&1 & )
i=0
while [ "$i" -lt "$WAIT" ]; do
    grep -qa "PROBE-OK\|PROBE-FAIL\|FATAL signal\|Terminating due to uncaught" "${LOG}" 2>/dev/null && break
    sleep 1; i=$((i+1))
done
sleep 1
for p in $(pgrep -x darlingserver); do
    case " $before " in *" $p "*) ;; *) sudo kill -9 "$p" 2>/dev/null ;; esac
done
for g in $(pgrep -f "mldr-real/mldr .*macos-version-parse-probe-macho"); do sudo kill -9 "$g" 2>/dev/null; done
if grep -qa "PROBE-OK" "$LOG"; then echo "VERDICT pass $(grep -a PROBE-OK "$LOG" | head -1) log=$LOG"
else echo "VERDICT FAIL $(grep -aE "PROBE-FAIL|FATAL signal|Terminating due to uncaught" "$LOG" | head -1 | cut -c1-200) log=$LOG"; exit 1; fi
