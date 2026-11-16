#!/bin/sh
# chrome-sr-batch.sh - N Chrome runs against an own headless sway, via chrome-wl-run.sh.
# usage: chrome-sr-batch.sh FIRST_SUFFIX_PREFIX N [TIMEOUT]    logs: $DARLING_BUILD_DIR/chrome-sr-<PREFIX><i>.log
# Env: DARLING_SRC_DIR DARLING_OVERLAY DARLING_BUILD_DIR CHROME_APP. Starts its own sway (private 0700
# XDG_RUNTIME_DIR, headless, pixman) for the batch and stops it afterwards. Runs without GUEST-EXEC are
# invalid and repeated (up to 2N attempts). Prints chrome-run-summary.sh lines.
P=${1:?}; N=${2:?}; T=${3:-120}
: "${DARLING_SRC_DIR:?}" "${DARLING_OVERLAY:?}" "${DARLING_BUILD_DIR:?}"
RUN=/tmp/sr56-run
mkdir -p "$RUN" && chmod 700 "$RUN"
[ -f "$RUN/page.html" ] || cp /tmp/or55-run/page.html "$RUN/page.html"
XR=$RUN/xdg; mkdir -p "$XR" && chmod 700 "$XR"
export XDG_RUNTIME_DIR=$XR WAYLAND_DISPLAY=wayland-sr56
WLR_BACKENDS=headless WLR_LIBINPUT_NO_DEVICES=1 WLR_RENDERER=pixman \
  sway --unsupported-gpu > "$RUN/sway.log" 2>&1 &
SWAYPID=$!
i=0; while [ ! -S "$XR/wayland-1" ] && [ $i -lt 30 ]; do sleep 1; i=$((i+1)); done
ls "$XR" | grep -q wayland || { echo "sway did not start"; kill $SWAYPID; exit 1; }
export WAYLAND_DISPLAY=$(ls "$XR" | grep -E '^wayland-[0-9]+$' | head -1)
: "${CHROME_APP:?host path of the .app bundle}"; export CHROME_APP
export DARLING_GUEST_EXPOSE=$XR DARLING_GUEST_MKDIR=/wlprofile DARLING_GUEST_COPY="$RUN/page.html=/wlpage/page.html"
export CHROME_ARGS="--no-sandbox --user-data-dir=/wlprofile --disable-gpu --no-first-run --no-default-browser-check --disable-background-networking --disable-component-update --disable-sync --disable-breakpad ${CHROME_EXTRA_ARGS} file:///wlpage/page.html"
export CHROME_LOG_PREFIX=chrome-sr
valid=0; i=1
while [ $valid -lt "$N" ] && [ $i -le $((2*N)) ]; do
	S=$P$i
	sh "$DARLING_SRC_DIR/build-freebsd/chrome-wl-run.sh" "$S" "$T" DARLING_KEEP_LOCAL=1 DARLING_SR_RUN=1 $CHROME_SR_EXTRA >/dev/null 2>&1
	L=$DARLING_BUILD_DIR/chrome-sr-$S.log
	if grep -aq GUEST-EXEC "$L"; then valid=$((valid+1)); fi
	sh "$DARLING_SRC_DIR/build-freebsd/chrome-run-summary.sh" "$L"
	i=$((i+1))
done
kill $SWAYPID 2>/dev/null; sleep 1; kill -9 $SWAYPID 2>/dev/null
echo "BATCH-DONE $P valid=$valid"
