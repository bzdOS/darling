#!/bin/sh
# wchan-kvm watcher: capture the kernel view of the parked lane's process.
#
# The fresh mldr is resolved WITHOUT argv patterns for the target: the
# freshest darlingserver by ps args (highest pid = newest), then its
# PARENT (ppid) — the mldr that launch-dynamic exec'd, which runs the
# target in-process and forks the darlingserver as its child.
# Cross-checked against the target's own `start: pid=` line. Validation
# at snapshot time: `ps -p PID -o lstart,comm` must be non-empty.
#
# Snapshots: T1 the first moment the guest is ps-visible (pre-park
# baseline); T2 the first moment it leaves ps (the window); T3 after
# ~3s (stable). Each: ps -H thread rows + wchan, per-pid procstat -kk,
# and a kernel-existence check (kill -0 + sysctl kern.proc.pid) that
# tells "ps-invisible but alive" from "exited". The probe's stdout runs
# on a pty (ptyrun.py) so the step markers in the log are real-time.
BD=${DARLING_BUILD_DIR:-/tmp/darling-build}
SRC=${DARLING_SRC_DIR:-$(cd "$(dirname "$0")/.." && pwd)}
OVL=${DARLING_OVERLAY:-${SRC}/overlay}
LOG="${BD}/wl-body-wchan-watch.log"
DUMP="${BD}/wl-body-wchan.txt"
PY="${BD}/ptyrun.py"
[ -f "${PY}" ] || PY="$(cd "$(dirname "$0")" && pwd)/ptyrun.py"
: > "${DUMP}"
: > "${LOG}"
[ -f "${PY}" ] || { echo "missing ptyrun.py (pty runner)" >> "${DUMP}"; exit 1; }

cd /tmp/wlrun || exit 97

sudo -n env DARLING_SRC_DIR="${SRC}" \
	DARLING_OVERLAY="${OVL}" \
	DARLING_BUILD_DIR="${BD}" \
	DARLING_TEST_BINARY=guest-wl-session-roundtrip-macho \
	DARLING_STAGING_TREES='System/Library/Frameworks:System/Library/PrivateFrameworks:usr/lib' \
	LD_LIBRARY_PATH="${BD}/wl-debug-copy" \
	WL_SKIP_B=1 DARLING_TRAP_LOG=1 \
	XDG_RUNTIME_DIR=/tmp/wayland-gwr WAYLAND_DISPLAY=wayland-1 \
	python3 "${PY}" "${LOG}" \
	env LD_DEBUG=all \
	timeout --foreground -k 5 200 "${BD}/launch-dynamic" &

fresh_ds() {
	ps -axo pid,args 2>/dev/null | \
		awk '/darlingserver \/tmp\/darling-dynamic-smoke/ && !/awk/ {print $1}' | sort -n | tail -1
}

snap() {
	tag="$1"; G="$2"
	LAST=$(grep -aoE '\[step [0-9]+\]|DID-NOT-RETURN|LANE FINDING' "${LOG}" 2>/dev/null | tail -1)
	echo "=== ${tag} $(date '+%H:%M:%S') guest=${G:-none} logpid=$(grep -ao 'start: pid=[0-9]*' "${LOG}" 2>/dev/null | head -1 | cut -d= -f2) logmark=[${LAST}] ===" >> "${DUMP}"
	echo "--- all mldr-matching pids with lstart (the old argv route, for contrast) ---" >> "${DUMP}"
	for hp in $(ps -axo pid,comm 2>/dev/null | awk '$2=="mldr" {print $1}'); do
		ps -p "${hp}" -o pid,lstart,comm 2>/dev/null | tail -1 >> "${DUMP}"
	done
	if [ -n "${G}" ]; then
		echo "--- validation ps -p guest -o lstart,comm (must be non-empty) ---" >> "${DUMP}"
		ps -p "${G}" -o pid,lstart,comm 2>&1 >> "${DUMP}"
		echo "--- ps -H rows for the guest (all its threads + wchan) ---" >> "${DUMP}"
		ps -H -axo pid,tid,state,wchan,comm 2>/dev/null | awk -v g="${G}" 'NR==1 || $1==g' >> "${DUMP}"
		echo "--- procstat -kk per-guest ---" >> "${DUMP}"
		sudo -n procstat -kk "${G}" 2>&1 | head -8 >> "${DUMP}"
		echo "--- kernel existence: kill -0 ('Operation not permitted'=alive, 'No such process'=gone) ---" >> "${DUMP}"
		kill -0 "${G}" >> "${DUMP}" 2>&1; echo "  rc=$?" >> "${DUMP}"
		echo "--- procstat -kk all-route rows for the guest ---" >> "${DUMP}"
		sudo -n procstat -a -kk 2>/dev/null | awk -v g="${G}" '$1==g' | head -5 >> "${DUMP}"
	fi
	DS=$(fresh_ds)
	echo "--- control: darlingserver threads (pid ${DS:-none}) ---" >> "${DUMP}"
	[ -n "${DS}" ] && ps -H -axo pid,tid,state,wchan,comm 2>/dev/null | awk -v d="${DS}" 'NR==1 || $1==d' >> "${DUMP}"
}

G=""
base=0; win=0; post=0
n=0
while [ "${n}" -lt 3000 ]; do
	if [ -z "${G}" ]; then
		DS=$(fresh_ds)
		if [ -n "${DS}" ]; then
			P=$(ps -axo pid,ppid 2>/dev/null | awk -v d="${DS}" '$1==d {print $2; exit}')
			[ -n "${P}" ] && [ "${P}" != "1" ] && G="${P}"
		fi
	fi
	if [ -n "${G}" ]; then
		if [ "${base}" -eq 0 ]; then
			# first sight of the guest: dump the baseline right away
			snap "T1 pre-park baseline (first sight)" "${G}"
			ST0=$(ps -p "${G}" -o stat= 2>/dev/null | tr -d ' ')
			[ -n "${ST0}" ] && base=1
		fi
		if [ "${base}" -eq 1 ] && [ "${win}" -eq 0 ]; then
			ST=$(ps -p "${G}" -o stat= 2>/dev/null | tr -d ' ')
			if [ -z "${ST}" ]; then
				snap "T2 in the window (guest left ps)" "${G}"; win=1
			fi
		fi
		if [ "${win}" -eq 1 ]; then
			post=$((post + 1))
			if [ "${post}" -ge 30 ]; then
				snap "T3 confirm (stable)" "${G}"; break
			fi
		fi
	fi
	n=$((n + 1))
	sleep 0.1
done
echo "SNAPSHOT DONE n=${n} G=${G} base=${base} win=${win}" >> "${DUMP}"
