#!/bin/sh
# chrome-sr-batch.sh - N Chrome runs against an own headless sway, via chrome-wl-run.sh.
# usage: chrome-sr-batch.sh FIRST_SUFFIX_PREFIX N [TIMEOUT]    logs: $DARLING_BUILD_DIR/chrome-sr-<PREFIX><i>.log
# SNAP_AT (default 30): seconds after launch for a grim snapshot of the series' sway, if the run is alive.
# Env: DARLING_SRC_DIR DARLING_OVERLAY DARLING_BUILD_DIR CHROME_APP. Starts its own sway (private 0700
# XDG_RUNTIME_DIR, headless, pixman) for the batch and stops it afterwards. Runs without GUEST-EXEC are
# invalid and repeated (up to 2N attempts). Prints chrome-run-summary.sh lines.
# exclusive run lock: one probe/Chrome run at a time; nested calls (DARLING_LOCK_HELD) do not re-lock
if [ -z "$DARLING_LOCK_HELD" ]; then
	: "${DARLING_BUILD_DIR:?}"
	DARLING_LOCK_HELD=1 lockf -t 0 "$DARLING_BUILD_DIR/.darling-run.lock" sh "$0" "$@"
	rc=$?
	[ $rc -eq 75 ] && { echo "LOCK busy"; exit 1; }
	exit $rc
fi
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
# SEAT_INPUT=1: give the series sway a virtual keyboard and pointer (seat capabilities 3 instead of 0)
# with $SEAT_INPUT_BIN (default $DARLING_BUILD_DIR/conjure-wayland-input, built from conjure-wayland-input.c);
# the devices live only while that process does, it is stopped with the sway. Default: off.
CONJPID=""
if [ "${SEAT_INPUT:-0}" = 1 ]; then
	"${SEAT_INPUT_BIN:-$DARLING_BUILD_DIR/conjure-wayland-input}" > "$RUN/conjure.log" 2>&1 &
	CONJPID=$!
	sleep 1
	swaymsg -t get_seats 2>/dev/null | grep -o "\"capabilities\": *[0-9]*" | sed "s/^/seat /"
fi
: "${CHROME_APP:?host path of the .app bundle}"; export CHROME_APP
export DARLING_GUEST_EXPOSE=$XR DARLING_GUEST_MKDIR=/wlprofile DARLING_GUEST_COPY="$RUN/page.html=/wlpage/page.html"
export CHROME_ARGS="--no-sandbox --user-data-dir=/wlprofile --disable-gpu --no-first-run --no-default-browser-check --disable-background-networking --disable-component-update --disable-sync --disable-breakpad ${CHROME_EXTRA_ARGS} file:///wlpage/page.html"
export CHROME_LOG_PREFIX=chrome-sr
valid=0; i=1
while [ $valid -lt "$N" ] && [ $i -le $((2*N)) ]; do
	# disk guard: stop between runs at >= SERIES_DISK_STOP (default 85) % root use
	DISK_USE=$(df -P / | awk 'NR==2{gsub("%","",$5); print $5+0}')
	[ "$DISK_USE" -ge "${SERIES_DISK_STOP:-85}" ] && { echo "SERIES-STOP reason=disk use=${DISK_USE}% after=$((i-1))"; break; }
	S=$P$i
	L=$DARLING_BUILD_DIR/chrome-sr-$S.log
	PNG=$DARLING_BUILD_DIR/chrome-sr-$S.png
	(
		sleep "${SNAP_AT:-30}"
		if grep -aq GUEST-EXEC "$L" && ! grep -aq '^WRAPPER_RC=' "$L"; then
			grim "$PNG" >/dev/null 2>&1
			# swaymsg finds the series' own sway only through its IPC socket (private XDG_RUNTIME_DIR, no SWAYSOCK in the env):
			# without it swaymsg printed "Unable to retrieve socket path" and the count was always 0, whatever was on screen
			# (read-only get_tree; a view node carries a "shell" key, an app_id may be unset)
			SS=$(ls "$XR"/sway-ipc.* 2>/dev/null | head -1)
			# self-check: no socket, or swaymsg failing/empty, is "unknown", never 0
			TREE=$([ -n "$SS" ] && SWAYSOCK=$SS swaymsg -t get_tree 2>/dev/null)
			if [ -n "$TREE" ]; then w=$(printf '%s\n' "$TREE" | grep -c '"shell"'); else w=unknown; fi
			echo "SNAP windows=$w png=$PNG" >> "$L"
		fi
	) &
	SNAPPID=$!
	sh "$DARLING_SRC_DIR/build-freebsd/chrome-wl-run.sh" "$S" "$T" DARLING_KEEP_LOCAL=1 DARLING_SR_RUN=1 $CHROME_SR_EXTRA >/dev/null 2>&1
	kill $SNAPPID 2>/dev/null
	if grep -aq GUEST-EXEC "$L"; then valid=$((valid+1)); fi
	sh "$DARLING_SRC_DIR/build-freebsd/chrome-run-summary.sh" "$L"
	i=$((i+1))
done
[ -n "$CONJPID" ] && kill $CONJPID 2>/dev/null
kill $SWAYPID 2>/dev/null; sleep 1; kill -9 $SWAYPID 2>/dev/null
echo "BATCH-DONE $P valid=$valid"
