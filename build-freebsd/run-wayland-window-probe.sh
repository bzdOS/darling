#!/bin/sh
# run-wayland-window-probe.sh — one command for the whole window-probe run.
#
# The guest run is the one step in this milestone that needs root, so it gets
# exactly one shot at a root prompt. This wraps everything around it and
# refuses to spend that shot on something that could have been checked
# without root: sway not running, the seat still empty, the probe binary not
# built, the vendored backend not matching the one installed. Each of those
# fails in a way that looks like a backend bug from the inside.
#
# Usage:
#   sh build-freebsd/run-wayland-window-probe.sh
#   DRY_RUN=1 sh build-freebsd/run-wayland-window-probe.sh
#
# Env:
#   DARLING_BUILD_DIR   build/scratch dir (same as the other build-freebsd
#                       scripts). Also where launch-dynamic is looked for.
#   DARLING_OVERLAY     the overlay, i.e. the guest's DYLD_ROOT_PATH.
#   WAYLAND_DISPLAY     defaults to wayland-1.
#   XDG_RUNTIME_DIR     discovered if unset: a live sway ipc socket is found
#                       and the runtime dir taken from its path.
#   WAIT_SECS           how long to wait for the seat, and for the run.
#                       Defaults to 60.
#   DRY_RUN=1           do every check and print the exact command that would
#                       have been run, but do not run it. Needs no root.
#
# NO PRIVATE PATH LITERALS: every path here is either derived from this
# script's own location or comes from the environment, and the sudo line is
# assembled from variables, so nothing machine-specific can end up in it.

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
SRC="$(cd "${SCRIPT_DIR}/.." && pwd)"
BD="${DARLING_BUILD_DIR:?set DARLING_BUILD_DIR to your build dir}"
OD="${DARLING_OVERLAY:?set DARLING_OVERLAY to your overlay dir}"

export PATH="/usr/local/bin:/usr/local/sbin:/usr/bin:/bin:/sbin:/usr/sbin"

DRY_RUN="${DRY_RUN:-0}"
WAIT_SECS="${WAIT_SECS:-60}"
WAYLAND_DISPLAY="${WAYLAND_DISPLAY:-wayland-1}"
TEST_BIN="wayland-window-create-macho"
CONJURE="${BD}/conjure-wayland-input"
LOG="${BD}/wayland-window-probe.log"
CONJURE_LOG="${BD}/conjure-wayland-input.log"

# The backend dylib as committed, and the same file as installed in the
# overlay. They must be byte-identical: the vendored copy is the one the probe
# is checked against, and the overlay copy is the one the guest will actually
# dlopen. If they ever diverge, every conclusion drawn from the vendored copy
# is about a binary that is not running.
VENDORED="${SRC}/tests/vendor/wayland-backend/Wayland"
INSTALLED="${OD}/System/Library/Frameworks/AppKit.framework/Versions/C/Resources/Backends/Wayland.backend/Contents/MacOS/Wayland"
# The hash the vendored copy is committed with (tests/vendor/wayland-backend/
# README.md). Checked so a silent edit of the vendored artifact is noticed
# even if the overlay is edited to match.
EXPECT_SHA="a4797cddc449477fe65317548e58d6c18f050e838804596d470b4f9ae69eff2c"

CONJURE_PID=""

# --- reporting -----------------------------------------------------------

npass=0
step_no=0

ok() {
	npass=$((npass + 1))
	printf '  [PASS] %s\n' "$1"
}

note() {
	printf '         %s\n' "$1"
}

# die <step> <what> <where-to-look>
#
# The third argument is the point of this script: on failure, say which step
# died AND which line of the backend's own log tells us why, so the failure is
# diagnosable from the log alone without re-running anything.
die() {
	step_no="$1"
	what="$2"
	look="$3"
	printf '\nFAILED at step %s: %s\n' "${step_no}" "${what}" >&2
	if [ -n "${look}" ]; then
		printf 'look in %s for:\n' "${LOG}" >&2
		printf '%s\n' "${look}" | sed 's/^/    /' >&2
	fi
	if [ -f "${LOG}" ]; then
		printf '\nlast 20 lines of %s:\n' "${LOG}" >&2
		tail -20 "${LOG}" | sed 's/^/    /' >&2
	fi
	exit 1
}

cleanup() {
	if [ -n "${CONJURE_PID}" ] && kill -0 "${CONJURE_PID}" 2>/dev/null; then
		# The virtual input devices exist only while this process does; sway
		# goes back to an empty seat the moment it goes away. Leaving it
		# behind would make the NEXT run look like it worked.
		kill "${CONJURE_PID}" 2>/dev/null || true
		wait "${CONJURE_PID}" 2>/dev/null || true
		printf '\nstopped conjure-wayland-input (pid %s)\n' "${CONJURE_PID}"
	fi
}
trap cleanup EXIT INT TERM

printf '=== wayland window probe: preflight ===\n'
if [ "${DRY_RUN}" = "1" ]; then
	printf 'DRY_RUN=1 -- every check below runs, the sudo line is only printed\n'
fi

# --- 1. sway is alive and reachable --------------------------------------

step_no=1
if [ -z "${XDG_RUNTIME_DIR:-}" ]; then
	# Derive it: the runtime dir is the parent of a live sway ipc socket.
	for sock in /tmp/wayland-*/sway-ipc.*.sock /run/user/*/sway-ipc.*.sock; do
		[ -S "${sock}" ] || continue
		# sway-ipc.<uid>.<pid>.sock -- strip the directory, then ".sock",
		# and only then the last dot field, which is the pid. (Taking the
		# last dot field off the whole path yields "sock".)
		base="${sock##*/}"; base="${base%.sock}"; sway_pid="${base##*.}"
		case "${base}" in
			sway-ipc.*."${sway_pid}") ;;
			*) continue ;;
		esac
		if kill -0 "${sway_pid}" 2>/dev/null; then
			XDG_RUNTIME_DIR="$(dirname "${sock}")"
			SWAYSOCK="${sock}"
			break
		fi
	done
fi
if [ -z "${XDG_RUNTIME_DIR:-}" ]; then
	die "${step_no}" "no live sway ipc socket found; is sway running?" ""
fi
if [ -z "${SWAYSOCK:-}" ]; then
	SWAYSOCK=""
	for sock in "${XDG_RUNTIME_DIR}"/sway-ipc.*.sock; do
		[ -S "${sock}" ] || continue
		base="${sock##*/}"; base="${base%.sock}"; sway_pid="${base##*.}"
		case "${base}" in
			sway-ipc.*."${sway_pid}") ;;
			*) continue ;;
		esac
		if kill -0 "${sway_pid}" 2>/dev/null; then
			SWAYSOCK="${sock}"
			break
		fi
	done
fi

WAYLAND_SOCK="${XDG_RUNTIME_DIR}/${WAYLAND_DISPLAY}"
[ -S "${WAYLAND_SOCK}" ] || die "${step_no}" \
	"no wayland socket at ${WAYLAND_SOCK}" \
	"WaylandDisplay: wl_display_connect failed (WAYLAND_DISPLAY=%s)"
[ -n "${SWAYSOCK}" ] || die "${step_no}" "found a wayland socket but no sway ipc socket" ""
command -v swaymsg >/dev/null 2>&1 || die "${step_no}" "swaymsg not in PATH" ""
ok "sway is alive: ${WAYLAND_SOCK}, ipc ${SWAYSOCK}"

# --- 2. conjure-wayland-input is built ------------------------------------

step_no=2
if [ ! -x "${CONJURE}" ]; then
	note "building conjure-wayland-input (it makes the seat non-empty)"
	GEN="${BD}/wlr-protocols-gen"
	mkdir -p "${GEN}"
	# The output names are spelled out rather than looped: conjure-wayland-input.c
	# includes wlr-virtual-pointer-client-protocol.h and
	# virtual-keyboard-client-protocol.h, which are NOT the names wayland-scanner
	# would pick from the xml filenames.
	wayland-scanner client-header \
		"${SCRIPT_DIR}/wlr-protocols/wlr-virtual-pointer-unstable-v1.xml" \
		"${GEN}/wlr-virtual-pointer-client-protocol.h"
	wayland-scanner private-code \
		"${SCRIPT_DIR}/wlr-protocols/wlr-virtual-pointer-unstable-v1.xml" \
		"${GEN}/wlr-virtual-pointer-protocol.c"
	wayland-scanner client-header \
		"${SCRIPT_DIR}/wlr-protocols/virtual-keyboard-unstable-v1.xml" \
		"${GEN}/virtual-keyboard-client-protocol.h"
	wayland-scanner private-code \
		"${SCRIPT_DIR}/wlr-protocols/virtual-keyboard-unstable-v1.xml" \
		"${GEN}/virtual-keyboard-protocol.c"
	# shellcheck disable=SC2086
	clang -O1 -Wall -I"${GEN}" -o "${CONJURE}" \
		"${SCRIPT_DIR}/conjure-wayland-input.c" \
		"${GEN}/wlr-virtual-pointer-protocol.c" \
		"${GEN}/virtual-keyboard-protocol.c" \
		$(pkg-config --cflags --libs wayland-client)
fi
[ -x "${CONJURE}" ] || die "${step_no}" "conjure-wayland-input is not built" ""
ok "conjure-wayland-input: ${CONJURE}"

# --- 3. the probe binary is built ----------------------------------------

step_no=3
if [ ! -f "${SRC}/tests/${TEST_BIN}" ]; then
	note "building the probe (${TEST_BIN})"
	DARLING_SRC_DIR="${SRC}" DARLING_BUILD_DIR="${BD}" DARLING_OVERLAY="${OD}" \
		sh "${SCRIPT_DIR}/build-wayland-window-test.sh" >"${BD}/build-probe.log" 2>&1 \
		|| die "${step_no}" "the probe failed to build; see ${BD}/build-probe.log" ""
fi
[ -f "${SRC}/tests/${TEST_BIN}" ] || die "${step_no}" \
	"${TEST_BIN} missing after build" ""
ok "probe binary: ${SRC}/tests/${TEST_BIN}"

# --- 4. the vendored backend is the one that will actually run ------------

step_no=4
[ -f "${VENDORED}" ] || die "${step_no}" "vendored backend missing: ${VENDORED}" ""
[ -f "${INSTALLED}" ] || die "${step_no}" \
	"backend not installed in the overlay: ${INSTALLED}" ""
sha_installed="$(sha256 -q "${INSTALLED}" 2>/dev/null || shasum -a 256 "${INSTALLED}" | cut -d' ' -f1)"
sha_vendored="$(sha256 -q "${VENDORED}" 2>/dev/null || shasum -a 256 "${VENDORED}" | cut -d' ' -f1)"
[ "${sha_installed}" = "${sha_vendored}" ] || die "${step_no}" \
	"overlay and vendored backend differ (${sha_installed} vs ${sha_vendored})" ""
[ "${sha_vendored}" = "${EXPECT_SHA}" ] || die "${step_no}" \
	"vendored backend is not the committed artifact (${sha_vendored})" ""
ok "backend sha256 $(printf '%s' "${sha_vendored}" | cut -c1-8)... matches vendored, overlay and the committed hash"

printf '\n=== preflight: %d check(s) passed ===\n' "${npass}"

# --- 5. raise the seat, then hold it up for the duration of the run -------

step_no=5
printf '\n=== seat ===\n'
printf 'before: %s\n' "$(SWAYSOCK="${SWAYSOCK}" swaymsg -t get_seats \
	| tr -d ' \n' | sed 's/.*"capabilities":\([0-9]*\).*/capabilities=\1/')"

XDG_RUNTIME_DIR="${XDG_RUNTIME_DIR}" WAYLAND_DISPLAY="${WAYLAND_DISPLAY}" \
	"${CONJURE}" "$((WAIT_SECS * 3))" >"${CONJURE_LOG}" 2>&1 &
CONJURE_PID=$!
note "conjure-wayland-input started, pid ${CONJURE_PID}, log ${CONJURE_LOG}"

seat_caps=""
i=0
while [ "${i}" -lt "${WAIT_SECS}" ]; do
	seat_caps="$(SWAYSOCK="${SWAYSOCK}" swaymsg -t get_seats 2>/dev/null \
		| tr -d ' \n' | sed 's/.*"capabilities":\([0-9]*\).*/\1/')"
	# 3 = POINTER(1) | KEYBOARD(2). The shm format negotiation declines to
	# guess when the seat advertises nothing, so 0 is not good enough.
	if [ "${seat_caps}" = "3" ]; then
		break
	fi
	sleep 1
	i=$((i + 1))
done
[ "${seat_caps}" = "3" ] || die "${step_no}" \
	"seat never reached capabilities=3 (stuck at '${seat_caps:-?}')" \
	"WaylandDisplay: no wl_output mode known yet, reporting a placeholder" \
	# conjure is stopped by the trap
	ok "seat capabilities=3 (keyboard+pointer)"

# --- 6. the run itself ----------------------------------------------------

step_no=6
RUN_CMD="env DARLING_SRC_DIR=${SRC} DARLING_OVERLAY=${OD} DARLING_BUILD_DIR=${BD}"
RUN_CMD="${RUN_CMD} DARLING_TEST_BINARY=${TEST_BIN}"
RUN_CMD="${RUN_CMD} WAYLAND_DISPLAY=${WAYLAND_DISPLAY} XDG_RUNTIME_DIR=${XDG_RUNTIME_DIR}"
RUN_CMD="${RUN_CMD} ${BD}/launch-dynamic"

printf '\n=== run ===\n'
printf 'log: %s\n' "${LOG}"
if [ "${DRY_RUN}" = "1" ]; then
	printf 'DRY_RUN=1 -- NOT running. The command that would be run is exactly:\n\n'
	printf '    sudo %s\n\n' "${RUN_CMD}"
	printf 'The virtual input devices will exist only while this script runs.\n'
	exit 0
fi

printf 'running: sudo %s\n' "${RUN_CMD}"
# shellcheck disable=SC2086
sudo ${RUN_CMD} >"${LOG}" 2>&1 || true

# launch-dynamic exits 0 even when the guest dies, so the log decides, not $?.
grep -q 'RESULT: window created' "${LOG}" 2>/dev/null || die "${step_no}" \
	"the run did not reach the shm buffer (see below)" \
	"WaylandWindow: shm allocation failed for %dx%d buffer: %s
    WaylandWindow: mmap failed for %dx%d buffer: %s
    WaylandWindow: wl_shm_pool_create_buffer failed for %dx%d
    WaylandWindow: wl_compositor_create_surface failed
    WaylandWindow: wl_display_dispatch failed while waiting for initial configure: %s
    WaylandWindow: flushBuffer: no buffer available, frame dropped"

printf '\n=== result ===\n'
grep -E '^\[step|RESULT|NEW cwd entry' "${LOG}" 2>/dev/null || true
ok "the probe reached the shm buffer and flushed it"
exit 0
