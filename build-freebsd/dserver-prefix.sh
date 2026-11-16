#!/bin/sh
# dserver-prefix.sh - sourced by run scripts: give each run its own darlingserver prefix and
# make sure that run's server is dead and its prefix removed when the script ends.
#   dsp_new   creates DARLING_PREFIX=/tmp/darling-run.XXXXXX (exported; pass it on to launch-dynamic)
#   dsp_reap  kills the darlingserver whose argv holds exactly that prefix, waits for it to die,
#             then removes only that prefix (never while the server is alive). Also runs on EXIT/INT/TERM.
dsp_reap() {
	[ -n "$DARLING_PREFIX" ] || return 0
	_p=$DARLING_PREFIX
	_i=0
	while :; do
		_pids=$(ps -axww -o pid,command | awk -v k="darlingserver $_p " 'index($0,k){printf "%s ",$1}')
		[ -n "$_pids" ] || break
		[ $_i -lt 600 ] || { echo "dsp_reap: server pids $_pids for $_p still alive, prefix kept"; return 1; }
		/usr/local/bin/sudo -n kill -9 $_pids 2>/dev/null
		_i=$((_i+1)); sleep 0.1
	done
	/usr/local/bin/sudo -n rm -rf "$_p"
	DARLING_PREFIX=
}
dsp_new() {
	DARLING_PREFIX=$(mktemp -d /tmp/darling-run.XXXXXX) || return 1
	chmod 755 "$DARLING_PREFIX"
	export DARLING_PREFIX
	trap dsp_reap EXIT
	trap 'dsp_reap; exit 130' INT
	trap 'dsp_reap; exit 143' TERM
}
