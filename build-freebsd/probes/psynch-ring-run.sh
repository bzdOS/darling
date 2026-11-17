#!/bin/sh
# psynch-ring-run.sh - run psynch-ring-probe-macho under launch-dynamic with the server's psynch ring on, take one
# dsdump while the probe holds its contended rwlock, and print two verdict lines:
#   PSYNCH-RING pass|fail expected=N got=M   (dump holds >= N ring records for the probe's phase-1 lock address)
#   RWLABEL pass|fail                        (rdlock waiter is labelled R, wrlock waiter W in the kwq wait list)
# Needs the darlingserver built with darlingserver-psynch-ring.patch. The dump and log paths go to stderr.
# Usage: sh build-freebsd/probes/psynch-ring-run.sh [wait-secs]   Env: DARLING_SRC_DIR, DARLING_OVERLAY, DARLING_BUILD_DIR.
# Stops only the darlingserver/mldr pids it started (by pid).
SRC="${DARLING_SRC_DIR:?}"; BD="${DARLING_BUILD_DIR:?}"; OD="${DARLING_OVERLAY:?}"
WAIT="${1:-180}"
PATH="/usr/local/bin:/usr/local/sbin:/usr/bin:/bin:/sbin:/usr/sbin"; export PATH
[ -f "${SRC}/tests/psynch-ring-probe-macho" ] || { echo "FATAL: build it with probes/psynch-ring-build.sh" >&2; exit 2; }
if pgrep -f "lockf.*darling-run.lock" >/dev/null 2>&1; then echo "FATAL: a series is running" >&2; exit 2; fi
TS=$(date +%m%d-%H%M%S)
LOG="${BD}/psynch-ring-probe-${TS}.log"; DUMP="${BD}/psynch-ring-probe-${TS}.dump"
before=$(pgrep -x darlingserver | tr "\n" " ")
( sudo timeout -k 5 $((WAIT+30)) env DSERVER_PSYNCH_RING=1 DSERVER_PSYNCH_RING_SIZE=16384 "DARLING_SRC_DIR=${SRC}" "DARLING_OVERLAY=${OD}" "DARLING_BUILD_DIR=${BD}" \
    "DARLING_TEST_BINARY=psynch-ring-probe-macho" DARLING_SMOKE_REFRESH=1 "${BD}/launch-dynamic" >"${LOG}" 2>&1 & )
i=0
while [ "$i" -lt "$WAIT" ]; do
    grep -qa "^RWLABEL-WAIT\|PROBE-FAIL\|FATAL signal\|Terminating due to uncaught" "${LOG}" 2>/dev/null && break
    sleep 1; i=$((i+1))
done
sleep 1
for p in $(pgrep -x darlingserver); do
    case " $before " in *" $p "*) continue ;; esac
    sudo sh "${SRC}/build-freebsd/ds-microthreads-dump.sh" "$p" >"${DUMP}.$p" 2>&1
    if grep -qa "^psynch ring:" "${DUMP}.$p"; then mv "${DUMP}.$p" "${DUMP}"; break; fi
done
for p in $(pgrep -x darlingserver); do
    case " $before " in *" $p "*) ;; *) sudo kill -9 "$p" 2>/dev/null ;; esac
done
for g in $(pgrep -f "mldr-real/mldr .*psynch-ring-probe-macho"); do sudo kill -9 "$g" 2>/dev/null; done
echo "log=$LOG dump=$DUMP" >&2
rc=0
# PSYNCH-RING: count ring records of the phase-1 address
A1=$(grep -a "^PSRING-ADDR" "$LOG" | head -1 | sed 's/.*addr=\(0x[0-9a-f]*\).*/\1/')
N1=$(grep -a "^PSRING-ADDR" "$LOG" | head -1 | sed 's/.*n=\([0-9]*\).*/\1/')
if [ -z "$A1" ] || [ ! -f "$DUMP" ]; then
    echo "PSYNCH-RING fail expected=${N1:-0} got=0 (no probe address or no ring in the dump)"; rc=1
else
    M=$(grep -a "^ring seq=" "$DUMP" | grep -ac " uaddr=${A1} ")
    if [ "$M" -ge "$N1" ]; then echo "PSYNCH-RING pass expected=$N1 got=$M"; else echo "PSYNCH-RING fail expected=$N1 got=$M"; rc=1; fi
fi
# RWLABEL: the rdlock waiter must be R, the wrlock waiter W
L=$(grep -a "^RWLABEL-WAIT" "$LOG" | head -1)
A2=$(echo "$L" | sed 's/.*addr=\(0x[0-9a-f]*\).*/\1/'); RD=$(echo "$L" | sed 's/.*rd=\([0-9]*\).*/\1/'); WR=$(echo "$L" | sed 's/.*wr=\([0-9]*\).*/\1/')
if [ -z "$L" ] || [ ! -f "$DUMP" ]; then
    echo "RWLABEL fail (no wait line or no dump)"; rc=1
else
    # the kwq line for the address is followed by "   waiters: R:tid=a/nsB(..)@seq.. W:tid=.."
    W=$(grep -a -A3 "^kwq .* uaddr=${A2} " "$DUMP" | grep -a "waiters:" | head -1)
    case "$W" in
        *"R:tid="*"/ns${RD}("*) rok=y ;; *) rok=n ;;
    esac
    case "$W" in
        *"W:tid="*"/ns${WR}("*) wok=y ;; *) wok=n ;;
    esac
    case "$W" in
        *"W:tid="*"/ns${RD}("*|*"R:tid="*"/ns${WR}("*) wok=n ;;
    esac
    if [ "$rok" = y ] && [ "$wok" = y ]; then echo "RWLABEL pass"; else echo "RWLABEL fail rd=$RD wr=$WR waiters=[$W]"; rc=1; fi
fi
exit $rc
