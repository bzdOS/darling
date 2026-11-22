#!/bin/sh
# psynch-ring-run.sh - run psynch-ring-probe-macho under launch-dynamic with the server's psynch ring on, take one
# dsdump while the probe holds its contended rwlock, and print two verdict lines:
#   PSYNCH-RING pass|fail expected=N got=M psynch=rec/drop park=rec/drop susp=rec/drop
#        (the psynch ring of the dump holds >= N records for the probe's phase-1 lock address; rec = recorded, drop =
#        overwritten, per ring class; a fail with psynch dropped=0 and got=0 is a defect of the server hook, not of the probe)
#   RWLABEL pass|fail                        (rdlock waiter is labelled R, wrlock waiter W in the kwq wait list)
# Needs the darlingserver built with darlingserver-psynch-ring.patch. The dump and log paths go to stderr.
# Usage: sh build-freebsd/probes/psynch-ring-run.sh [wait-secs]   Env: DARLING_SRC_DIR, DARLING_OVERLAY, DARLING_BUILD_DIR,
# PSYNCH_RING_CLASSES (server ring class mask, default 7; bit 0 psynch, 1 park/unpark, 2 suspend/resume).
# Stops only the darlingserver/mldr pids it started (by pid).
SRC="${DARLING_SRC_DIR:?}"; BD="${DARLING_BUILD_DIR:?}"; OD="${DARLING_OVERLAY:?}"
WAIT="${1:-180}"
PATH="/usr/local/bin:/usr/local/sbin:/usr/bin:/bin:/sbin:/usr/sbin"; export PATH
[ -f "${SRC}/tests/psynch-ring-probe-macho" ] || { echo "FATAL: build it with probes/psynch-ring-build.sh" >&2; exit 2; }
. "$(dirname "$0")/../series-guard.sh"; series_guard || exit 2
TS=$(date +%m%d-%H%M%S)
LOG="${BD}/psynch-ring-probe-${TS}.log"; DUMP="${BD}/psynch-ring-probe-${TS}.dump"
before=$(pgrep -x darlingserver | tr "\n" " ")
( sudo timeout -k 5 $((WAIT+30)) env DSERVER_PSYNCH_RING=1 DSERVER_PSYNCH_RING_SIZE=16384 "DSERVER_PSYNCH_RING_CLASSES=${PSYNCH_RING_CLASSES:-7}" "DARLING_SRC_DIR=${SRC}" "DARLING_OVERLAY=${OD}" "DARLING_BUILD_DIR=${BD}" \
    "DARLING_TEST_BINARY=psynch-ring-probe-macho" $([ "${STAGE_GATE_KEEP_STAGED:-}" = 1 ] && echo DARLING_KEEP_LOCAL=1 || echo DARLING_SMOKE_REFRESH=1) "${BD}/launch-dynamic" >"${LOG}" 2>&1 & )
i=0
while [ "$i" -lt "$WAIT" ]; do
    grep -qa "^RWLABEL-WAIT\|PROBE-FAIL\|FATAL signal\|Terminating due to uncaught" "${LOG}" 2>/dev/null && break
    sleep 1; i=$((i+1))
done
sleep 1
for p in $(pgrep -x darlingserver); do
    case " $before " in *" $p "*) continue ;; esac
    # the attach can fail on a loaded machine (lldb: "Cannot get process architecture"): up to 3 tries, 3 s apart
    for try in 1 2 3; do
        sudo sh "${SRC}/build-freebsd/ds-microthreads-dump.sh" "$p" >"${DUMP}.$p" 2>&1
        grep -qa "^psynch ring:" "${DUMP}.$p" && break
        grep -qa "attach failed" "${DUMP}.$p" && ATTACH_FAILED=1
        sleep 3
    done
    if grep -qa "^psynch ring:" "${DUMP}.$p"; then ATTACH_FAILED=; mv "${DUMP}.$p" "${DUMP}"; break; fi
done
for p in $(pgrep -x darlingserver); do
    case " $before " in *" $p "*) ;; *) sudo kill -9 "$p" 2>/dev/null ;; esac
done
for g in $(pgrep -f "mldr-real/mldr .*psynch-ring-probe-macho"); do sudo kill -9 "$g" 2>/dev/null; done
echo "log=$LOG dump=$DUMP" >&2
rc=0
# PSYNCH-RING: count ring records of the phase-1 address in the psynch class ring (fields parsed, not matched as text)
A1=$(grep -a "^PSRING-ADDR" "$LOG" | head -1 | sed 's/.*addr=\(0x[0-9a-f]*\).*/\1/')
N1=$(grep -a "^PSRING-ADDR" "$LOG" | head -1 | sed 's/.*n=\([0-9]*\).*/\1/')
if [ -n "$A1" ] && [ ! -f "$DUMP" ] && [ -n "$ATTACH_FAILED" ]; then
    echo "PSYNCH-RING fail dump-attach-failed expected=${N1:-0} (the debugger could not attach to the server in 3 tries; the probe itself ran)"; rc=1
elif [ -z "$A1" ] || [ ! -f "$DUMP" ]; then
    echo "PSYNCH-RING fail expected=${N1:-0} got=0 (no probe address or no ring in the dump)"; rc=1
else
    # one pass over the dump: per ring class recorded/dropped, and the psynch ring entries whose uaddr field equals A1
    R=$(awk -v a="$A1" '
        function kv(line, key,    n, i, f, t) { n = split(line, f, " "); for (i = 1; i <= n; i++) { t = f[i]; if (index(t, key "=") == 1) return substr(t, length(key) + 2) } return "" }
        /^ring class=/ { c = kv($0, "class"); rec[c] = kv($0, "recorded"); drp[c] = kv($0, "dropped"); next }
        /^ring seq=/ { if (kv($0, "cls") == "psynch" && kv($0, "uaddr") == a) m++ }
        END { printf "%d psynch=%s/%s park=%s/%s susp=%s/%s", m, (rec["psynch"] == "" ? "-" : rec["psynch"]), (drp["psynch"] == "" ? "-" : drp["psynch"]), (rec["park"] == "" ? "-" : rec["park"]), (drp["park"] == "" ? "-" : drp["park"]), (rec["susp"] == "" ? "-" : rec["susp"]), (drp["susp"] == "" ? "-" : drp["susp"]) }' "$DUMP")
    M=${R%% *}; CNT=${R#* }
    PD=$(echo "$CNT" | sed 's/.*psynch=[^/]*\/\([^ ]*\).*/\1/')
    if [ "$M" -ge "$N1" ]; then echo "PSYNCH-RING pass expected=$N1 got=$M $CNT"
    elif [ "$M" -eq 0 ] && [ "$PD" = 0 ]; then echo "PSYNCH-RING fail expected=$N1 got=$M $CNT HOOK-DEFECT (psynch ring dropped nothing and holds nothing for the address)"; rc=1
    else echo "PSYNCH-RING fail expected=$N1 got=$M $CNT"; rc=1; fi
fi
# RWLABEL: the rdlock waiter must be R, the wrlock waiter W (exactly these two entries, by field)
L=$(grep -a "^RWLABEL-WAIT" "$LOG" | head -1)
A2=$(echo "$L" | sed 's/.*addr=\(0x[0-9a-f]*\).*/\1/'); RD=$(echo "$L" | sed 's/.*rd=\([0-9]*\).*/\1/'); WR=$(echo "$L" | sed 's/.*wr=\([0-9]*\).*/\1/')
if [ -z "$L" ] || [ ! -f "$DUMP" ]; then
    echo "RWLABEL fail ${ATTACH_FAILED:+dump-attach-failed }(no wait line or no dump)"; rc=1
else
    # the kwq line whose uaddr field equals A2 is followed by "   waiters: R:tid=a/nsB(..)@seq.. W:tid=.." ; each waiter is one token
    W=$(awk -v a="$A2" -v rd="$RD" -v wr="$WR" '
        /^kwq / { cur = 0; n = split($0, f, " "); for (i = 1; i <= n; i++) if (f[i] == "uaddr=" a) cur = 1; next }
        cur && /^   waiters:/ {
            nr = 0; nw = 0; bad = 0; tot = 0; desc = ""
            for (i = 2; i <= NF; i++) {
                if (!match($i, /^[A-Z][0-9]*:tid=[0-9]+\/ns[0-9]+\(/)) continue
                tok = $i; lab = substr(tok, 1, index(tok, ":") - 1); ns = tok; sub(/^[^\/]*\/ns/, "", ns); sub(/\(.*/, "", ns)
                tot++; desc = desc " " lab "=" ns
                if (lab == "R" && ns == rd) nr++; else if (lab == "W" && ns == wr) nw++; else bad++
            }
            printf "%s rd=%d wr=%d other=%d waiters:%s\n", (nr == 1 && nw == 1 && bad == 0 && tot == 2) ? "ok" : "bad", nr, nw, bad, desc
            exit
        }' "$DUMP")
    case "$W" in
        ok*) echo "RWLABEL pass" ;;
        *) echo "RWLABEL fail rd=$RD wr=$WR got=[${W:-no waiters line for $A2}]"; rc=1 ;;
    esac
fi
exit $rc
