#!/bin/sh
# env-preflight.sh - environment check before a Chrome series / probe.
# Prints "PREFLIGHT ok" or "PREFLIGHT fail <reason>"; exit 1 on failure.
# Env: DARLING_SRC_DIR DARLING_OVERLAY DARLING_BUILD_DIR (needed for the probe)
#      EXPECT_DSERVER_MD5  if set, must equal md5 of $DARLING_BUILD_DIR/dserver/darlingserver
#      PREFLIGHT_MAX_LOAD  max 1-min load (default: hw.ncpu)
#      PREFLIGHT_PROBE=0   skip the N=1 probe run (default: run it)
#      PREFLIGHT_IGNORE    egrep pattern of command lines to ignore in the leftover check (e.g. a neighbour smoke loop)
#      PREFLIGHT_PREFIX    probe log prefix (default: preflight-<epoch>)
#      PROBE_TIMEOUT       probe timeout in s (darling.toml [env], default 240)
pf_fail() { echo "PREFLIGHT fail $*"; exit 1; }
: "${DARLING_BUILD_DIR:?}"
# 1. leftover servers/mldr from previous runs
L=$(ps -axww -o pid,command | awk '$2 ~ /(^|\/)(darlingserver|mldr)$/ && $0 ~ /\/tmp\/darling-/ {printf "%s ", $1}')
# PREFLIGHT_IGNORE never hides our own servers (/tmp/darling-run.* or the old fixed prefix)
[ -z "$PREFLIGHT_IGNORE" ] || L=$(ps -axww -o pid,command | awk '$2 ~ /(^|\/)(darlingserver|mldr)$/ && $0 ~ /\/tmp\/darling-/' | grep -Ev "$PREFLIGHT_IGNORE|/tmp/darling-run\.|/tmp/darling-dynamic-smoke-fresh" | awk '{printf "%s ", $1}')$(ps -axww -o pid,command | awk '$2 ~ /(^|\/)(darlingserver|mldr)$/ && ($0 ~ /\/tmp\/darling-run\./ || $0 ~ /\/tmp\/darling-dynamic-smoke-fresh/) {printf "%s ", $1}')
[ -z "$L" ] || pf_fail "live darlingserver/mldr under /tmp/darling-*: pids $L"
# 2. darlingserver md5
if [ -n "$EXPECT_DSERVER_MD5" ]; then
	D=$(md5 -q "$DARLING_BUILD_DIR/dserver/darlingserver" 2>/dev/null)
	[ "$D" = "$EXPECT_DSERVER_MD5" ] || pf_fail "darlingserver md5 $D != expected $EXPECT_DSERVER_MD5"
fi
# 3. load
NCPU=$(sysctl -n hw.ncpu); MAX=${PREFLIGHT_MAX_LOAD:-$NCPU}
LOAD=$(sysctl -n vm.loadavg | awk '{print $2}')
awk -v l="$LOAD" -v m="$MAX" 'BEGIN{exit !(l<m)}' || pf_fail "load $LOAD >= max $MAX"
# 4. quick probe
if [ "${PREFLIGHT_PROBE:-1}" != 0 ]; then
	: "${DARLING_SRC_DIR:?}" "${DARLING_OVERLAY:?}"
	PX=${PREFLIGHT_PREFIX:-preflight-$(date +%s)}
	R=$(DARLING_TEST_BINARY=sig-reentry-probe-macho sh "$DARLING_SRC_DIR/build-freebsd/sig-reentry-probe-run.sh" "$PX" 1 "${PROBE_TIMEOUT:-240}" 1500 100 0 2>&1)
	echo "$R" | grep -q 'SUMMARY.* ok=1 ' || pf_fail "probe not ok: $(echo "$R" | head -1)"
fi
echo "PREFLIGHT ok (load $LOAD<$MAX)"
exit 0
