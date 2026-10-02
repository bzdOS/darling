#!/bin/sh
# wchan-kvm watcher (flat loop): at the variant-(a) park take kvm
# snapshots — ps -H (all threads + wchan of the parked process),
# procstat -kk per-pid (ESRCH expected at the park — documented),
# and the control: the same dumps on the darlingserver child.
# One pid scan per iteration, no nested spins. No gate: the run
# reaches the window.
BD=${DARLING_BUILD_DIR:-/tmp/darling-build}
SRC=${DARLING_SRC_DIR:-$(cd "$(dirname "$0")/.." && pwd)}
OVL=${DARLING_OVERLAY:-${SRC}/overlay}
LOG="${BD}/wl-body-wchan-watch.log"
DUMP="${BD}/wl-body-wchan.txt"
: > "${DUMP}"
REAL=""
DUMPED=0

cd /tmp/wlrun || exit 97
sudo -n env DARLING_SRC_DIR="${SRC}" \
	DARLING_OVERLAY="${OVL}" \
	DARLING_BUILD_DIR="${BD}" \
	DARLING_TEST_BINARY=guest-wl-session-roundtrip-macho \
	DARLING_STAGING_TREES='System/Library/Frameworks:System/Library/PrivateFrameworks:usr/lib' \
	LD_LIBRARY_PATH="${BD}/wl-debug-copy" LD_DEBUG=all \
	WL_SKIP_B=1 \
	XDG_RUNTIME_DIR=/tmp/wayland-gwr WAYLAND_DISPLAY=wayland-1 \
	timeout --foreground -k 5 200 "${BD}/launch-dynamic" \
	> "${LOG}" 2>&1 &

n=0
while [ "${n}" -lt 600 ]; do
	if [ -z "${REAL}" ]; then
		LAUNCH=$(pgrep -f 'launch-dynamic' | head -1)
		[ -n "${LAUNCH}" ] && \
			REAL=$(ps -axo pid,ppid | awk -v lp="${LAUNCH}" '$2==lp {print $1; exit}')
		[ -z "${REAL}" ] && REAL=$(pgrep -f 'mldr-real/mldr' | sort -n | tail -1)
	fi
	if [ -n "${REAL}" ] && [ "${DUMPED}" -eq 0 ] && \
	   grep -q 'DID-NOT-RETURN' "${LOG}" 2>/dev/null; then
		echo "=== park t0=$(date '+%H:%M:%S') pid=${REAL} ===" >> "${DUMP}"
		echo "--- all mldr-matching pids with lstart at t0 ---" >> "${DUMP}"
		for hp in $(pgrep -f 'mldr-real/mldr'); do ps -p "${hp}" -o pid,lstart,comm 2>/dev/null | tail -1; done >> "${DUMP}"
		echo "--- ps -H: all threads of the parked process ---" >> "${DUMP}"
		ps -H -axo pid,tid,state,wchan,comm 2>/dev/null | \
			awk -v p="${REAL}" 'NR==1 || $1==p' >> "${DUMP}"
		echo "--- procstat -kk per-pid (ESRCH expected at the park) ---" >> "${DUMP}"
		sudo -n procstat -kk "${REAL}" 2>&1 | head -4 >> "${DUMP}"
		echo "--- control: darlingserver child of the same session ---" >> "${DUMP}"
		DS=$(ps -axo pid,args | awk '/darlingserver/ && !/awk/ {print $1; exit}')
		echo "darlingserver pid=${DS}" >> "${DUMP}"
		ps -H -axo pid,tid,state,wchan,comm 2>/dev/null | \
			awk -v p="${DS}" 'NR==1 || $1==p' >> "${DUMP}"
		sleep 2
		echo "=== park t1=$(date '+%H:%M:%S') ===" >> "${DUMP}"
		ps -H -axo pid,tid,state,wchan,comm 2>/dev/null | \
			awk -v p="${REAL}" 'NR==1 || $1==p' >> "${DUMP}"
		echo "--- procstat -a -kk all-route rows for the parked pid ---" >> "${DUMP}"
		sudo -n procstat -a -kk 2>/dev/null | \
			awk -v p="${REAL}" '$1==p' | head -12 >> "${DUMP}"
		echo "SNAPSHOT DONE" >> "${DUMP}"
		DUMPED=1
	fi
	n=$((n + 1))
	sleep 1
done
echo "watcher done pid=${REAL} dumped=${DUMPED} i=${n}" >> "${DUMP}"
