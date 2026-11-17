#!/bin/sh
# catx-kvc-run.sh - run catx-kvc-probe-macho under launch-dynamic. Needs a darlingserver built with the
# memory-access serialization patch to pass; on the old server it fails with first_kr=0x10000002.
# Prints the CATX-DETAIL lines and one CATX-KVC-PROOF line   (log path on stderr).
# Usage: sh build-freebsd/probes/catx-kvc-run.sh [wait-secs]   Env: DARLING_SRC_DIR, DARLING_OVERLAY, DARLING_BUILD_DIR.
# Stops only the darlingserver/mldr pids it started (by pid).
SRC="${DARLING_SRC_DIR:?}"; BD="${DARLING_BUILD_DIR:?}"; OD="${DARLING_OVERLAY:?}"
WAIT="${1:-120}"
export XDG_RUNTIME_DIR="$(mktemp -d)"; export XDG_CACHE_HOME="$XDG_RUNTIME_DIR"
PATH="/usr/local/bin:/usr/local/sbin:/usr/bin:/bin:/sbin:/usr/sbin"; export PATH
[ -f "${SRC}/tests/catx-kvc-probe-macho" ] || { echo "FATAL: build it with probes/catx-kvc-build.sh" >&2; exit 2; }
. "$(dirname "$0")/../series-guard.sh"; series_guard || exit 2
LOG="${BD}/catx-kvc-probe-$(date +%m%d-%H%M%S).log"
before=$(pgrep -x darlingserver | tr "\n" " ")
( sudo timeout -k 5 $((WAIT+30)) env "DARLING_SRC_DIR=${SRC}" "DARLING_OVERLAY=${OD}" "DARLING_BUILD_DIR=${BD}" \
    "DARLING_TEST_BINARY=catx-kvc-probe-macho" DARLING_SMOKE_REFRESH=1 DARLING_STAGING_TREES=usr/lib:System/Library/Frameworks:System/Library/PrivateFrameworks:private/etc "${BD}/launch-dynamic" >"${LOG}" 2>&1 & )
i=0
while [ "$i" -lt "$WAIT" ]; do
    grep -qa "CATX-KVC-PROOF\|PROBE-OK\|PROBE-FAIL\|FATAL signal\|Terminating due to uncaught" "${LOG}" 2>/dev/null && break
    sleep 1; i=$((i+1))
done
sleep 1
for p in $(pgrep -x darlingserver); do
    case " $before " in *" $p "*) ;; *) sudo kill -9 "$p" 2>/dev/null ;; esac
done
for g in $(pgrep -f "mldr-real/mldr .*catx-kvc-probe-macho"); do sudo kill -9 "$g" 2>/dev/null; done
echo "log=$LOG" >&2
grep -a "^CATX-DETAIL\|^kCAT\|^valueForKey\|^dlopen" "${LOG}" | head -40
L=$(grep -a "^CATX-KVC-PROOF" "$LOG" | head -1)
if [ -z "$L" ]; then echo "CATX-KVC-PROOF no result line"; exit 1; fi
echo "$L"
