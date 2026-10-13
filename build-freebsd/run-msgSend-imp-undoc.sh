#!/bin/sh
# run-msgSend-imp-undoc.sh — Control #162, step 40: run the
# objc-msgSend-imp-undoc-macho probe under the guest through launch-dynamic
# (same launcher and run rule as run-constdict-desc-probe.sh).
#
# Usage: sh build-freebsd/run-msgSend-imp-undoc.sh [wait-secs]
#
# The acceptance lines in the log are  undoc pre=... ret1=... post=... stable=...
# and  SENDUNDOC sel=<name> ret=<p> live=<yes|no> ; the terminal marker is
# SENDUNDOC DONE.
#
# Run rule (project card): the whole run is bounded by timeout -k 5 300;
# if mldr dies the server must outlive it by > 60 s, then the server is
# stopped BY PID (pgrep -x darlingserver, printed, killed).
#
# Environment:
#   DARLING_SRC_DIR, DARLING_OVERLAY, DARLING_BUILD_DIR
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
SRC="${DARLING_SRC_DIR:-${SCRIPT_DIR}}"
BD="${DARLING_BUILD_DIR:?set DARLING_BUILD_DIR to your build dir}"
OD="${DARLING_OVERLAY:?set DARLING_OVERLAY to your overlay dir}"
WAIT="${1:-45}"
TEST="objc-msgSend-imp-undoc-macho"

[ -f "${BD}/launch-dynamic" ] || { echo "FATAL: ${BD}/launch-dynamic missing" >&2; exit 1; }
[ -f "${SRC}/tests/${TEST}" ] || { echo "FATAL: test binary ${SRC}/tests/${TEST} missing" >&2; exit 1; }

PATH="/usr/local/bin:/usr/local/sbin:/usr/bin:/bin:/sbin:/usr/sbin"
export PATH

LOG="${BD}/msgSend-imp-undoc-$(date +%m%d-%H%M%S).log"
echo "LOG=$LOG"

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
    "${BD}/launch-dynamic" >"${LOG}" 2>&1 & )

i=0
while [ "$i" -lt "$WAIT" ]; do
    if grep -qa "SENDUNDOC DONE\|ret1=\|FATAL signal\|Terminating due to uncaught\|unrecognized selector" "${LOG}" 2>/dev/null; then
        break
    fi
    sleep 1
    i=$((i+1))
done
sleep 1

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
grep -a "^undoc " "${LOG}" || echo "(no undoc line in log)"
echo "--- markers ---"
grep -a "SENDUNDOC" "${LOG}" || echo "(no SENDUNDOC marker in log)"
echo "--- exceptions / signals ---"
grep -aE "FATAL signal|Terminating due to uncaught|doesNotRecognizeSelector|NSInvalidArgument|unrecognized selector|Symbol not found|image not found" "${LOG}" | head -10 || true
if grep -qa "FATAL signal" "${LOG}"; then
    echo "--- crash decoded ---"
    python3 "${SRC}/build-freebsd/decode-crash.py" "${LOG}" "${OD}" "${SRC}/tests" || true
fi
