#!/bin/sh
# run-coretext-probe.sh - run coretext-probe-macho and coretext-extras-probe-macho under launch-dynamic. One-line verdict:
#   CORETEXT-VERDICT pass|FAIL n=<checks> bad=[names]     (both images must pass; names are prefixed x: for the Extras image)
# Usage: sh build-freebsd/run-coretext-probe.sh [wait-secs]      Env: DARLING_SRC_DIR, DARLING_OVERLAY, DARLING_BUILD_DIR.
# The server is stopped by pid (never pkill).
SRC="${DARLING_SRC_DIR:?}"; BD="${DARLING_BUILD_DIR:?}"; OD="${DARLING_OVERLAY:?}"
WAIT="${1:-60}"
PATH="/usr/local/bin:/usr/local/sbin:/usr/bin:/bin:/sbin:/usr/sbin"; export PATH
for t in coretext-probe-macho coretext-extras-probe-macho; do
    [ -f "${SRC}/tests/$t" ] || { echo "FATAL: build it with build-coretext-probe.sh" >&2; exit 2; }
done
if [ -z "$DARLING_LOCK_HELD" ] && pgrep -f "lockf.*darling-run.lock" >/dev/null 2>&1; then echo "FATAL: a series is running" >&2; exit 2; fi
tot=0; allbad=""; rc=0
for t in coretext-probe-macho coretext-extras-probe-macho; do
    LOG="${BD}/${t}-$(date +%m%d-%H%M%S).log"
    before=$(pgrep -x darlingserver | tr "\n" " ")
    ( sudo timeout -k 5 $((WAIT+30)) env "DARLING_SRC_DIR=${SRC}" "DARLING_OVERLAY=${OD}" "DARLING_BUILD_DIR=${BD}" \
        "DARLING_TEST_BINARY=${t}" DARLING_SMOKE_REFRESH=1 ${PROBE_ENV} "${BD}/launch-dynamic" >"${LOG}" 2>&1 & )
    i=0
    while [ "$i" -lt "$WAIT" ]; do
        grep -qa "CORETEXT-VERDICT\|FATAL signal\|Terminating due to uncaught" "${LOG}" 2>/dev/null && break
        sleep 1; i=$((i+1))
    done
    sleep 1
    for p in $(pgrep -x darlingserver); do
        case " $before " in *" $p "*) ;; *) sudo kill -9 "$p" 2>/dev/null ;; esac
    done
    for g in $(pgrep -f "mldr-real/mldr .*${t}"); do sudo kill -9 "$g" 2>/dev/null; done
    pre=""; [ "$t" = coretext-extras-probe-macho ] && pre="x:"
    L=$(grep -a "^CORETEXT-VERDICT" "$LOG" | head -1)
    if [ -z "$L" ]; then
        last=$(grep -a "^CORETEXT-T " "$LOG" | tail -1 | awk '{print $2}')
        allbad="${allbad}${pre}died-after-${last:-start},"; rc=1; continue
    fi
    c=${L#*n=}; c=${c%% *}; tot=$((tot + c))
    b=${L#*bad=[}; b=${b%]*}
    if [ -n "$b" ]; then allbad="${allbad}$(echo "$b" | sed "s/\([^,]*\)/${pre}\1/g"),"; rc=1; fi
    echo "log=$LOG" >&2
done
allbad=${allbad%,}
if [ $rc = 0 ]; then echo "CORETEXT-VERDICT pass n=${tot} bad=[]"; else echo "CORETEXT-VERDICT FAIL n=${tot} bad=[${allbad}]"; fi
exit $rc
