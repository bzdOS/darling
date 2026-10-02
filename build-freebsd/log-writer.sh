#!/bin/sh
# log-writer watcher: the probe log runs on to [step 10-12] after the
# resolved guest pid leaves ps. Who still holds and writes the log file?
# MODE=file (stdout to a regular file, no pty). On the guest's departure
# it records the departed (pid/ppid/lstart/comm), the log line count, and
# the log-fd holders (lsof + fstat by inode), then re-checks after 3s to
# see if the log grows and who holds it.
BD=${DARLING_BUILD_DIR:-/tmp/darling-build}
SRC=${DARLING_SRC_DIR:-$(cd "$(dirname "$0")/.." && pwd)}
OVL=${DARLING_OVERLAY:-${SRC}/overlay}
LOG="${BD}/wl-body-logwriter.log"
DUMP="${BD}/wl-body-logwriter.txt"
: > "${DUMP}"; : > "${LOG}"
cd /tmp/wlrun || exit 97

sudo -n env DARLING_SRC_DIR="${SRC}" DARLING_OVERLAY="${OVL}" DARLING_BUILD_DIR="${BD}" \
	DARLING_TEST_BINARY=guest-wl-session-roundtrip-macho \
	DARLING_STAGING_TREES='System/Library/Frameworks:System/Library/PrivateFrameworks:usr/lib' \
	LD_LIBRARY_PATH="${BD}/wl-debug-copy" WL_SKIP_B=1 DARLING_TRAP_LOG=1 \
	XDG_RUNTIME_DIR=/tmp/wayland-gwr WAYLAND_DISPLAY=wayland-1 \
	env LD_DEBUG=all \
	timeout --foreground -k 5 200 "${BD}/launch-dynamic" \
	> "${LOG}" 2>&1 &

holders() {
	INUM=$(ls -i "${LOG}" 2>/dev/null | awk '{print $1}')
	echo "  log inode=${INUM} lines=$(wc -l < "${LOG}" 2>/dev/null)" >> "${DUMP}"
	echo "  lsof:" >> "${DUMP}"
	sudo -n lsof "${LOG}" 2>&1 | sed 's/^/    /' >> "${DUMP}"
	echo "  fstat (inode ${INUM}):" >> "${DUMP}"
	sudo -n fstat 2>/dev/null | awk -v i="${INUM}" 'NR==1 || $6==i' | sed 's/^/    /' >> "${DUMP}"
	echo "  holder ps:" >> "${DUMP}"
	for p in $(sudo -n lsof -t "${LOG}" 2>/dev/null | sort -u); do
		ps -p "${p}" -o pid,ppid,stat,lstart,comm 2>&1 | tail -1 | sed 's/^/    /' >> "${DUMP}"
	done
}

fresh_ds() {
	ps -axo pid,args 2>/dev/null | \
		awk '/darlingserver \/tmp\/darling-dynamic-smoke/ && !/awk/ {print $1}' | sort -n | tail -1
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
		ST=$(ps -p "${G}" -o stat= 2>/dev/null | tr -d ' ')
		echo "poll t=$(date '+%H:%M:%S') G=${G} ps=${ST:-GONE} lines=$(wc -l < "${LOG}" 2>/dev/null) mark=[$(grep -aoE '\[step [0-9]+\]|DID-NOT-RETURN|LANE FINDING' "${LOG}" 2>/dev/null | tail -1)]" >> "${DUMP}"
		if [ "${base}" -eq 0 ] && [ -n "${ST}" ]; then base=1; fi
		if [ "${base}" -eq 1 ] && [ "${win}" -eq 0 ] && [ -z "${ST}" ]; then
			echo "=== DEPARTURE $(date '+%H:%M:%S') guest=${G} ===" >> "${DUMP}"
			echo "  departed ps: $(ps -p "${G}" -o pid,ppid,lstart,comm 2>&1 | tail -1)" >> "${DUMP}"
			echo "  darlingserver ps: $(ps -p "${DS}" -o pid,ppid,lstart,comm 2>&1 | tail -1)" >> "${DUMP}"
			echo "--- holders at departure ---" >> "${DUMP}"; holders
			sleep 3
			echo "--- holders 3s later ---" >> "${DUMP}"; holders
			echo "  logmark 3s later: [$(grep -aoE '\[step [0-9]+\]|DID-NOT-RETURN|LANE FINDING' "${LOG}" 2>/dev/null | tail -1)]" >> "${DUMP}"
			echo "--- full tree (mldr/darlingserver/timeout/launch-dynamic) ---" >> "${DUMP}"
			ps -axo pid,ppid,stat,lstart,comm,args 2>/dev/null | \
				awk '/mldr|darlingserver|launch-dynamic|guest-wl|timeout --foreground -k 5 200/ && !/awk/ && !/log-writer/' | sed 's/^/  /' >> "${DUMP}"
			win=1
			break
		fi
	fi
	[ "${base}" -eq 0 ] && [ "${n}" -ge 500 ] && { echo "gave up no baseline" >> "${DUMP}"; break; }
	n=$((n + 1))
	sleep 0.1
done
echo "LOGWRITER DONE n=${n} G=${G} base=${base} win=${win}" >> "${DUMP}"
