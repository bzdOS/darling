#!/bin/sh
# tty-fast: poll the guest every 0.05s, log ps-state + kill -0 raw, and
# snap (privileged procstat -kk + fstat -p) on the first ps-empty. Goal:
# catch the "kill -0 = EPERM while ps-empty" moment and its kstack.
BD=${DARLING_BUILD_DIR:-/tmp/darling-build}
SRC=${DARLING_SRC_DIR:-$(cd "$(dirname "$0")/.." && pwd)}
OVL=${DARLING_OVERLAY:-${SRC}/overlay}
LOG="${BD}/wl-body-ttyfast.log"
DUMP="${BD}/wl-body-ttyfast.txt"
PY="${BD}/ptyrun.py"; [ -f "$PY" ] || PY="$(cd "$(dirname "$0")" && pwd)/ptyrun.py"
: > "${DUMP}"; : > "${LOG}"
cd /tmp/wlrun || exit 97
sudo -n env DARLING_SRC_DIR="${SRC}" DARLING_OVERLAY="${OVL}" DARLING_BUILD_DIR="${BD}" \
	DARLING_TEST_BINARY=guest-wl-session-roundtrip-macho \
	DARLING_STAGING_TREES='System/Library/Frameworks:System/Library/PrivateFrameworks:usr/lib' \
	LD_LIBRARY_PATH="${BD}/wl-debug-copy" WL_SKIP_B=1 DARLING_TRAP_LOG=1 \
	XDG_RUNTIME_DIR=/tmp/wayland-gwr WAYLAND_DISPLAY=wayland-1 \
	python3 "${PY}" "${LOG}" env LD_DEBUG=all \
	timeout --foreground -k 5 200 "${BD}/launch-dynamic" &

G=""; base=0; win=0
n=0
while [ "${n}" -lt 3000 ]; do
	if [ -z "${G}" ]; then
		DS=$(ps -axo pid,args 2>/dev/null | awk '/darlingserver \/tmp\/darling-dynamic-smoke/ && !/awk/ {print $1}' | sort -n | tail -1)
		if [ -n "${DS}" ]; then
			P=$(ps -axo pid,ppid 2>/dev/null | awk -v d="${DS}" '$1==d {print $2; exit}')
			[ -n "${P}" ] && [ "${P}" != "1" ] && G="${P}"
		fi
	fi
	if [ -n "${G}" ]; then
		ST=$(ps -p "${G}" -o stat= 2>/dev/null | tr -d ' ')
		K0=$(kill -0 "${G}" 2>&1)
		echo "t=$(date '+%H:%M:%S.%N' | cut -c1-12) G=${G} ps=${ST:-GONE} k0=[${K0:-ok}]" >> "${DUMP}"
		if [ "${base}" -eq 0 ] && [ -n "${ST}" ]; then base=1; fi
		if [ "${base}" -eq 1 ] && [ "${win}" -eq 0 ] && [ -z "${ST}" ]; then
			echo "=== WINDOW $(date '+%H:%M:%S') k0=[${K0}] ===" >> "${DUMP}"
			echo "--- sudo ps -p G ---" >> "${DUMP}"; sudo -n ps -p "${G}" -o pid,stat,wchan,lstart,comm 2>&1 | tail -2 >> "${DUMP}"
			echo "--- sudo procstat -kk G ---" >> "${DUMP}"; sudo -n procstat -kk "${G}" 2>&1 | head -6 >> "${DUMP}"
			echo "--- sudo fstat -p G ---" >> "${DUMP}"; sudo -n fstat -p "${G}" 2>&1 | head -12 >> "${DUMP}"
			echo "--- sudo procstat -kk all-route rows for G ---" >> "${DUMP}"; sudo -n procstat -a -kk 2>/dev/null | awk -v g="${G}" '$1==g' | head -6 >> "${DUMP}"
			win=1
		fi
		if [ "${win}" -eq 1 ]; then break; fi
	fi
	[ "${base}" -eq 0 ] && [ "${n}" -ge 500 ] && { echo "gave up no baseline" >> "${DUMP}"; break; }
	n=$((n + 1))
	sleep 0.05
done
echo "TTYFAST DONE n=${n} G=${G} base=${base} win=${win}" >> "${DUMP}"
