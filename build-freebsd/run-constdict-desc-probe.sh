#!/bin/sh
# run-constdict-desc-probe.sh — Control #160, step 38: run the
# objc-constdict-desc-macho marker probe under the guest through
# launch-dynamic (the handshake launcher; do NOT start darlingserver by
# hand — see build-freebsd/launch-chrome.sh's launch-model note).
#
# Usage:
#   sh build-freebsd/run-constdict-desc-probe.sh [wait-secs] [probe-arg]
#
# The optional probe-arg goes to the guest binary as argv[1]; "stamp" makes
# it install its own objc_setForwardHandler. The acceptance line in the log
# is  desc-imp=<value> foundation_790b0=<expected> match=<yes|no> .
#
# Run rule (project card): the whole run is bounded by timeout -k 5 300;
# after mldr dies the server must stay alive > 60 s (so mldr's death is the
# probe's fault, not a collapsed server), then the server is stopped BY PID
# (pgrep -x darlingserver, printed, killed) — pkill/killall are forbidden
# by the fleet rules.
#
# Environment (same as run-smoke.sh):
#   DARLING_SRC_DIR, DARLING_OVERLAY, DARLING_BUILD_DIR
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
SRC="${DARLING_SRC_DIR:-${SCRIPT_DIR}}"
BD="${DARLING_BUILD_DIR:?set DARLING_BUILD_DIR to your build dir}"
OD="${DARLING_OVERLAY:?set DARLING_OVERLAY to your overlay dir}"
WAIT="${1:-45}"
TEST="objc-constdict-desc-macho"
# DARLING_TEST_ARGS — launch-dynamic passes these to the TARGET binary as
# argv[1..] (see tests/launch-dynamic-smoke.c).
PROBE_ARG="${2:-}"

[ -f "${BD}/launch-dynamic" ] || { echo "FATAL: ${BD}/launch-dynamic missing" >&2; exit 1; }
[ -f "${SRC}/tests/${TEST}" ] || { echo "FATAL: test binary ${SRC}/tests/${TEST} missing" >&2; exit 1; }

PATH="/usr/local/bin:/usr/local/sbin:/usr/bin:/bin:/sbin:/usr/sbin"
export PATH DARLING_SRC_DIR="$SRC" DARLING_OVERLAY="$OD" DARLING_BUILD_DIR="$BD"

LOG="${BD}/constdict-desc-probe-$(date +%m%d-%H%M%S).log"
echo "LOG=$LOG"

# Stop a leftover server BY PID: print what the pid is, then kill it.
stop_server() {
    for p in $(pgrep -x darlingserver || true); do
        echo "stopping darlingserver pid=$p ($(ps -p "$p" -o comm= 2>/dev/null || echo '?'))"
        kill -9 "$p" 2>/dev/null || true
    done
    sleep 1
}
stop_server

( sudo timeout -k 5 300 env \
    "DARLING_SRC_DIR=${SRC}" \
    "DARLING_OVERLAY=${OD}" \
    "DARLING_BUILD_DIR=${BD}" \
    "DARLING_TEST_BINARY=${TEST}" \
    "DARLING_SMOKE_REFRESH=1" \
    ${PROBE_ARG:+"DARLING_TEST_ARGS=${PROBE_ARG}"} \
    "${BD}/launch-dynamic" >"${LOG}" 2>&1 & )

# Early exit: the probe prints its own terminal marker.
i=0
while [ "$i" -lt "$WAIT" ]; do
    if grep -qa "CONSTDICT DONE\|desc-imp=\|FATAL signal\|Terminating due to uncaught" "${LOG}" 2>/dev/null; then
        break
    fi
    sleep 1
    i=$((i+1))
done
sleep 1

# Liveness rule: if mldr died, the server must outlive it by > 60 s —
# that separates "the probe crashed the guest" from "the server collapsed".
if grep -qa "FATAL signal" "${LOG}" 2>/dev/null; then
    echo "--- mldr died; waiting 65 s to prove the server outlives it ---"
    sleep 65
    if pgrep -x darlingserver >/dev/null 2>&1; then
        echo "server liveness: darlingserver ALIVE 65+ s after mldr death (ok)"
    else
        echo "server liveness: darlingserver GONE after mldr death (server collapsed — run is ambiguous)"
    fi
fi

stop_server

echo "=== probe result ($(basename "$LOG"), waited ${i}s) ==="
echo "--- acceptance line ---"
grep -a "desc-imp=" "${LOG}" || echo "(no desc-imp line in log)"
echo "--- markers ---"
grep -a "CONSTDICT" "${LOG}" || echo "(no CONSTDICT marker in log)"
echo "--- exceptions / signals ---"
grep -aE "FATAL signal|Terminating due to uncaught|doesNotRecognizeSelector|NSInvalidArgument|Symbol not found|image not found" "${LOG}" | head -10 || true
echo "--- Foundation image base (for the IMP offset comparison) ---"
grep -aE "Foundation.*(mapped at|__TEXT at|base)" "${LOG}" | head -5 || true
if grep -qa "FATAL signal" "${LOG}"; then
    echo "--- crash decoded ---"
    python3 "${SRC}/build-freebsd/decode-crash.py" "${LOG}" "${OD}" "${SRC}/tests" || true
fi
