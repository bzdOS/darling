#!/bin/sh
# launch-chrome.sh — launch the downloaded macOS Chrome (Mach-O) under Darling,
# routing through the AppKit Wayland.backend.
#
# Launch model (MUST be `launch-dynamic`, not manual darlingserver+mldr):
#   Starting darlingserver by hand (`darlingserver <prefix> <uid> <gid> 0 0`)
#   and exec'ing mldr as a separate client crashes darlingserver with
#   std::system_error in MessageQueue::sendMany — the guest connects before
#   the server loop is ready and a response is queued to fd 0 (the listener
#   socket). launch-dynamic (tests/launch-dynamic-smoke.c) does the correct
#   pipe handshake: it forks darlingserver and waits for the readiness byte
#   (".") BEFORE exec'ing mldr.
#
# Chrome support is BUILT INTO launch-dynamic: pass CHROME_APP (host path to
# the .app) through the environment and, for DARLING_TEST_BINARY=chrome-macho,
# it stages the embedded framework to $LOCAL/Frameworks (+ /tmp/Frameworks
# symlinks covering every @loader_path resolution) and copies the launcher
# binary from $DARLING_SRC_DIR/tests/chrome-macho to $LOCAL/chrome-macho.
# Do NOT pre-stage anything under /tmp/darling-local-overlay yourself:
# launch-dynamic's cleanup() wipes it unconditionally at startup.
#
# Usage:
#   WAYLAND_DISPLAY=wayland-1 XDG_RUNTIME_DIR=/tmp/wayland-test \
#     WAIT_SECS=60 sh build-freebsd/launch-chrome.sh

SCRIPT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
SRC="${DARLING_SRC_DIR:-${SCRIPT_DIR}}"
OD="${DARLING_OVERLAY:?set DARLING_OVERLAY to your overlay dir}"
BD="${DARLING_BUILD_DIR:?set DARLING_BUILD_DIR to your build dir}"
CHROME_APP="${CHROME_APP:-/tmp/chrome-cft/chrome-mac-x64/Google Chrome for Testing.app}"

LD="${BD}/launch-dynamic"
WAIT_SECS="${WAIT_SECS:-60}"
# TEST_BIN: guest binary to run (default chrome-macho). E.g.
#   TEST_BIN=chrome-dlopen-probe-macho — path-resolution probe (prints dlerror
#   for every candidate spelling of the framework path in one run).
TEST_BIN="${TEST_BIN:-chrome-macho}"

LOG="${BD}/smoke-chrome-$(date +%m%d-%H%M%S).log"
echo "LOG=$LOG"

PATH="/usr/local/bin:/usr/local/sbin:/usr/bin:/bin:/sbin:/usr/sbin"
export PATH DARLING_SRC_DIR DARLING_OVERLAY DARLING_BUILD_DIR

[ -f "$LD" ] || { echo "FATAL: $LD missing" >&2; exit 1; }
[ -d "$CHROME_APP" ] || { echo "FATAL: $CHROME_APP missing" >&2; exit 1; }
[ -f "$CHROME_APP/Contents/MacOS/Google Chrome for Testing" ] || {
  echo "FATAL: Chrome launcher binary missing" >&2; exit 1; }

sudo pkill -9 darlingserver 2>/dev/null; sleep 1

# Source tree layout launch-dynamic expects: $DARLING_SRC_DIR/tests/<binary>.
TMP_SRC="$(mktemp -d /tmp/darling-chrome-src.XXXXXX)"
sudo mkdir -p "$TMP_SRC/tests"
if [ "$TEST_BIN" = "chrome-macho" ]; then
  sudo cp "$CHROME_APP/Contents/MacOS/Google Chrome for Testing" "$TMP_SRC/tests/chrome-macho"
else
  sudo cp "$SRC/tests/$TEST_BIN" "$TMP_SRC/tests/$TEST_BIN"
fi
sudo chmod 755 "$TMP_SRC/tests/"*

# Guest env: translate host XDG_RUNTIME_DIR (strip local-overlay prefix).
GUEST_XDG="${XDG_RUNTIME_DIR#/tmp/darling-local-overlay}"

# NOTE: every env value is passed as ONE quoted word — CHROME_APP contains
# spaces ("Google Chrome for Testing.app"), so a flat ${SUDO_ENV} expansion
# would word-split it.
echo "Running: $LD (DARLING_TEST_BINARY=$TEST_BIN, CHROME_APP=$CHROME_APP)" | tee -a "$LOG"
( sudo env \
    "DARLING_SRC_DIR=$TMP_SRC" \
    "DARLING_OVERLAY=$OD" \
    "DARLING_BUILD_DIR=$BD" \
    "DARLING_TEST_BINARY=$TEST_BIN" \
    "CHROME_APP=$CHROME_APP" \
    ${WAYLAND_DISPLAY:+"WAYLAND_DISPLAY=$WAYLAND_DISPLAY"} \
    ${GUEST_XDG:+"XDG_RUNTIME_DIR=$GUEST_XDG"} \
    ${CFDBG:+"CFDBG=$CFDBG"} \
    ${DYLD_DEBUG:+"${DYLD_DEBUG}"} \
    ${DYLD_PRINT_LIBRARIES:+"DYLD_PRINT_LIBRARIES=$DYLD_PRINT_LIBRARIES"} \
    ${DYLD_PRINT_INITIALIZERS:+"DYLD_PRINT_INITIALIZERS=$DYLD_PRINT_INITIALIZERS"} \
    ${DYLD_PRINT_BINDINGS:+"DYLD_PRINT_BINDINGS=$DYLD_PRINT_BINDINGS"} \
	${DYLD_PRINT_TO_FILE:+"DYLD_PRINT_TO_FILE=$DYLD_PRINT_TO_FILE"} \
    ${DARLING_PROBE_PATH:+"DARLING_PROBE_PATH=$DARLING_PROBE_PATH"} \
    ${DARLING_SMOKE_REFRESH:+"DARLING_SMOKE_REFRESH=$DARLING_SMOKE_REFRESH"} \
    "$LD" >"$LOG" 2>&1 & )

# Wait with EARLY EXIT: poll the log for terminal markers instead of sleeping
# the full WAIT_SECS (a fast crash should not cost a minute).
i=0
while [ "$i" -lt "$WAIT_SECS" ]; do
  if grep -qE "FATAL signal|Terminating due to uncaught|image not found|Symbol not found|Library not loaded|PROBE DONE|visible=" "$LOG" 2>/dev/null; then
    break
  fi
  sleep 1
  i=$((i+1))
done
sleep 1   # let trailing output flush
sudo pkill -9 darlingserver 2>/dev/null

echo "=== chrome launch result ($(basename "$LOG"), waited ${i}s) ==="
# Quiet summary: drop per-image dyld noise; show lifecycle + errors only.
LINES_ALL=$(wc -l < "$LOG")
IMAGES=$(grep -c "dyld: loaded:" "$LOG")
echo "log lines=$LINES_ALL, dyld images loaded=$IMAGES"
grep -vE "dyld: loaded:|dyld: Mapping|dyld: Speculatively|dyld: (re-export|using)|__TEXT at|__DATA at|__LINKEDIT at|patch_linux_raw_syscalls|version 11\.0\.0|DEBUG stack slots|DEBUG pre-start|^\s+\[\s*[0-9]+\] 0x" "$LOG" | head -60
if grep -q "FATAL signal" "$LOG"; then
  echo "=== crash decoded ==="
  python3 "$SRC/build-freebsd/decode-crash.py" "$LOG" "$OD" "$SRC/tests"
fi
