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
#      PREFLIGHT_DISK_DEV / _MSW_START / _QLEN_MAX / _MSW_STOP  disk gate (darling.toml [env]); PREFLIGHT_DISK=0 skips it
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
# 3b. disk: two iostat -x samples of the VM disk. Fail ("stop") when both exceed MSW_STOP ms per write;
# fail ("not start") when the last one has ms/w >= MSW_START or qlen > QLEN_MAX.
if [ "${PREFLIGHT_DISK:-1}" != 0 ]; then
	DDEV=${PREFLIGHT_DISK_DEV:-vtbd0}; DSTART=${PREFLIGHT_DISK_MSW_START:-50}; DQ=${PREFLIGHT_DISK_QLEN_MAX:-4}; DSTOP=${PREFLIGHT_DISK_MSW_STOP:-200}
	# first report of iostat is the since-boot average: take 3 reports and use the 2nd and 3rd
	DS=$(iostat -x -w 3 -c 3 "$DDEV" 2>/dev/null | awk -v d="$DDEV" '$1==d {n++; if (n>1) printf "%s %s ", $7, $10}')
	set -- $DS
	[ $# -eq 4 ] || pf_fail "disk $DDEV: no iostat sample"
	awk -v a="$1" -v b="$3" -v m="$DSTOP" 'BEGIN{exit !(a+0>m && b+0>m)}' && pf_fail "disk $DDEV ms/w $1,$3 > stop $DSTOP twice"
	awk -v w="$3" -v q="$4" -v m="$DSTART" -v x="$DQ" 'BEGIN{exit !(w+0>=m || q+0>x)}' && pf_fail "disk $DDEV ms/w=$3 qlen=$4 (start needs ms/w<$DSTART qlen<=$DQ)"
	DISKMSG=" disk $DDEV ms/w=$3 qlen=$4"
fi
# 4. quick probe
if [ "${PREFLIGHT_PROBE:-1}" != 0 ]; then
	: "${DARLING_SRC_DIR:?}" "${DARLING_OVERLAY:?}"
	PX=${PREFLIGHT_PREFIX:-preflight-$(date +%s)}
	R=$(DARLING_TEST_BINARY=sig-reentry-probe-macho sh "$DARLING_SRC_DIR/build-freebsd/sig-reentry-probe-run.sh" "$PX" 1 "${PROBE_TIMEOUT:-240}" 1500 100 0 2>&1)
	echo "$R" | grep -q 'SUMMARY.* ok=1 ' || pf_fail "probe not ok: $(echo "$R" | head -1)"
fi
echo "PREFLIGHT ok (load $LOAD<$MAX)${DISKMSG}"
exit 0
