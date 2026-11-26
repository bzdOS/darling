#!/bin/sh
# fd-view watcher v6 (step 12): pid by pgrep -x darlingserver from t=0 (no run-log
# discovery lag), 20 ms cadence, per sample: fd3 TYPE, sockstat -u row for the pid,
# netstat -Anf unix rows for *.darlingserver.sock, socket-file existence (newest
# darling-run.* prefix). Full procstat -f on anomalies (fd3 not a socket while alive)
# and every 5th live sample. v6 tools are the step-12 visibility winners: on this
# kernel sockstat -u AND netstat -Anf unix both list a bound-unconnected dgram
# listener, and sendto semantics are 0=live-listener / 61=vnode-without-socket /
# 2=no-path (measured 2026-11-26, /tmp/mx-3state probe).
# usage: fdwatch6.sh OUT_FILE DURATION_SEC
if [ "$(id -u)" != "0" ]; then
	exec sudo -n sh "$0" "$@"
fi
OUT=${1:?OUT_FILE}; WEND=$(( $(date +%s) + ${2:?DURATION_SEC} ))
i=0; live=0
echo "PROVENANCE fdwatch6 start=$(date -u +%Y-%m-%dT%H:%M:%SZ) host=$(uname -n)" > "$OUT"
while [ "$(date +%s)" -lt "$WEND" ]; do
	i=$((i+1)); t=$(date +%s)
	pid=$(pgrep -x darlingserver 2>/dev/null | head -1)
	alive=no
	[ -n "$pid" ] && { lv=$(procstat basic "$pid" 2>/dev/null | awk 'NR==2{print $1}'); [ "$lv" = "$pid" ] && alive=y; }
	rows=$(netstat -Anf unix 2>/dev/null | grep -F '.darlingserver.sock')
	nr=$(printf '%s\n' "$rows" | grep -c . )
	sock=$(sockstat -u 2>/dev/null | awk -v p="$pid" '$3==p')
	ns=$(printf '%s' "$sock" | grep -c .)
	f=$(ls -t /tmp/darling-run.*/.darlingserver.sock 2>/dev/null | head -1)
	fl=n; [ -n "$f" ] && [ -S "$f" ] && fl=y
	fd3=-
	if [ "$alive" = y ]; then
		live=$((live+1))
		fd3=$(procstat -f "$pid" 2>/dev/null | awk '$3=="3"{print $4}')
		[ -z "$fd3" ] && fd3=none
		printf 'S %s t=%s pid=%s alive=y fd3=%s sockrows=%s pathrows=%s file=%s\n' "$i" "$t" "$pid" "$fd3" "$ns" "$nr" "$fl" >> "$OUT"
		if [ "$fd3" != "s" ] || [ $((live % 5)) = 0 ]; then
			{ echo "FD $i t=$t pid=$pid fd3=$fd3"; procstat -f "$pid" 2>/dev/null | head -25; } >> "$OUT"
		fi
	else
		printf 'S %s t=%s pid=%s alive=n sockrows=%s pathrows=%s file=%s\n' "$i" "$t" "${pid:-none}" "$ns" "$nr" "$fl" >> "$OUT"
	fi
	[ "$nr" -gt 0 ] && printf '%s\n' "$rows" >> "$OUT"
	[ "$ns" -gt 0 ] && printf 'SS %s %s\n' "$t" "$(printf '%s' "$sock" | tr '\n' ';')" >> "$OUT"
	sleep 0.02
done
echo "S-END samples=$i live=$live" >> "$OUT"
