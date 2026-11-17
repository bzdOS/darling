#!/bin/sh
# rc124-trace.sh - analyse one Chrome run log (and its debug darlingserver log) for the rc124 hang in
# dserver_rpc_explicit_mach_port_construct: is the reply lost around an S2C signal (65 = SIGRTMIN+1)?
# usage: rc124-trace.sh CHROME_LOG [DSERVER_LOG]   (DSERVER_LOG default: CHROME_LOG with .log -> -dserver.txt; .xz accepted)
#        rc124-trace.sh --selftest
# The run needs the server debug log (CHROME_SR_SERVER_LOG=debug); DARLING_RPC_LOG=1 in the server env
# (CHROME_SR_EXTRA=DARLING_RPC_LOG=1) adds "[dserver-rpc]" recv/reply-queued/sent lines to the Chrome log.
# Last stdout line:
#   RC124-TRACE caught=<y/n> rc=<code> overwrite=<n> outside_irq=<n> lost_reply=<y/n/unknown> evidence=<file:line>
# lost_reply: y   main thread's last call is explicit_mach_port_construct and the server logged a dropped/overwritten pushed reply,
#                 or (RPC log) the call was received and no reply was queued/sent to the caller afterwards;
#             n   main thread's last call is not the construct (server holds nothing), or (RPC log) the reply was sent;
#             unknown  anything else (no RPC log and no drop evidence, no hang, no logs).
# Also writes <CHROME_LOG minus .log>-rc124trace.txt (the main thread's last events, small).
if [ "$1" = "--selftest" ]; then
	T=$(mktemp -d "${TMPDIR:-/tmp}/rc124t.XXXXXX") || exit 1
	S=$0
	{ echo "WRAPPER_RC=124"; } > "$T/a.log"
	cat > "$T/a-dserver.txt" <<'EOF'
[1.0](calls, Debug) Received call #1 (dserver_callnum_checkin) from PID 10 (10), TID 11 (11)
[1.1](calls, Debug) Received call #50 (dserver_callnum_explicit_mach_port_construct) from PID 10 (10), TID 11 (11)
[1.2](s2c, Debug)[P:10(10)][T:11(11)] [T:11(11)]: Sending S2C signal
[1.3](calls, Error) Client tried to push reply outside of interrupt; dropping it
EOF
	o=$(sh "$S" "$T/a.log" | tail -1)
	echo "WRAPPER_RC=124" > "$T/b.log"
	cat > "$T/b-dserver.txt" <<'EOF'
[1.0](calls, Debug) Received call #1 (dserver_callnum_checkin) from PID 10 (10), TID 11 (11)
[1.1](calls, Debug) Received call #50 (dserver_callnum_explicit_mach_port_construct) from PID 10 (10), TID 11 (11)
[1.2](calls, Debug) Received call #9 (dserver_callnum_mach_msg) from PID 10 (10), TID 12 (12)
EOF
	o2=$(sh "$S" "$T/b.log" | tail -1)
	echo "WRAPPER_RC=0" > "$T/c.log"; cp "$T/b-dserver.txt" "$T/c-dserver.txt"
	o3=$(sh "$S" "$T/c.log" | tail -1)
	rm -rf "$T"
	case "$o" in *"caught=y rc=124 overwrite=0 outside_irq=1 lost_reply=y evidence="*) a=1;; *) a=0;; esac
	case "$o2" in *"caught=y rc=124 overwrite=0 outside_irq=0 lost_reply=unknown"*) b=1;; *) b=0;; esac
	case "$o3" in *"caught=n rc=0 "*) c=1;; *) c=0;; esac
	if [ $a = 1 ] && [ $b = 1 ] && [ $c = 1 ]; then echo "RC124-TRACE-SELFTEST pass"; exit 0; fi
	echo "RC124-TRACE-SELFTEST fail a=$a b=$b c=$c: [$o] [$o2] [$o3]"; exit 1
fi
CL=${1:?usage: rc124-trace.sh CHROME_LOG [DSERVER_LOG]}
DL=${2:-${CL%.log}-dserver.txt}
[ -f "$DL" ] || [ ! -f "$DL.xz" ] || DL=$DL.xz
RC=$(sed -n 's/^WRAPPER_RC=//p' "$CL" 2>/dev/null | tail -1)
RC=${RC:-none}
CAUGHT=n; [ "$RC" = 124 ] && CAUGHT=y
OUT=${CL%.log}-rc124trace.txt
if [ ! -f "$DL" ]; then
	echo "RC124-TRACE caught=$CAUGHT rc=$RC overwrite=0 outside_irq=0 lost_reply=unknown evidence=no-dserver-log"; exit 0
fi
case "$DL" in *.xz) CAT="xzcat";; *) CAT="cat";; esac
$CAT "$DL" | LC_ALL=C awk -v dl="$(basename "$DL")" -v rc="$CAUGHT" -v out="$OUT" '
function tidof(s,   m) { if (match(s, /TID [0-9]+/)) return substr(s, RSTART+4, RLENGTH-4); return "" }
function ttid(s,   m) { if (match(s, /\[T:[0-9]+\([0-9]+\)\] \[T:[0-9]+/)) { m = substr(s, RSTART, RLENGTH); sub(/.*\[T:/, "", m); return m } return "" }
/would overwrite saved reply|would overwrite existing saved reply/ { ow++; if (!owl) owl = NR }
/outside of interrupt/ { oi++; if (!oil) oil = NR }
/Dropping client-pushed reply/ { dr++ }
/Received call #[0-9]+ \(dserver_callnum_checkin\)/ { if (M == "") { M = tidof($0) } }
{
	t = ""
	if ($0 ~ /Received call #/) t = tidof($0)
	else if ($0 ~ /\(s2c,/) t = ttid($0)
	if (M != "" && t == M) {
		n++; ev[n] = NR ": " substr($0, 1, 200)
		if ($0 ~ /Received call #/) { lastcall = n; lastname = $0; lastline = NR }
		if ($0 ~ /Sending S2C signal/) { sig = n; sigline = NR }
	}
}
END {
	cons = (lastname ~ /explicit_mach_port_construct/)
	lost = "unknown"; evd = dl ":" (lastline ? lastline : 0)
	if (rc == "y" && M != "" && lastcall) {
		if (!cons) { lost = "n"; evd = dl ":" lastline }
		else if (ow + oi + dr > 0) { lost = "y"; evd = dl ":" (oil ? oil : (owl ? owl : lastline)) }
	}
	if (M != "") { s = (n > 40 ? n - 39 : 1); for (i = s; i <= n; i++) print dl ":" ev[i] > out }
	printf "%d %d %s %s %s %s\n", ow + 0, oi + 0, lost, evd, (M != "" ? M : ""), (cons ? "y" : "n")
}' > "$OUT.tmp"
read OW OI LOST EVD MT LASTCONS < "$OUT.tmp"
rm -f "$OUT.tmp"
# RPC log (DARLING_RPC_LOG=1, in the Chrome log): last recv of the main thread -> reply-queued to its address -> sent to it
if [ "$LOST" = unknown ] && [ "$CAUGHT" = y ] && [ -n "$MT" ] && grep -aq '^\[dserver-rpc\]' "$CL" 2>/dev/null; then
	R=$(LC_ALL=C awk -v m="$MT" '
		$2 ~ /^[0-9]+$/ && $3 == "recv" && $0 ~ (" tid=" m " ") { r = NR; from = $NF; sub(/^from=/, "", from); q = 0; s = 0; next }
		r && $3 == "reply-queued" && $NF == "to=" from { q = NR }
		r && $0 ~ /sent\[/ && $NF == "to=" from { s = NR }
		END { print r + 0, q + 0, s + 0 }' "$CL")
	set -- $R
	if [ "$1" -gt 0 ] && grep -aq "explicit_mach_port_construct" "$OUT" 2>/dev/null && [ "$LASTCONS" = y ]; then
		if [ "$3" -gt 0 ]; then LOST=n; EVD=$(basename "$CL"):$3
		elif [ "$2" -eq 0 ]; then LOST=y; EVD=$(basename "$CL"):$1
		else LOST=y; EVD=$(basename "$CL"):$2; fi
	fi
fi
echo "RC124-TRACE caught=$CAUGHT rc=$RC overwrite=$OW outside_irq=$OI lost_reply=$LOST evidence=$EVD"
