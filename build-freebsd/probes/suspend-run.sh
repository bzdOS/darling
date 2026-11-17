#!/bin/sh
# suspend-run.sh - run suspend-<kind>-probe-macho (kind = rwlock | mutex | cv, env SR_KIND) under launch-dynamic with the server's
# psynch ring on (own headless sway in a private XDG_RUNTIME_DIR, own darlingserver), take one dsdump after the probe's summary
# line, and print:
#   SUSPEND-<KIND> confirmed=y|n iters=N eintr=E retry=R retry_same_a0=S invariant_broken=I spurious=P hung=y|n pass=y|n
#        iters / invariant_broken / spurious / hung come from the probe (hung=y also when no summary line came: crash or timeout;
#        spurious is "-" for kinds that do not count it);
#        eintr         = ring records "<cont op> res=error a0=0x4" for the probe's lock address (the interrupted call);
#        retry         = of those, how many were followed by a new <wait op> record of the same tid on the same address
#                        (what libpthread must do on EINTR). For mutex the a0 is NOT compared (libpthread bumps a0 by +0x100 on the
#                        repeat); for rwlock a0 must equal the interrupted one;
#        retry_same_a0 = of those, how many repeats carried the same a0 as the interrupted call (reference only for mutex);
#        confirmed=y   when eintr>0 and retry=0 (interrupted calls are not repeated);
#        pass=y        rwlock, mutex: eintr>0 and retry==eintr and invariant_broken=0 and hung=n;
#                      cv (libpthread turns EINTR into a spurious wakeup, no repeat; POSIX allows it): eintr>0 and
#                      invariant_broken=0 and hung=n; spurious is informational only.
#   followed by "SUSPEND-RING-TAIL ..." lines (last ring records of the address around the first interrupted call).
# RUN-INVALID (no verdict) when the own sway is not alive at the end. Needs the darlingserver built with the psynch ring.
# Usage: SR_KIND=rwlock sh build-freebsd/probes/suspend-run.sh [wait-secs]   Env: DARLING_SRC_DIR, DARLING_OVERLAY, DARLING_BUILD_DIR,
# PSYNCH_RING_CLASSES (default 7), PSYNCH_RING_SIZE (default 262144), SR_SECS/SR_HANG_S/SR_SUSP_US/SR_GAP_US (probe),
# SUSPEND_LOCK_WAIT (secs to wait for the run lock via lockf, default 0 = fail at once). Takes the run lock itself unless
# DARLING_LOCK_HELD is set. Stops only the sway/darlingserver/mldr pids it started (by pid, ps line printed to stderr first).
SRC="${DARLING_SRC_DIR:?}"; BD="${DARLING_BUILD_DIR:?}"; OD="${DARLING_OVERLAY:?}"
KIND="${SR_KIND:?}"
case "$KIND" in
    rwlock) LABEL=SUSPEND-RWLOCK; WOP=rd_lock; COP=rd_cont ;;
    mutex)  LABEL=SUSPEND-MUTEX;  WOP=mutexwait; COP=mutex_cont ;;
    cv)     LABEL=SUSPEND-CV;     WOP=cvwait; COP=cv_cont ;;
    *) echo "FATAL: SR_KIND=$KIND" >&2; exit 2 ;;
esac
BIN="suspend-${KIND}-probe-macho"
WAIT="${1:-120}"
PATH="/usr/local/bin:/usr/local/sbin:/usr/bin:/bin:/sbin:/usr/sbin"; export PATH
if [ -z "$DARLING_LOCK_HELD" ]; then
    lockf -t "${SUSPEND_LOCK_WAIT:-0}" "${BD}/.darling-run.lock" env DARLING_LOCK_HELD=1 sh "$0" "$@"
    rc=$?
    [ "$rc" -eq 75 ] && echo "$LABEL skipped lock-busy"
    exit "$rc"
fi
DFP=$(df / | awk 'NR==2{gsub("%","",$5); print $5}'); [ "${DFP:-100}" -lt 84 ] || { echo "$LABEL fail disk-${DFP}pct"; exit 3; }
FREEMB=$(( ($(sysctl -n vm.stats.vm.v_free_count) + $(sysctl -n vm.stats.vm.v_inactive_count) + $(sysctl -n vm.stats.vm.v_laundry_count)) * 4096 / 1048576 ))
[ "$FREEMB" -ge 6144 ] || { echo "$LABEL fail mem-${FREEMB}MiB"; exit 3; }
SWP=$(swapinfo | awk '$1=="Total"{gsub("%","",$5); print $5}'); [ "${SWP:-100}" -lt 25 ] || { echo "$LABEL fail swap-${SWP}pct"; exit 3; }
[ -f "${SRC}/tests/${BIN}" ] || { echo "FATAL: build it with probes/suspend-build.sh" >&2; exit 2; }
TS=$(date +%m%d-%H%M%S)
LOG="${BD}/suspend-${KIND}-${TS}.log"; DUMP="${BD}/suspend-${KIND}-${TS}.dump"
XR=$(mktemp -d /tmp/suspend-${KIND}-xdg.XXXXXX) || { echo "FATAL: mktemp" >&2; exit 2; }
chmod 700 "$XR"
XDG_RUNTIME_DIR="$XR" WLR_BACKENDS=headless WLR_LIBINPUT_NO_DEVICES=1 WLR_RENDERER=pixman \
    nice -n 19 sway --unsupported-gpu >"$XR/sway.log" 2>&1 &
SWAYPID=$!
# sway names its own socket wayland-N (it ignores WAYLAND_DISPLAY with the headless backend): wait for any, then use that one
WD=""; i=0
while [ -z "$WD" ] && [ "$i" -lt 30 ]; do WD=$(ls "$XR" 2>/dev/null | grep -E "^wayland-[0-9]+$" | head -1); [ -n "$WD" ] || { sleep 1; i=$((i+1)); }; done
if [ -z "$WD" ] || [ ! -S "$XR/$WD" ]; then echo "RUN-INVALID sway-did-not-start"; ps -o pid,ppid,lstart,args -p "$SWAYPID" >&2; kill "$SWAYPID"; exit 4; fi
before=$(pgrep -x darlingserver | tr "\n" " ")
( sudo timeout -k 5 $((WAIT+60)) env DSERVER_PSYNCH_RING=1 "DSERVER_PSYNCH_RING_SIZE=${PSYNCH_RING_SIZE:-262144}" "DSERVER_PSYNCH_RING_CLASSES=${PSYNCH_RING_CLASSES:-7}" \
    "DARLING_SRC_DIR=${SRC}" "DARLING_OVERLAY=${OD}" "DARLING_BUILD_DIR=${BD}" "DARLING_TEST_BINARY=${BIN}" DARLING_SMOKE_REFRESH=1 \
    XDG_RUNTIME_DIR="$XR" WAYLAND_DISPLAY="$WD" DARLING_GUEST_EXPOSE="$XR" \
    ${SR_SECS:+SR_SECS=$SR_SECS} ${SR_HANG_S:+SR_HANG_S=$SR_HANG_S} ${SR_SUSP_US:+SR_SUSP_US=$SR_SUSP_US} ${SR_GAP_US:+SR_GAP_US=$SR_GAP_US} \
    "${BD}/launch-dynamic" >"${LOG}" 2>&1 & )
i=0
while [ "$i" -lt "$WAIT" ]; do
    grep -qa "^SUSPEND-SUMMARY\|PROBE-FAIL\|FATAL signal\|Terminating due to uncaught" "${LOG}" 2>/dev/null && break
    sleep 1; i=$((i+1))
done
sleep 1
for p in $(pgrep -x darlingserver); do
    case " $before " in *" $p "*) continue ;; esac
    sudo sh "${SRC}/build-freebsd/ds-microthreads-dump.sh" "$p" >"${DUMP}.$p" 2>&1
    if grep -qa "^psynch ring:" "${DUMP}.$p"; then mv "${DUMP}.$p" "${DUMP}"; break; fi
done
SWAYOK=n
kill -0 "$SWAYPID" 2>/dev/null && [ -S "$XR/$WD" ] && SWAYOK=y
for p in $(pgrep -x darlingserver); do
    case " $before " in *" $p "*) ;; *) ps -o pid,ppid,lstart,args -p "$p" >&2; sudo kill -9 "$p" ;; esac
done
for g in $(pgrep -f "mldr-real/mldr .*${BIN}"); do ps -o pid,ppid,lstart,args -p "$g" >&2; sudo kill -9 "$g"; done
ps -o pid,ppid,lstart,args -p "$SWAYPID" >&2; kill "$SWAYPID"
sleep 1; kill -0 "$SWAYPID" 2>/dev/null && kill -9 "$SWAYPID"
ls -ld "$XR" >&2; rm -rf "$XR"
echo "log=$LOG dump=$DUMP sway=$SWAYOK" >&2
if [ "$SWAYOK" != y ]; then echo "RUN-INVALID sway-down"; exit 4; fi
S=$(grep -a "^SUSPEND-SUMMARY" "$LOG" | head -1)
f() { echo "$S" | sed -n "s/.* $1=\([^ ]*\).*/\1/p"; }
ITERS=$(f iters); BROKEN=$(f invariant_broken); HUNG=$(f hung); A=$(f addr); SPUR=$(f spurious)
[ -n "$A" ] || A=$(grep -a "^SUSPEND-ADDR" "$LOG" | head -1 | sed 's/.*addr=\(0x[0-9a-f]*\).*/\1/')
case "$HUNG" in 0) HUNG=n ;; 1) HUNG=y ;; *) HUNG="y(no-summary)" ;; esac
if [ -z "$A" ] || [ ! -f "$DUMP" ]; then
    echo "$LABEL confirmed=- iters=${ITERS:--} eintr=- retry=- retry_same_a0=- invariant_broken=${BROKEN:--} spurious=${SPUR:--} hung=${HUNG} pass=n (no address or no ring in the dump)"; exit 1
fi
awk -v a="$A" -v iters="${ITERS:--}" -v broken="${BROKEN:--}" -v hung="$HUNG" -v label="$LABEL" -v kind="$KIND" -v spur="${SPUR:--}" -v wop="$WOP" -v cop="$COP" '
    function kv(line, key,    n, i, f, t) { n = split(line, f, " "); for (i = 1; i <= n; i++) { t = f[i]; if (index(t, key "=") == 1) return substr(t, length(key) + 2) } return "" }
    /^ring seq=/ && kv($0, "cls") == "psynch" && kv($0, "uaddr") == a {
        op = kv($0, "op"); res = kv($0, "res"); t = kv($0, "tid")
        n++; line[n] = $0
        if (op == wop) {
            if ((t in pend) && pend[t] != "") { retry_any++; if (kv($0, "a0") == pend[t]) same++; pend[t] = "" }
            last[t] = kv($0, "a0")
        } else if (op == cop && res == "error" && kv($0, "a0") == "0x4") {
            eintr++; pend[t] = last[t]; if (!first) first = n
        }
    }
    END {
        retry = (kind == "mutex") ? retry_any : same
        ok = (eintr > 0 && broken == "0" && hung == "n" && (kind == "cv" || retry == eintr)) ? "y" : "n"
        printf "%s confirmed=%s iters=%s eintr=%d retry=%d retry_same_a0=%d invariant_broken=%s spurious=%s hung=%s pass=%s\n", label, (eintr > 0 && retry == 0) ? "y" : "n", iters, eintr, retry, same, broken, spur, hung, ok
        if (!first) first = n
        s = first - 3; if (s < 1) s = 1
        for (i = s; i <= n && i <= s + 11; i++) print "SUSPEND-RING-TAIL " line[i]
    }' "$DUMP"
exit 0
