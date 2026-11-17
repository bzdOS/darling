#!/bin/sh
# semtw-run.sh - run semtw-probe-macho under launch-dynamic with a private XDG dir and print one verdict line:
#   SEMTW-PROOF panic=y|n iters=N first_panic_iter=K server_alive=y|n
# Usage: sh build-freebsd/probes/semtw-run.sh [wait-secs]   Env: DARLING_SRC_DIR, DARLING_OVERLAY, DARLING_BUILD_DIR.
# Stops only the darlingserver/mldr pids it started (by pid).
SRC="${DARLING_SRC_DIR:?}"; BD="${DARLING_BUILD_DIR:?}"; OD="${DARLING_OVERLAY:?}"
WAIT="${1:-240}"
PATH="/usr/local/bin:/usr/local/sbin:/usr/bin:/bin:/sbin:/usr/sbin"; export PATH
[ -f "${SRC}/tests/semtw-probe-macho" ] || { echo "FATAL: build it with probes/semtw-build.sh" >&2; exit 2; }
. "$(dirname "$0")/../series-guard.sh"; series_guard || exit 2
XDG=$(mktemp -d /tmp/semtw-xdg.XXXXXX); chmod 700 "$XDG"
LOG="${BD}/semtw-probe-$(date +%m%d-%H%M%S).log"
before=$(pgrep -x darlingserver | tr "\n" " ")
( sudo timeout -k 5 $((WAIT+30)) env XDG_RUNTIME_DIR="$XDG" "DARLING_SRC_DIR=${SRC}" "DARLING_OVERLAY=${OD}" "DARLING_BUILD_DIR=${BD}" \
    "DARLING_TEST_BINARY=semtw-probe-macho" DARLING_SMOKE_REFRESH=1 "${BD}/launch-dynamic" >"${LOG}" 2>&1 & )
i=0
while [ "$i" -lt "$WAIT" ]; do
    grep -qa "PROBE-OK\|PROBE-FAIL\|FATAL signal\|Terminating due to uncaught\|semaphore_block\|panic" "${LOG}" 2>/dev/null && break
    sleep 1; i=$((i+1))
done
sleep 2
alive=n
for p in $(pgrep -x darlingserver); do
    case " $before " in *" $p "*) ;; *) alive=y ;; esac
done
for p in $(pgrep -x darlingserver); do
    case " $before " in *" $p "*) ;; *) sudo kill -9 "$p" 2>/dev/null ;; esac
done
for g in $(pgrep -f "mldr-real/mldr .*semtw-probe-macho"); do sudo kill -9 "$g" 2>/dev/null; done
rmdir "$XDG" 2>/dev/null
echo "log=$LOG" >&2
pan=n; k=0
if grep -qa "semaphore_block\|sync_sema" "${LOG}"; then pan=y; k=$(grep -a "SEMTW progress" "${LOG}" | tail -1 | sed 's/.*n=\([0-9]*\).*/\1/'); k=$((k+1)); fi
n=$(grep -a "SEMTW progress" "${LOG}" | tail -1 | sed 's/.*n=\([0-9]*\).*/\1/'); n="${n:-0}"
grep -qa "^SEMTW iters=" "${LOG}" && n=$(grep -a "^SEMTW iters=" "${LOG}" | sed 's/.*iters=\([0-9]*\).*/\1/')
echo "SEMTW-PROOF panic=$pan iters=$n first_panic_iter=$k server_alive=$alive"
