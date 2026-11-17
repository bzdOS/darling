#!/bin/sh
# ptattach-run.sh - run ptattach-probe-macho under launch-dynamic. Needs a darlingserver built with the
# memory-access serialization patch to pass; on the old server it fails with first_kr=0x10000002.
# Prints one line: PTATTACH pass|fail calls=N fail=K   (log path on stderr).
# Usage: sh build-freebsd/probes/ptattach-run.sh [wait-secs]   Env: DARLING_SRC_DIR, DARLING_OVERLAY, DARLING_BUILD_DIR.
# Stops only the darlingserver/mldr pids it started (by pid).
SRC="${DARLING_SRC_DIR:?}"; BD="${DARLING_BUILD_DIR:?}"; OD="${DARLING_OVERLAY:?}"
WAIT="${1:-120}"
PATH="/usr/local/bin:/usr/local/sbin:/usr/bin:/bin:/sbin:/usr/sbin"; export PATH
[ -f "${SRC}/tests/ptattach-probe-macho" ] || { echo "FATAL: build it with probes/ptattach-build.sh" >&2; exit 2; }
if pgrep -f "lockf.*darling-run.lock" >/dev/null 2>&1; then echo "FATAL: a series is running" >&2; exit 2; fi
LOG="${BD}/ptattach-probe-$(date +%m%d-%H%M%S).log"
before=$(pgrep -x darlingserver | tr "\n" " ")
( sudo timeout -k 5 $((WAIT+30)) env "DARLING_SRC_DIR=${SRC}" "DARLING_OVERLAY=${OD}" "DARLING_BUILD_DIR=${BD}" \
    "DARLING_TEST_BINARY=ptattach-probe-macho" DARLING_SMOKE_REFRESH=1 "${BD}/launch-dynamic" >"${LOG}" 2>&1 & )
i=0
while [ "$i" -lt "$WAIT" ]; do
    grep -qa "PROBE-OK\|PROBE-FAIL\|FATAL signal\|Terminating due to uncaught" "${LOG}" 2>/dev/null && break
    sleep 1; i=$((i+1))
done
sleep 1
for p in $(pgrep -x darlingserver); do
    case " $before " in *" $p "*) ;; *) sudo kill -9 "$p" 2>/dev/null ;; esac
done
for g in $(pgrep -f "mldr-real/mldr .*ptattach-probe-macho"); do sudo kill -9 "$g" 2>/dev/null; done
echo "log=$LOG" >&2
L=$(grep -a "^PTATTACH" "$LOG" | head -1)
if [ -z "$L" ]; then echo "PTATTACH fail calls=0 fail=0 (no result line)"; exit 1; fi
echo "$L"
case "$L" in "PTATTACH pass"*) exit 0 ;; *) exit 1 ;; esac
