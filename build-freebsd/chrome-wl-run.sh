#!/bin/sh
# chrome-wl-run.sh - run Chrome (Mach-O) under Darling against a Wayland
# compositor, one log per run.
#
# usage: chrome-wl-run.sh SUFFIX TIMEOUT [ENV=val ...]
#
# Required environment (no defaults, nothing machine specific here):
#   DARLING_SRC_DIR DARLING_OVERLAY DARLING_BUILD_DIR   as for launch-dynamic
#   CHROME_APP         host path of the .app bundle
#   XDG_RUNTIME_DIR    private (mode 0700) directory that holds the compositor
#                      socket; passed to the guest unchanged
#   WAYLAND_DISPLAY    socket name inside XDG_RUNTIME_DIR
# Optional:
#   DARLING_GUEST_EXPOSE  colon separated host directories shown to the guest at
#                      the same path (launch-dynamic links them into the guest
#                      root). Must cover XDG_RUNTIME_DIR and every path in
#                      CHROME_ARGS (profile, local page).
#   CHROME_ARGS        space separated argv for Chrome (launch-dynamic splits
#                      DARLING_TEST_ARGS on spaces: no quoting, so no spaces
#                      inside one argument, at most 30 arguments, 1023 bytes)
#
# How the environment reaches the guest: this script runs launch-dynamic under
# `sudo env NAME=val ...` (sudo resets the environment, only explicit env
# arguments survive); launch-dynamic only adds to what it inherited and execs
# mldr, which hands the whole environment to the guest. DARLING_TEST_ARGS
# becomes the guest's argv[1..].
#
# Other useful NAME=val arguments: DARLING_KEEP_LOCAL=1 (do not re-stage the
# frameworks), DARLING_RPC_LOG=1 (darlingserver call/reply log).
# The log is never overwritten. WRAPPER_RC and WRAPPER_DURATION are the last
# lines; a timeout kill is reported as rc 124 and proves nothing by itself.
S=$1; T=$2; shift 2
: "${DARLING_SRC_DIR:?}" "${DARLING_OVERLAY:?}" "${DARLING_BUILD_DIR:?}"
: "${CHROME_APP:?}" "${XDG_RUNTIME_DIR:?}" "${WAYLAND_DISPLAY:?}"
LOG=$DARLING_BUILD_DIR/chrome-wl-$S.log
[ -e "$LOG" ] && { echo "exists $LOG"; exit 1; }
START=$(date +%s)
echo "WRAPPER_START=$START timeout=$T" > "$LOG"
/usr/local/bin/sudo -n env \
  DARLING_SRC_DIR="$DARLING_SRC_DIR" DARLING_OVERLAY="$DARLING_OVERLAY" \
  DARLING_BUILD_DIR="$DARLING_BUILD_DIR" \
  DARLING_TEST_BINARY=chrome-macho \
  DARLING_STAGING_TREES=System/Library/Frameworks:System/Library/PrivateFrameworks:usr/lib \
  DARLING_TRAP_LOG=1 \
  XDG_RUNTIME_DIR="$XDG_RUNTIME_DIR" WAYLAND_DISPLAY="$WAYLAND_DISPLAY" \
  DARLING_TEST_ARGS="$CHROME_ARGS" \
  DARLING_GUEST_EXPOSE="$DARLING_GUEST_EXPOSE" \
  "$@" CHROME_APP="$CHROME_APP" \
  timeout --foreground -k 5 "$T" "$DARLING_BUILD_DIR/launch-dynamic" >> "$LOG" 2>&1
echo "WRAPPER_RC=$?" >> "$LOG"
echo "WRAPPER_DURATION=$(( $(date +%s) - START ))s TIMEOUT=${T}s" >> "$LOG"
