#!/bin/sh
# run-dserver-survive-probe.sh - darlingserver must outlive malformed requests and a client killed mid-call.
#   DSERVER-SURVIVE-VERDICT pass|FAIL n=<checks> bad=<failed> [<names>]  (rc 0 = pass)
# Starts a guest (tests/dserver-survive-probe-macho, a loop of Mach port round trips) under launch-dynamic, then
#   parse-garbage   sends malformed datagrams to the server socket (too short for a call header; header with an
#                   invalid call number; header from an unknown pid/tid): the server must stay up and the guest keep ticking
#   kill-client     kills the guest (kill -9, by pid) while it is calling: the server must stay up
# Before the exception-by-class fix (call.cpp/server.cpp/thread.cpp patch darlingserver-dead-client.patch) a
# malformed request threw out of Server::start and the server died (std::terminate).
# Usage: sh build-freebsd/run-dserver-survive-probe.sh      Env: DARLING_SRC_DIR DARLING_OVERLAY DARLING_BUILD_DIR
# Skipped lock check when DARLING_LOCK_HELD is set (the caller holds the run lock). Servers/guests are stopped by pid.
SRC="${DARLING_SRC_DIR:?}"; BD="${DARLING_BUILD_DIR:?}"; OD="${DARLING_OVERLAY:?}"
PATH="/usr/local/bin/:/usr/local/sbin:/usr/bin:/bin:/sbin:/usr/sbin"; export PATH
[ -f "${SRC}/tests/dserver-survive-probe-macho" ] || { echo "FATAL: build it with build-dserver-survive-probe.sh" >&2; exit 2; }
. "$(dirname "$0")/series-guard.sh"; series_guard || exit 2
. "${SRC}/build-freebsd/dserver-prefix.sh"
dsp_new || exit 2
LOG="${BD}/dserver-survive-probe-$(date +%m%d-%H%M%S).log"
: > "$LOG"
n=0; nbad=0; bad=""
chk() { n=$((n+1)); if [ "$2" = ok ]; then :; else nbad=$((nbad+1)); bad="${bad}$1,"; fi; echo "SURVIVE-T $1 $2 $3" >> "$LOG"; }
( sudo timeout -k 5 100 env "DARLING_SRC_DIR=${SRC}" "DARLING_OVERLAY=${OD}" "DARLING_BUILD_DIR=${BD}" "DARLING_PREFIX=${DARLING_PREFIX}" \
    "DARLING_TEST_BINARY=dserver-survive-probe-macho" $([ "${STAGE_GATE_KEEP_STAGED:-}" = 1 ] && echo DARLING_KEEP_LOCAL=1 || echo DARLING_SMOKE_REFRESH=1) \
    "${BD}/launch-dynamic" >>"${LOG}" 2>&1 & )
ticks() { grep -ac "^SURVIVE-GUEST tick" "$LOG"; }
srvpid() { ps -axww -o pid,command | awk -v P="$DARLING_PREFIX" '$2 ~ /(^|\/)darlingserver$/ && $3 == P {printf "%s%s", (n++?",":""), $1}'; }
diag() { echo "SURVIVE-DIAG $1 servers=[$(srvpid)] ticks=$(ticks)" >> "$LOG"; }
i=0; while [ "$i" -lt 60 ] && [ "$(ticks)" -lt 3 ]; do sleep 1; i=$((i+1)); done
SP=$(srvpid)
if [ "$(ticks)" -lt 3 ] || [ -z "$SP" ]; then
	echo "DSERVER-SURVIVE-VERDICT FAIL n=0 bad=incomplete [guest-never-ticked]"; echo "log=$LOG" >&2; exit 1
fi
# malformed datagrams from a bound client socket (the server needs a reply address)
SOCK="$DARLING_PREFIX/.darlingserver.sock"
sudo python3 -I - "$SOCK" "$DARLING_PREFIX" >> "$LOG" 2>&1 <<'PY'
import socket, struct, sys, os
srv, pre = sys.argv[1:3]
me = pre + "/survive-probe-client.sock"
s = socket.socket(socket.AF_UNIX, socket.SOCK_DGRAM)
try: os.unlink(me)
except OSError: pass
s.bind(me)
def send(b):
    try: s.sendto(b, srv)
    except OSError as e: print("SURVIVE-INJECT send failed", e)
send(b"x")                                   # shorter than any call header
send(b"\x00" * 3)
send(struct.pack("<8I", 0x7fffffff, 1, 2, 3, 4, 5, 6, 7) + b"\x00" * 64)   # header-sized, invalid call number
send(struct.pack("<8I", 0, 0xdead, 0xbeef, 0, 0, 0, 0, 0) + b"\x00" * 256)  # invalid number 0, unknown pid/tid
send(os.urandom(200))
print("SURVIVE-INJECT sent 5")
PY
diag after-inject
sleep 1
t0=$(ticks); sleep 3; t1=$(ticks); diag after-wait
if [ -n "$(srvpid)" ] && [ "$t1" -gt "$t0" ]; then chk parse-garbage ok "ticks $t0->$t1 server=$(srvpid)"; else chk parse-garbage bad "ticks $t0->$t1 server=$(srvpid)"; fi
# kill the client while it is mid-call
GP=$(pgrep -f "mldr-real/mldr .*dserver-survive-probe-macho" | head -1)
if [ -n "$GP" ]; then sudo kill -9 "$GP" 2>/dev/null; else chk kill-client bad "no guest pid"; fi
sleep 3
if [ -n "$(srvpid)" ]; then chk kill-client ok "server=$(srvpid) guest_pid=$GP"; else chk kill-client bad "server gone after guest kill"; fi
sudo tail -c 3000 "$DARLING_PREFIX/private/var/log/dserver.log" 2>/dev/null | tail -5 | cut -c1-200 | sed 's/^/SURVIVE-SRVLOG /' >> "$LOG"
echo "SURVIVE-INFO dropped=$(grep -ac "\[dserver\] dropped a call" "$LOG")"
dsp_reap
for g in $(pgrep -f "mldr-real/mldr .*dserver-survive-probe-macho"); do sudo kill -9 "$g" 2>/dev/null; done
echo "DSERVER-SURVIVE-VERDICT $([ "$nbad" = 0 ] && echo pass || echo FAIL) n=$n bad=$nbad [$bad]"
echo "log=$LOG" >&2
[ "$nbad" = 0 ]
