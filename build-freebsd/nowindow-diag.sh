#!/bin/sh
# nowindow-diag.sh - why does a Chrome-under-Darling run show no window while the process lives?
#   nowindow-diag.sh analyze LOG [GUEST_DUMP]   offline: verdict from a chrome-sr-*.log (+ optional -guest.txt)
#   nowindow-diag.sh run PREFIX [TIMEOUT]       one series run (N=1, NWD_CHROME_ARGS default --enable-logging=stderr --single-process, no lldb dump,
#                                               swaymsg poll every 10 s on the series' own sway), then analyze
# Env: DARLING_BUILD_DIR DARLING_OVERLAY DARLING_SRC_DIR (all modes), CHROME_APP (run).
# alert= is the NSAlert message text when the guest logged one (ALERT-PROBE line), else none.
# Output: last line "NOWINDOW-DIAG main=<where> wayland=<yes/no> windows=<max> reached=<stage> maxgap=<s>s last=<hh:mm:ss> verdict=<...>"
# Exit: 0 verdict=window-shown, 1 no window (verdict says why), 2 usage/input error.
: "${DARLING_BUILD_DIR:?}" "${DARLING_OVERLAY:?}" "${DARLING_SRC_DIR:?}"
W=$(cd "$(dirname "$0")" && pwd)
sym() { # image-relative offset -> name via llvm-symbolizer
	llvm-symbolizer --obj="$1" "$2" 2>/dev/null | head -1
}
analyze() {
	L=$1; G=${2:-${1%.log}-guest.txt}; TL=${1%.log}-windows.txt
	XT=
	# a series compresses its big logs at the end: read LOG.xz when the plain log is gone
	if [ ! -r "$L" ] && [ -r "$L.xz" ]; then XT=$(mktemp "${TMPDIR:-/tmp}/nwd.XXXXXX") && xz -dc "$L.xz" > "$XT" && L=$XT; fi
	[ -r "$L" ] || { echo "unreadable $L"; return 2; }
	O=$DARLING_OVERLAY
	wl=no; grep -aq "wl_display_connect returned 0x[0-9a-f]*[1-9a-f]" "$L" && wl=yes
	rt=no; grep -aq "roundtrip1 returned" "$L" && rt=yes
	win=0
	[ -r "$TL" ] && win=$(awk '{if($2+0>m)m=$2+0}END{print m+0}' "$TL")
	s=$(sed -n 's/^SNAP windows=\([0-9]*\).*/\1/p' "$L" | tail -1); [ "${s:-0}" -gt "$win" ] && win=$s
	# Chrome log timeline: max gap between consecutive Chrome log lines, last line time
	TLN=$(grep -a '^\[[0-9]*:[0-9]*:[0-9]*/[0-9]*\.[0-9]*:' "$L" | sed -n 's/^\[[0-9]*:[0-9]*:[0-9]*\/\([0-9][0-9]\)\([0-9][0-9]\)\([0-9][0-9]\)\.[0-9]*:.*/\1 \2 \3/p' |
		awk '{t=$1*3600+$2*60+$3; if(NR>1&&t-p>g)g=t-p; p=t; l=sprintf("%02d:%02d:%02d",$1,$2,$3)} END{printf "%d %s",g,l}')
	gap=${TLN% *}; last=${TLN#* }
	reached=startup
	grep -aq "AddKeepAlive(.*kBrowserWindow" "$L" && reached=browser-window-created
	grep -aq "Handling shutdown for signal" "$L" && sd=yes || sd=no
	# main thread: symbolize guest dump THREAD 1
	main=unknown
	if [ -r "$G" ]; then
		fr=$(awk '/^THREAD 1 /{f=1;next} /^THREAD /{f=0} f&&/^  #[0-9]+ [A-Za-z]/{print $2}' "$G" | head -40)
		main=$(echo "$fr" | head -1 | sed 's/+0x.*//')
		AK=$O/System/Library/Frameworks/AppKit.framework/Versions/C/AppKit
		leaf=""; modal=""
		for f in $fr; do
			img=${f%%+*}; off=${f#*+}
			case $img in
			AppKit) n=$(sym "$AK" "$off");;
			libsystem_kernel.dylib) n=$(sym "$O/usr/lib/system/libsystem_kernel.dylib" "$off");;
			*) n="";;
			esac
			case $n in *runModal*) modal=NSAlert-runModal\>;; esac
			case $n in dserver_rpc_*|sys_psynch*|*ulock*|*kevent*|*mach_msg*) [ -n "$leaf" ] || leaf=emul:$n;; esac
		done
		[ -n "$leaf" ] && main=$leaf
		main=$modal$main
		[ "$main" = "ChromeFW" ] && main=chrome-code
		grep -a "_sigrt_handler\|sigrt_handler" "$G" >/dev/null && main="$main(sigrt-handler)"
	fi
	# NSAlert text (ALERT-PROBE from AppKitExtras alert.c): the dialog the main thread is blocked in
	alert=$(sed -n 's/^ALERT-PROBE runModal message="\(.*\)" informative=.*/\1/p' "$L" | head -1 | tr ' ' '_' | cut -c1-80)
	[ -n "$XT" ] && rm -f "$XT"
	if [ "${win:-0}" -gt 0 ]; then v=window-shown; rc=0
	elif [ "$wl" = no ]; then v=wayland-not-connected; rc=1
	elif [ "$reached" = startup ]; then v=stalled-before-browser-window; rc=1
	elif [ "${gap:-0}" -ge 30 ]; then v=slow-startup-gap-${gap}s-then-browser-window-created-but-no-surface-within-timeout; rc=1
	else v=browser-window-created-but-no-wayland-window; rc=1; fi
	echo "NOWINDOW-DIAG main=$main wayland=$wl windows=$win reached=$reached maxgap=${gap}s last=$last sigterm=$sd alert=${alert:-none} verdict=$v"
	return $rc
}
case "$1" in
analyze) shift; analyze "$@"; exit $?;;
run)
	P=${2:?prefix}; T=${3:-300}
	: "${CHROME_APP:?host path of the .app bundle}"
	export DARLING_BUILD_DIR DARLING_OVERLAY DARLING_SRC_DIR CHROME_APP CHROME_SR_SEGV=1 CHROME_SR_DUMP=0
	export CHROME_EXTRA_ARGS="${NWD_CHROME_ARGS:---enable-logging=stderr --single-process}"
	L=$DARLING_BUILD_DIR/chrome-sr-${P}1.log; TL=${L%.log}-windows.txt; : > "$TL"
	sh "$W/chrome-series.sh" "$P" 1 "$T" > "${L%.log}-series.out" 2>&1 &
	SP=$!
	# poll the series' own sway (private socket under its XDG_RUNTIME_DIR); read-only get_tree
	while kill -0 $SP 2>/dev/null; do
		sleep 10
		S=$(ls /tmp/sr56-run/xdg/sway-ipc.* 2>/dev/null | head -1)
		[ -n "$S" ] && echo "$(date +%T) $(SWAYSOCK=$S swaymsg -t get_tree 2>/dev/null | grep -c '"shell"')" >> "$TL"
	done
	wait $SP
	analyze "$L"; exit $?;;
*) echo "usage: $0 analyze LOG [GUEST_DUMP] | run PREFIX [TIMEOUT]"; exit 2;;
esac
