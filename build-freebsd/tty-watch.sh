#!/bin/sh
# tty-window watcher: is the park a guest block in ttydev_write (a pty
# overflow / under-drain artifact of the observer) or something else?
#
# The probe's stdout runs on a pty (ptyrun.py). The watcher resolves the
# fresh mldr by parentage, snapshots it while ps-visible (baseline) and
# again in the window (ps-empty but kill -0 = EPERM), and at each stage
# runs the PRIVILEGED reads: sudo procstat -kk <pid> (kstack) and sudo
# fstat -p <pid> (open fds — the pty slave). It also reports the pty
# reader (ptyrun.py) and its state.
#
# MODE=pty (default) runs on a pty; MODE=file redirects stdout to a file
# (no pty) as the control.
BD=${DARLING_BUILD_DIR:-/tmp/darling-build}
SRC=${DARLING_SRC_DIR:-$(cd "$(dirname "$0")/.." && pwd)}
OVL=${DARLING_OVERLAY:-${SRC}/overlay}
MODE=${MODE:-pty}
LOG="${BD}/wl-body-tty-${MODE}.log"
DUMP="${BD}/wl-body-tty-${MODE}.txt"
PY="${BD}/ptyrun.py"
[ -f "${PY}" ] || PY="$(cd "$(dirname "$0")" && pwd)/ptyrun.py"
: > "${DUMP}"
: > "${LOG}"

cd /tmp/wlrun || exit 97

if [ "${MODE}" = "pty" ]; then
	sudo -n env DARLING_SRC_DIR="${SRC}" DARLING_OVERLAY="${OVL}" DARLING_BUILD_DIR="${BD}" \
		DARLING_TEST_BINARY=guest-wl-session-roundtrip-macho \
		DARLING_STAGING_TREES='System/Library/Frameworks:System/Library/PrivateFrameworks:usr/lib' \
		LD_LIBRARY_PATH="${BD}/wl-debug-copy" WL_SKIP_B=1 DARLING_TRAP_LOG=1 \
		XDG_RUNTIME_DIR=/tmp/wayland-gwr WAYLAND_DISPLAY=wayland-1 \
		python3 "${PY}" "${LOG}" env LD_DEBUG=all \
		timeout --foreground -k 5 200 "${BD}/launch-dynamic" &
else
	sudo -n env DARLING_SRC_DIR="${SRC}" DARLING_OVERLAY="${OVL}" DARLING_BUILD_DIR="${BD}" \
		DARLING_TEST_BINARY=guest-wl-session-roundtrip-macho \
		DARLING_STAGING_TREES='System/Library/Frameworks:System/Library/PrivateFrameworks:usr/lib' \
		LD_LIBRARY_PATH="${BD}/wl-debug-copy" WL_SKIP_B=1 DARLING_TRAP_LOG=1 \
		XDG_RUNTIME_DIR=/tmp/wayland-gwr WAYLAND_DISPLAY=wayland-1 \
		env LD_DEBUG=all \
		timeout --foreground -k 5 200 "${BD}/launch-dynamic" \
		> "${LOG}" 2>&1 &
fi

fresh_ds() {
	ps -axo pid,args 2>/dev/null | \
		awk '/darlingserver \/tmp\/darling-dynamic-smoke/ && !/awk/ {print $1}' | sort -n | tail -1
}

snap() {
	tag="$1"; G="$2"
	echo "=== ${tag} $(date '+%H:%M:%S') mode=${MODE} guest=${G:-none} ptyrun=$(pgrep -f 'ptyrun.py' | tr '\n' ',') logmark=[$(grep -aoE '\[step [0-9]+\]|DID-NOT-RETURN|LANE FINDING' "${LOG}" 2>/dev/null | tail -1)] ===" >> "${DUMP}"
	echo "--- ps -p guest (plain + sudo) ---" >> "${DUMP}"
	ps -p "${G}" -o pid,stat,wchan,lstart,comm 2>&1 | tail -2 >> "${DUMP}"
	sudo -n ps -p "${G}" -o pid,stat,wchan,lstart,comm 2>&1 | tail -2 >> "${DUMP}"
	echo "--- sudo procstat -kk guest (kstack; the privileged read) ---" >> "${DUMP}"
	sudo -n procstat -kk "${G}" 2>&1 | head -6 >> "${DUMP}"
	echo "--- sudo fstat -p guest (open fds; look for the pty slave) ---" >> "${DUMP}"
	sudo -n fstat -p "${G}" 2>&1 | head -14 >> "${DUMP}"
	echo "--- kill -0 guest ---" >> "${DUMP}"
	kill -0 "${G}" >> "${DUMP}" 2>&1; echo "  rc=$?" >> "${DUMP}"
	echo "--- pty reader: ptyrun.py state ---" >> "${DUMP}"
	for pp in $(pgrep -f 'ptyrun.py'); do
		ps -p "${pp}" -o pid,stat,wchan,comm 2>&1 | tail -1 >> "${DUMP}"
		sudo -n fstat -p "${pp}" 2>&1 | head -8 >> "${DUMP}"
	done
	echo "--- control darlingserver ---" >> "${DUMP}"
	DS=$(fresh_ds); [ -n "${DS}" ] && sudo -n procstat -kk "${DS}" 2>&1 | head -4 >> "${DUMP}"
}

G=""
base=0; win=0; post=0
n=0
while [ "${n}" -lt 1500 ]; do
	if [ -z "${G}" ]; then
		DS=$(fresh_ds)
		if [ -n "${DS}" ]; then
			P=$(ps -axo pid,ppid 2>/dev/null | awk -v d="${DS}" '$1==d {print $2; exit}')
			[ -n "${P}" ] && [ "${P}" != "1" ] && G="${P}"
		fi
	fi
	if [ -n "${G}" ]; then
		if [ "${base}" -eq 0 ]; then
			ST0=$(ps -p "${G}" -o stat= 2>/dev/null | tr -d ' ')
			if [ -n "${ST0}" ]; then snap "T1 baseline (ps-visible)" "${G}"; base=1; fi
		fi
		if [ "${base}" -eq 1 ] && [ "${win}" -eq 0 ]; then
			ST=$(ps -p "${G}" -o stat= 2>/dev/null | tr -d ' ')
			if [ -z "${ST}" ]; then
				K0=$(kill -0 "${G}" 2>&1)
				snap "T2 window (ps-empty; kill -0: ${K0:-alive})" "${G}"; win=1
			fi
		fi
		if [ "${win}" -eq 1 ]; then
			post=$((post + 1))
			if [ "${post}" -ge 20 ]; then snap "T3 confirm" "${G}"; break; fi
		fi
	fi
	if [ "${base}" -eq 0 ] && [ "${n}" -ge 400 ]; then
		echo "gave up: no baseline" >> "${DUMP}"; break
	fi
	n=$((n + 1))
	sleep 0.1
done
echo "TTY DONE mode=${MODE} n=${n} G=${G} base=${base} win=${win}" >> "${DUMP}"
