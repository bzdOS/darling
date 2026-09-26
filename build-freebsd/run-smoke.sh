#!/bin/sh
# run-smoke.sh -- one-shot smoke test runner for the dyld dynamic-launch path.
#
# Wraps build/launch-dynamic with the standard environment, kills stale
# darlingserver instances, uses a timestamped log per run, prints the filtered
# result, and decodes any crash automatically (decode-crash.py).
#
# Usage:
#   sh build-freebsd/run-smoke.sh [test-binary]        # default: load-wayland-backend-macho
#
# Environment (all optional):
#   CFDBG=1              forwarded to the guest; enables _CFDBG() prints in CF
#   DYLD_DEBUG="..."     forwarded verbatim, e.g. DYLD_DEBUG="DYLD_PRINT_SEGMENTS=1 DYLD_PRINT_INITIALIZERS=1"
#                        (full words; this script exports them for the child)
#   WAIT_SECS=14         how long to let the test run before killing it
#   KEEP=1               keep /tmp/darling-local-overlay for post-mortem
#
# Env defaults are set ONE ASSIGNMENT PER LINE on purpose: ${SRC} inside a
# single `export A=x B=$A/y` expands to the OLD (empty) value.

SCRIPT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
SRC="${DARLING_SRC_DIR:-${SCRIPT_DIR}}"
DARLING_SRC_DIR="${SRC}"
DARLING_OVERLAY="${DARLING_OVERLAY:?set DARLING_OVERLAY to your overlay dir}"
DARLING_BUILD_DIR="${DARLING_BUILD_DIR:?set DARLING_BUILD_DIR to your build dir}"
TEST="${1:-load-wayland-backend-macho}"
WAIT_SECS="${WAIT_SECS:-14}"
PATH="/usr/local/bin:/usr/local/sbin:/usr/bin:/bin:/sbin:/usr/sbin"
export PATH DARLING_SRC_DIR DARLING_OVERLAY DARLING_BUILD_DIR

[ -f "${DARLING_BUILD_DIR}/launch-dynamic" ] || { echo "FATAL: ${DARLING_BUILD_DIR}/launch-dynamic missing" >&2; exit 1; }
[ -f "${SRC}/tests/${TEST}" ] || { echo "FATAL: test binary ${SRC}/tests/${TEST} missing" >&2; exit 1; }

LOG="${DARLING_BUILD_DIR}/smoke-${TEST}-$(date +%m%d-%H%M%S).log"

sudo pkill -9 darlingserver 2>/dev/null
sleep 1
sudo rm -rf /tmp/darling-dynamic-smoke /tmp/darling-local-overlay

# Env forwarded into the sudo child (guest sees these via mldr).
SUDO_ENV="DARLING_SRC_DIR=${DARLING_SRC_DIR} DARLING_OVERLAY=${DARLING_OVERLAY} DARLING_BUILD_DIR=${DARLING_BUILD_DIR} DARLING_TEST_BINARY=${TEST} DARLING_SMOKE_REFRESH=1"
# Forward fontconfig env so the host fontconfig shim (run as root under sudo)
# finds the system config and scans /usr/local/share/fonts (where Anthropic Sans
# lives). Without these, Onyx2D's NSFont/font matching logs
# "No font found for name Anthropic Sans" and text fails to render.
# The fontconfig config lives inside the overlay; HOME/XDG belong to the
# invoking user (sudo resets them to root's, which the guest cannot see).
SUDO_ENV="${SUDO_ENV} FONTCONFIG_PATH=${DARLING_OVERLAY}/usr/local/etc/fonts XDG_CONFIG_HOME=${XDG_CONFIG_HOME:-${HOME}/.config} HOME=${HOME}"
[ -n "${CHROME_APP}" ] && SUDO_ENV="${SUDO_ENV} CHROME_APP=${CHROME_APP}"
[ -n "${CFDBG}" ] && SUDO_ENV="${SUDO_ENV} CFDBG=${CFDBG}"
if [ -n "${MLDRTACE}" ]; then SUDO_ENV="${SUDO_ENV} MLDRTACE=${MLDRTACE}"; fi
# Forward Wayland display env to guest (guest's /tmp maps to overlay/tmp)
if [ -n "${WAYLAND_DISPLAY}" ]; then
    SUDO_ENV="${SUDO_ENV} WAYLAND_DISPLAY=${WAYLAND_DISPLAY}"
fi
if [ -n "${XDG_RUNTIME_DIR}" ]; then
    # Translate host path to guest path: /tmp/darling-local-overlay/foo -> /foo
    GUEST_XDG="${XDG_RUNTIME_DIR#/tmp/darling-local-overlay}"
    SUDO_ENV="${SUDO_ENV} XDG_RUNTIME_DIR=${GUEST_XDG}"
fi
if [ -n "${DYLD_DEBUG}" ]; then
    # shellcheck disable=SC2086
    export ${DYLD_DEBUG}
    SUDO_ENV="${SUDO_ENV} ${DYLD_DEBUG}"
fi

( sudo ${SUDO_ENV} "${DARLING_BUILD_DIR}/launch-dynamic" > "${LOG}" 2>&1 & )
sleep "${WAIT_SECS}"
sudo pkill -9 darlingserver 2>/dev/null
[ -z "${KEEP}" ] || echo "overlay kept at /tmp/darling-local-overlay"

echo "=== ${TEST} ($(basename "${LOG}")) ==="
grep -E "CFDBG|Symbol not found|FATAL|terminate|assert|Incompatible|principalClass|WaylandDisplay|Available backends|RESULT|^load-|^hello-|dyld: loaded.*AppKit" "${LOG}" | grep -v "patch_linux_raw_syscalls" | head -40

if grep -q "FATAL signal" "${LOG}"; then
    echo ""
    echo "=== crash decoded ==="
    python3 "${SRC}/build-freebsd/decode-crash.py" "${LOG}" "${DARLING_OVERLAY}" "${SRC}/tests"
fi
