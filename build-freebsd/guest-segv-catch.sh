#!/bin/sh
# guest-segv-catch.sh PID LOG  - capture the SIGSEGV/SIGBUS that hits a guest process and print one verdict line.
# Mechanism: a dtrace proc:::signal-send probe filtered on PID. A SIGSEGV/SIGBUS is raised synchronously on the faulting
#   thread, so at the probe the current thread IS the faulting one: its user rip/rsp/rbp, the fault address
#   (td_frame->tf_addr) and the user stack (ustack) are the fault context, taken BEFORE any handler or default action runs.
#   Nothing is attached with ptrace, so the guest's SIGILL syscall trap and timing are not disturbed (an lldb attach was
#   tried first and changed the run: every run died in the SIGILL trap instead). A second SIGSEGV with self=0 is a
#   thr_kill/raise from the process itself, i.e. a re-raise after a handler ran.
# PID: the mldr process ("GUEST-EXEC pid=" in the launcher log). LOG: the run log; its "[dyld-trace] [i] mh=0x... name"
#   lines map addresses to images. The probe stops when PID exits.
# Env: DEPTH (frames, 24), DARLING_OVERLAY unused. Needs root (dtrace). Raw dtrace output kept in SEGV_CATCH_OUT if set.
# Output: "SEGV-STOP n tid=.. sig=.. self=.. rip=.. rsp=.. addr=.." + "  #i module+0xoff" lines, then
#   "SEGV-VERDICT caught=<0|1> stops=<n> tid=.. sig=.. rip=<module+off|native:0x..> rsp=.. addr=.. frames=<n>: f0 < f1 < ..."
#   (the verdict describes the FIRST stop with self=1: the original fault, not the re-raise).
# Exit 0 if caught=1.
PID="${1:?usage: guest-segv-catch.sh PID LOG}"; LOG="${2:?}"
[ -r "$LOG" ] || { echo "unreadable $LOG" >&2; exit 2; }
D=$(cd "$(dirname "$0")" && pwd)
T=$(mktemp -t gsc) || exit 2
RAW=${SEGV_CATCH_OUT:-$T.raw}
trap 'rm -f "$T" "$T.raw"' EXIT
sudo -n dtrace -q -x bufsize=4m -s "$D/guest-segv-catch.d" "$PID" > "$RAW" 2>&1 </dev/null
python3 -I - "$LOG" "$RAW" "${DEPTH:-24}" <<'PY'
import sys, re
log, raw, depth = sys.argv[1], sys.argv[2], int(sys.argv[3])
imgs = {}
for ln in open(log, 'rb').read().decode('latin-1').split('\n'):
    m = re.match(r'\[dyld-trace\]\s+\[\d+\] mh=(0x[0-9a-f]+) (.+?)( <-NEW)?$', ln)
    if m and m.group(2) not in imgs: imgs[m.group(2)] = int(m.group(1), 16)
table = sorted((v, k) for k, v in imgs.items())
def fmt(a):
    best = None
    for b, n in table:
        if b <= a < b + 0x20000000: best = (n, a - b)
    if not best: return 'native:0x%x' % a
    return '%s+0x%x' % (best[0].replace('Google Chrome for Testing Framework', 'ChromeFW'), best[1])
stops = []; cur = None
for ln in open(raw, errors='replace'):
    ln = ln.rstrip('\n')
    m = re.match(r'SEGVD (.*)', ln)
    if m and not ln.startswith('SEGVD-END'):
        cur = {'h': dict(kv.split('=', 1) for kv in m.group(1).split()), 'fr': []}; stops.append(cur); continue
    m = re.match(r'\s+(0x[0-9a-f]+)\s*$', ln)
    if m and cur is not None and len(cur['fr']) < depth: cur['fr'].append(int(m.group(1), 16))
for i, s in enumerate(stops, 1):
    h = s['h']
    print('SEGV-STOP %d tid=%s sig=%s self=%s rip=%s rsp=%s addr=%s' % (i, h['tid'], h['sig'], h['self'], fmt(int(h['rip'], 16)), h['rsp'], h['addr']))
    for j, a in enumerate(s['fr']): print('  #%d %s' % (j, fmt(a)))
first = [s for s in stops if s['h'].get('self') == '1']
if not first:
    print('SEGV-VERDICT caught=0 stops=%d no synchronous SIGSEGV/SIGBUS seen (dtrace: %s)' % (len(stops), open(raw, errors='replace').read().strip()[-150:].replace('\n', ' | ')))
    sys.exit(1)
s = first[0]; h = s['h']
print('SEGV-VERDICT caught=1 stops=%d tid=%s sig=%s rip=%s rsp=%s addr=%s frames=%d: %s' % (len(stops), h['tid'], h['sig'], fmt(int(h['rip'], 16)), h['rsp'], h['addr'], len(s['fr']), ' < '.join(fmt(a) for a in s['fr'])))
PY
