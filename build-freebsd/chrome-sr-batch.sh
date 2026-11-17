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
# own private runtime dir per series (a shared fixed dir collected sockets of every old/other sway and "ls | head -1" then
# picked a foreign wayland-1 and sway-ipc: fix14 windows=unknown); the guest sees it via DARLING_GUEST_EXPOSE
XR=$(mktemp -d /tmp/sr56-xdg.XXXXXX) && chmod 700 "$XR" || { echo "SERIES-ABORT reason=xdg-dir"; exit 1; }
export XDG_RUNTIME_DIR=$XR
SWAYPID=""
start_sway() {
	rm -f "$XR"/wayland-* "$XR"/sway-ipc.* 2>/dev/null
	WLR_BACKENDS=headless WLR_LIBINPUT_NO_DEVICES=1 WLR_RENDERER=pixman \
	  sway --unsupported-gpu > "$RUN/sway.log" 2>&1 &
	SWAYPID=$!
	_i=0; while [ -z "$(ls "$XR" | grep -E '^wayland-[0-9]+$')" ] && [ $_i -lt 30 ]; do sleep 1; _i=$((_i+1)); done
	ls "$XR" | grep -qE '^wayland-[0-9]+$' || return 1
	export WAYLAND_DISPLAY=$(ls "$XR" | grep -E '^wayland-[0-9]+$' | head -1)
	SWAYSOCKF=$XR/sway-ipc.$(id -u).$SWAYPID.sock
}
# the series' sway is healthy: process alive and its IPC answers (SWAYSOCK of exactly this pid, never another sway's)
sway_ok() { kill -0 "$SWAYPID" 2>/dev/null && [ -S "$SWAYSOCKF" ] && SWAYSOCK=$SWAYSOCKF swaymsg -t get_version >/dev/null 2>&1; }
start_sway || { echo "sway did not start"; kill $SWAYPID; rm -rf "$XR"; exit 1; }
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
			SS=$SWAYSOCKF; [ -S "$SS" ] || SS=
			# get_tree is one JSON text: tiling nodes and floating_nodes (a modal panel is floating) both sit in it and both views carry
			# "shell", so the textual count includes floating windows (floating-count check: series-count-selftest.sh)
			# self-check: no socket, or swaymsg failing/empty, is "unknown", never 0
			TREE=$([ -n "$SS" ] && SWAYSOCK=$SS swaymsg -t get_tree 2>/dev/null)
			if [ -n "$TREE" ]; then w=$(printf '%s\n' "$TREE" | grep -c '"shell"'); else w=unknown; fi
			echo "SNAP windows=$w png=$PNG" >> "$L"
		fi
	) &
	SNAPPID=$!
	sh "$DARLING_SRC_DIR/build-freebsd/chrome-wl-run.sh" "$S" "$T" DARLING_KEEP_LOCAL=1 DARLING_SR_RUN=1 $CHROME_SR_EXTRA >/dev/null 2>&1
	kill $SNAPPID 2>/dev/null
	# a run is invalid (not counted, retried; series-count.sh skips logs with RUN-INVALID) when the series' own sway was not healthy
	# after it: that is a Wayland-side failure, not a Chrome result. A guest that never calls wl_display_connect with a healthy
	# sway stays counted (that is a guest stall; SERIES_RETRY_NOWL=1 retries those as well).
	INV=""
	if ! sway_ok; then INV=sway-down
	elif [ "${SERIES_RETRY_NOWL:-0}" = 1 ] && ! grep -aq "wl_display_connect returned" "$L"; then INV=wayland-not-connected; fi
	if [ -n "$INV" ]; then
		echo "RUN-INVALID run=$S reason=$INV sway_pid=$SWAYPID display=$WAYLAND_DISPLAY (not counted, retrying)"
		echo "RUN-INVALID reason=$INV" >> "$L"
		sway_ok || { kill $SWAYPID 2>/dev/null; start_sway || { echo "SERIES-ABORT reason=sway"; break; }; }
	elif grep -aq GUEST-EXEC "$L"; then valid=$((valid+1)); fi
	sh "$DARLING_SRC_DIR/build-freebsd/chrome-run-summary.sh" "$L"
	# crash class (133 SIGTRAP, 134 ABRT, 139 SEGV): IMAGES line (load bases) + image+offset frames, saved as chrome-sr-<run>-crash.txt
	case $(sed -n 's/^WRAPPER_RC=//p' "$L" | tail -1) in 132|133|134|139)
		sh "$DARLING_SRC_DIR/build-freebsd/guest-abort-symbolize.sh" "$L" 2>/dev/null | cut -c1-1500 | tee "$DARLING_BUILD_DIR/chrome-sr-$S-crash.txt" ;;
	esac
	i=$((i+1))
done
[ -n "$CONJPID" ] && kill $CONJPID 2>/dev/null
kill $SWAYPID 2>/dev/null; sleep 1; kill -9 $SWAYPID 2>/dev/null
rm -rf "$XR"
echo "BATCH-DONE $P valid=$valid"
