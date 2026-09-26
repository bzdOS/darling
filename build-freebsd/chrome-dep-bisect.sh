#!/bin/sh
# chrome-dep-bisect.sh — dlopen each candidate dependency INDIVIDUALLY (via
# chrome-dlopen-probe-macho + DARLING_PROBE_PATH) to find which one crashes
# dyld. Turns "SIGSEGV while linking the 254MB framework" into "dependency X
# crashes when linked alone".
#
# Usage:
#   WAYLAND_DISPLAY=wayland-1 XDG_RUNTIME_DIR=/tmp/wayland-test \
#     sh build-freebsd/chrome-dep-bisect.sh
#
# Env (passthrough to launch-chrome.sh):
#   TARGETS="path1 path2 ..."   guest paths to try, whitespace-separated
#                               (default: the usual suspects + the framework)
#   TEST_BIN / CHROME_APP / WAIT_SECS / DYLD_DEBUG
#
# NOTE: one launch at a time (shared /tmp/darling-dynamic-smoke).

SCRIPT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
SRC="${DARLING_SRC_DIR:-${SCRIPT_DIR}}"
CHROME_APP="${CHROME_APP:-/tmp/chrome-cft/chrome-mac-x64/Google Chrome for Testing.app}"
FWVER="154.0.8029.0"

TARGETS="${TARGETS:-/System/Library/Frameworks/CoreFoundation.framework/Versions/A/CoreFoundation
/System/Library/Frameworks/CoreGraphics.framework/Versions/A/CoreGraphics
/System/Library/Frameworks/CoreText.framework/Versions/A/CoreText
/System/Library/Frameworks/Foundation.framework/Versions/C/Foundation
/System/Library/Frameworks/AppKit.framework/Versions/C/AppKit
/System/Library/Frameworks/Metal.framework/Versions/A/Metal
/System/Library/Frameworks/QuartzCore.framework/Versions/A/QuartzCore
/System/Library/Frameworks/CoreImage.framework/Versions/A/CoreImage
/System/Library/PrivateFrameworks/Onyx2D.framework/Onyx2D
/Frameworks/Google Chrome for Testing Framework.framework/Versions/$FWVER/Google Chrome for Testing Framework}"

echo "== chrome-dep-bisect: one dlopen per launch =="
printf '%s\n' "$TARGETS" | while IFS= read -r t; do
  [ -z "$t" ] && continue
  out=$(DARLING_PROBE_PATH="$t" \
        WAYLAND_DISPLAY="${WAYLAND_DISPLAY}" \
        XDG_RUNTIME_DIR="${XDG_RUNTIME_DIR}" \
        WAIT_SECS="${WAIT_SECS:-40}" \
        TEST_BIN=chrome-dlopen-probe-macho \
        CHROME_APP="$CHROME_APP" \
        sh "$SRC/build-freebsd/launch-chrome.sh" 2>&1)
  log=$(printf '%s\n' "$out" | sed -n 's/^LOG=//p' | head -1)
  case "$log" in
    /*) logpath="$log" ;;
    "") logpath="" ;;
    *)  logpath="${DARLING_BUILD_DIR:?set DARLING_BUILD_DIR}/$log" ;;
  esac
  if [ -f "$logpath" ] && grep -q "dlopen: OK" "$logpath"; then
    verdict="OK"
  elif [ -f "$logpath" ] && grep -q "FATAL signal" "$logpath"; then
    verdict="CRASH-DYLD"
  else
    verdict="FAIL"
  fi
  reason=$([ -f "$logpath" ] && grep -oE "Symbol not found: \S+|image not found|Incompatible library version[^\"]*" "$logpath" | head -1)
  printf '%-11s %s\n' "$verdict" "$t"
  [ -n "$reason" ] && printf '%-11s   -> %s\n' "" "$reason"
  if [ "$verdict" = "CRASH-DYLD" ] && [ -f "$logpath" ]; then
    python3 "$SRC/build-freebsd/decode-crash.py" "$logpath" "${DARLING_OVERLAY:?set DARLING_OVERLAY}" "$SRC/tests" 2>/dev/null | grep -E "rip|pseudo|stack 0x" | head -6
  fi
done
echo "== done =="
