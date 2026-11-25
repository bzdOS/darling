#!/bin/sh
# bind-publish-probe driver: for each sockaddr shape bind a dgram socket
# over the same path shape the checkin socket uses, then sample the unix
# socket table and try to connect from a client.
# Host-only; no guest, no overlay.
set -u

SELF_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
OUT=${1:-/tmp/bind-publish-probe.out}
BIN=/tmp/bind-publish-probe.bin
KINDS="sunlen=45 sunlen=106 sunlen=44 sunlen=3 server server server server server server"

echo "PROVENANCE git_head=$(git -C "$SELF_DIR/.." rev-parse HEAD) kernel=$(uname -r) probe_src_sha=$(sha256 -q "$SELF_DIR/bind-publish-probe.c")" > "$OUT"
cc -O2 -o "$BIN" "$SELF_DIR/bind-publish-probe.c" || { echo "BUILD FAILED" >> "$OUT"; exit 1; }

for kind in $KINDS; do
	dir=$(mktemp -d /tmp/darling-run.PXXXXX)
	path="$dir/.darlingserver.sock"
	hold_out=/tmp/bind-publish-probe.hold.$$
	"$BIN" hold "$kind" "$path" 3 > "$hold_out" 2>&1 &
	hp=$!
	sleep 0.5
	file=$(test -S "$path" && echo y || echo n)
	rows=$(netstat -f unix | grep -F "$path" | wc -l | tr -d ' ')
	c_auto=$("$BIN" client "$path" auto 2>&1 | tr '\n' ';')
	c_106=$("$BIN" client "$path" sunlen=106 2>&1 | tr '\n' ';')
	wait $hp
	rb=$(grep -h 'readback\|bind_server\|bind ' "$hold_out" | tr '\n' ';')
	echo "KIND=$kind file=$file rows=$rows cl_auto=[$c_auto] cl_106=[$c_106] $rb" >> "$OUT"
	grep 'PROBE' "$hold_out" >> "$OUT"
	rm -rf "$dir" "$hold_out"
done

echo "DONE" >> "$OUT"
echo "log: $OUT"
