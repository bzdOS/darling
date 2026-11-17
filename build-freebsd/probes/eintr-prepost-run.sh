#!/bin/sh
# eintr-prepost-run.sh - run eintr-prepost-probe-macho under launch-dynamic with the server's psynch ring on, take one dsdump
# after the probe's summary line, and print:
#   EINTR-PREPOST-VERDICT iters=N lost=K early=X eintr_conts=E shape=y|n|- addr=0x..
#        lost  = iterations whose waiter did not wake after the unlock (interrupted by a signal first)
#        shape = for the first lost address (the last one when nothing is lost), the psynch ring holds, in order:
#                rd_cont/wr_cont res=error a0=4, then rw_unlock res=prepost wake=0[], and no later wake for that address
#   followed by "EINTR-RING-TAIL ..." lines: the last ring records of that address.
# Needs the darlingserver built with the psynch ring. The dump and log paths go to stderr.
# Usage: sh build-freebsd/probes/eintr-prepost-run.sh [wait-secs]   Env: DARLING_SRC_DIR, DARLING_OVERLAY, DARLING_BUILD_DIR,
# PSYNCH_RING_CLASSES (server ring class mask, default 1; bit 0 psynch, 1 park/unpark, 2 suspend/resume),
# PSYNCH_RING_SIZE (default 65536), EINTR_SERVER_MD5 (when set, the server binary must have this md5 after the lock is taken). Takes the run lock itself unless DARLING_LOCK_HELD is set (waits for it: EINTR_LOCK_WAIT secs,
# default 0 = fail at once). Stops only the darlingserver/mldr pids it started (by pid).
SRC="${DARLING_SRC_DIR:?}"; BD="${DARLING_BUILD_DIR:?}"; OD="${DARLING_OVERLAY:?}"
WAIT="${1:-240}"
PATH="/usr/local/bin:/usr/local/sbin:/usr/bin:/bin:/sbin:/usr/sbin"; export PATH
if [ -z "$DARLING_LOCK_HELD" ]; then
    lockf -t "${EINTR_LOCK_WAIT:-0}" "${BD}/.darling-run.lock" env DARLING_LOCK_HELD=1 sh "$0" "$@"
    rc=$?
    [ "$rc" -eq 75 ] && echo "EINTR-PREPOST-VERDICT skipped lock-busy"
    exit "$rc"
fi
if [ -n "$EINTR_SERVER_MD5" ] && [ "$(md5 -q "${BD}/dserver/darlingserver")" != "$EINTR_SERVER_MD5" ]; then echo "EINTR-PREPOST-VERDICT fail server-md5-mismatch" ; exit 3; fi
DFP=$(df / | awk 'NR==2{gsub("%","",$5); print $5}'); [ "${DFP:-100}" -lt 84 ] || { echo "EINTR-PREPOST-VERDICT fail disk-${DFP}pct"; exit 3; }
FREEMB=$(( ($(sysctl -n vm.stats.vm.v_free_count) + $(sysctl -n vm.stats.vm.v_inactive_count) + $(sysctl -n vm.stats.vm.v_laundry_count)) * 4096 / 1048576 ))
[ "$FREEMB" -ge 6144 ] || { echo "EINTR-PREPOST-VERDICT fail mem-${FREEMB}MiB"; exit 3; }
SWP=$(swapinfo | awk '$1=="Total"{gsub("%","",$5); print $5}'); [ "${SWP:-100}" -lt 25 ] || { echo "EINTR-PREPOST-VERDICT fail swap-${SWP}pct"; exit 3; }
[ -f "${SRC}/tests/eintr-prepost-probe-macho" ] || { echo "FATAL: build it with probes/eintr-prepost-build.sh" >&2; exit 2; }
TS=$(date +%m%d-%H%M%S)
LOG="${BD}/eintr-prepost-${TS}.log"; DUMP="${BD}/eintr-prepost-${TS}.dump"
before=$(pgrep -x darlingserver | tr "\n" " ")
( sudo timeout -k 5 $((WAIT+60)) env DSERVER_PSYNCH_RING=1 "DSERVER_PSYNCH_RING_SIZE=${PSYNCH_RING_SIZE:-65536}" "DSERVER_PSYNCH_RING_CLASSES=${PSYNCH_RING_CLASSES:-1}" "DARLING_SRC_DIR=${SRC}" "DARLING_OVERLAY=${OD}" "DARLING_BUILD_DIR=${BD}" \
    "DARLING_TEST_BINARY=eintr-prepost-probe-macho" DARLING_SMOKE_REFRESH=1 "${BD}/launch-dynamic" >"${LOG}" 2>&1 & )
i=0
while [ "$i" -lt "$WAIT" ]; do
    grep -qa "^EINTR-SUMMARY\|PROBE-FAIL\|FATAL signal\|Terminating due to uncaught" "${LOG}" 2>/dev/null && break
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
for g in $(pgrep -f "mldr-real/mldr .*eintr-prepost-probe-macho"); do sudo kill -9 "$g" 2>/dev/null; done
echo "log=$LOG dump=$DUMP" >&2
S=$(grep -a "^EINTR-SUMMARY" "$LOG" | head -1)
ITERS=$(echo "$S" | sed -n 's/.*iters=\([0-9]*\).*/\1/p'); LOST=$(echo "$S" | sed -n 's/.* lost=\([0-9]*\).*/\1/p')
FL=$(echo "$S" | sed -n 's/.*firstlost=\(0x[0-9a-f]\{2,\}\).*/\1/p'); EARLY=$(echo "$S" | sed -n 's/.* early=\([0-9]*\).*/\1/p'); LA=$(echo "$S" | sed -n 's/.*lastaddr=\(0x[0-9a-f]*\).*/\1/p')
A=${FL:-$LA}
if [ -z "$S" ] || [ -z "$A" ] || [ ! -f "$DUMP" ]; then
    echo "EINTR-PREPOST-VERDICT iters=${ITERS:-0} lost=${LOST:--} eintr_conts=- shape=- addr=${A:--} (no summary, no address or no ring in the dump)"; exit 1
fi
awk -v a="$A" -v iters="$ITERS" -v lost="$LOST" -v early="$EARLY" '
    function kv(line, key,    n, i, f, t) { n = split(line, f, " "); for (i = 1; i <= n; i++) { t = f[i]; if (index(t, key "=") == 1) return substr(t, length(key) + 2) } return "" }
    /^ring seq=/ && kv($0, "cls") == "psynch" {
        op = kv($0, "op"); res = kv($0, "res"); u = kv($0, "uaddr")
        if ((op == "rd_cont" || op == "wr_cont") && res == "error" && kv($0, "a0") == "0x4") ec++
        if (u != a) next
        n++; line[n] = $0
        if (stage == 0 && (op == "rd_cont" || op == "wr_cont") && res == "error" && kv($0, "a0") == "0x4") stage = 1
        else if (stage == 1 && op == "rw_unlock" && res == "prepost" && kv($0, "wake") == "0[]") stage = 2
        else if (stage == 2 && (res == "woken" || (op == "rw_unlock" && kv($0, "wake") != "0[]" ))) stage = 3
    }
    END {
        shape = (stage == 2) ? "y" : "n"
        printf "EINTR-PREPOST-VERDICT iters=%s lost=%s early=%s eintr_conts=%d shape=%s addr=%s\n", iters, lost, early, ec, shape, a
        s = n - 11; if (s < 1) s = 1
        for (i = s; i <= n; i++) print "EINTR-RING-TAIL " line[i]
    }' "$DUMP"
exit 0
