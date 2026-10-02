#!/bin/sh
# target-exit watcher: the last slice of the park lane — the target's
# exit status and Δt from the DID-NOT-RETURN marker to the target's exit.
#
# MODE=pty (default) runs the probe's stdout on a pty via ptyrun2.py
# (real-time markers; ptyrun2 propagates the child's status). MODE=file
# redirects to a file. The probe runs under a shell wrapper that records
# `$?` — launch-dynamic exec's mldr, which is the target, so that status
# IS the target's. The timing does not depend on resolving the guest: it
# polls the log for the marker and the wrapper for its exit.
BD=${DARLING_BUILD_DIR:-/tmp/darling-build}
SRC=${DARLING_SRC_DIR:-$(cd "$(dirname "$0")/.." && pwd)}
OVL=${DARLING_OVERLAY:-${SRC}/overlay}
MODE=${MODE:-pty}
LOG="${BD}/wl-body-targetexit.log"
DUMP="${BD}/wl-body-targetexit.txt"
STATUS="${BD}/wl-body-targetexit.status"
PY2="$(cd "$(dirname "$0")" && pwd)/ptyrun2.py"
: > "${DUMP}"; : > "${LOG}"; : > "${STATUS}"
cd /tmp/wlrun || exit 97

if [ "${MODE}" = "pty" ]; then
	(
		sudo -n env DARLING_SRC_DIR="${SRC}" DARLING_OVERLAY="${OVL}" DARLING_BUILD_DIR="${BD}" \
			DARLING_TEST_BINARY=guest-wl-session-roundtrip-macho \
			DARLING_STAGING_TREES='System/Library/Frameworks:System/Library/PrivateFrameworks:usr/lib' \
			LD_LIBRARY_PATH="${BD}/wl-debug-copy" WL_SKIP_B=1 DARLING_TRAP_LOG=1 \
			XDG_RUNTIME_DIR=/tmp/wayland-gwr WAYLAND_DISPLAY=wayland-1 \
			python3 "${PY2}" "${LOG}" env LD_DEBUG=all \
			timeout --foreground -k 5 200 "${BD}/launch-dynamic"
		echo "$?" > "${STATUS}"
	) &
else
	(
		sudo -n env DARLING_SRC_DIR="${SRC}" DARLING_OVERLAY="${OVL}" DARLING_BUILD_DIR="${BD}" \
			DARLING_TEST_BINARY=guest-wl-session-roundtrip-macho \
			DARLING_STAGING_TREES='System/Library/Frameworks:System/Library/PrivateFrameworks:usr/lib' \
			LD_LIBRARY_PATH="${BD}/wl-debug-copy" WL_SKIP_B=1 DARLING_TRAP_LOG=1 \
			XDG_RUNTIME_DIR=/tmp/wayland-gwr WAYLAND_DISPLAY=wayland-1 \
			env LD_DEBUG=all \
			timeout --foreground -k 5 200 "${BD}/launch-dynamic" \
			> "${LOG}" 2>&1
		echo "$?" > "${STATUS}"
	) &
fi
WRAP=$!

# resolve the guest (informational only)
fresh_ds() {
	ps -axo pid,args 2>/dev/null | \
		awk '/darlingserver \/tmp\/darling-dynamic-smoke/ && !/awk/ {print $1}' | sort -n | tail -1
}
G=""

DNRT=""; EXITT=""; n=0
while [ "${n}" -lt 6000 ]; do
	[ -z "${G}" ] && { DS=$(fresh_ds); [ -n "${DS}" ] && G=$(ps -axo pid,ppid 2>/dev/null | awk -v d="${DS}" '$1==d {print $2; exit}'); [ "${G}" = "1" ] && G=""; }
	if [ -z "${DNRT}" ] && grep -q 'DID-NOT-RETURN' "${LOG}" 2>/dev/null; then
		DNRT=$(date +%s.%N)
	fi
	if ! kill -0 "${WRAP}" 2>/dev/null; then
		EXITT=$(date +%s.%N)
		break
	fi
	n=$((n + 1))
	sleep 0.05
done

wait "${WRAP}" 2>/dev/null
EXIT=$(cat "${STATUS}" 2>/dev/null)
DT=""
[ -n "${DNRT}" ] && [ -n "${EXITT}" ] && DT=$(awk -v a="${DNRT}" -v b="${EXITT}" 'BEGIN{printf "%.2f", b-a}')
echo "=== target-exit mode=${MODE}: exit=${EXIT:-?} dt=${DT:-?}s (DNR->exit) guest=${G:-none} mark=[$(grep -aoE '\[step [0-9]+\]|DID-NOT-RETURN|LANE FINDING' "${LOG}" 2>/dev/null | tail -1)] ===" >> "${DUMP}"
if [ -n "${EXIT}" ] && [ "${EXIT}" -ge 128 ] 2>/dev/null; then
	echo "    interpretation: signal $((EXIT - 128))" >> "${DUMP}"
else
	echo "    interpretation: code ${EXIT:-?}" >> "${DUMP}"
fi
echo "TARGETEXIT DONE n=${n} G=${G}" >> "${DUMP}"
