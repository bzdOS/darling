#!/bin/sh
# guest-threads-snapshot.sh PID LOG - stack of every guest thread of a running mldr process.
#   PID  the mldr process (guest Mach-O runs inside it); LOG  the run log (its "[dyld-trace] [i] mh=0x... name" lines
#        map load addresses to images).
# Mechanism: lldb attaches (briefly stops the process), dumps rip/rsp/rbp of each thread and walks the guest
# frame-pointer chain (up to DEPTH, default 40) through process memory, then detaches. Frames print as
# image+offset, plus the nearest export of system dylibs under $DARLING_OVERLAY (llvm-nm) when found.
# SCAN=1 additionally lists words on each stack that point into loaded images (conservative, noisy).
# CORE=<file>[:<mldr path>] reads an (uncompressed) mldr core instead of attaching (PID is then ignored; use 0).
# Output: "THREAD <n> lwp=<id> state=<wchan>" followed by "  #i image+0xoff[=sym]" lines.
# Needs root (sudo) to attach. Exit 0 if at least one thread was dumped.
PID="${1:?usage: guest-threads-snapshot.sh PID LOG}"; LOG="${2:?}"
[ -r "$LOG" ] || { echo "unreadable $LOG" >&2; exit 2; }
DEPTH=${DEPTH:-40}
T=$(mktemp -t gts) || exit 2
trap 'rm -f "$T" "$T.k"' EXIT
# kernel-side wait channel per thread, before the attach
procstat -t "$PID" 2>/dev/null > "$T.k"
LLDB=$(command -v lldb19 || command -v lldb)
cat > "$T.py" <<'PY'
import lldb, struct, sys
p = lldb.debugger.GetSelectedTarget().GetProcess()
import sys
e = lldb.SBError()
def rd(a):
    r = p.ReadMemory(a, 16, e)
    return struct.unpack('<QQ', r) if e.Success() and r and len(r) == 16 else None
import os
depth = int(os.environ.get('DEPTH', '40'))
for t in p:
    f = t.GetFrameAtIndex(0)
    pc = f.GetPC(); rsp = f.FindRegister('rsp').GetValueAsUnsigned(); fp = f.FindRegister('rbp').GetValueAsUnsigned()
    print('TH %d %d' % (t.GetIndexID(), t.GetThreadID()))
    print('FR %d 0 0x%x' % (t.GetIndexID(), pc))
    # leaf may not have pushed rbp: also report the word at rsp as a candidate return address
    w = p.ReadMemory(rsp, 8, e)
    if e.Success() and w: print('FR %d s 0x%x' % (t.GetIndexID(), struct.unpack('<Q', w)[0]))
    if os.environ.get('SCAN', '0') == '1':
        n = int(os.environ.get('SCAN_BYTES', '12288'))
        b = p.ReadMemory(rsp, n, e)
        if not (e.Success() and b):
            b = b''
        for k in range(0, len(b) - 7, 8):
            print('SC %d %d 0x%x' % (t.GetIndexID(), k, struct.unpack_from('<Q', b, k)[0]))
    for i in range(1, depth):
        if fp == 0 or fp & 7: break
        r = rd(fp)
        if not r or r[1] == 0: break
        print('FR %d %d 0x%x' % (t.GetIndexID(), i, r[1]))
        if r[0] <= fp: break
        fp = r[0]
PY
if [ -n "$CORE" ]; then
	CF=${CORE%%:*}; CB=${CORE#*:}; [ "$CB" = "$CORE" ] && CB=${DARLING_BUILD_DIR:?set DARLING_BUILD_DIR or CORE=file:mldr}/dserver/mldr-real/mldr
	OUT=$(env DEPTH=$DEPTH SCAN=${SCAN:-0} SCAN_BYTES=${SCAN_BYTES:-12288} "$LLDB" --batch --no-lldbinit -c "$CF" -o "script exec(open('$T.py').read())" "$CB" 2>&1 </dev/null)
else
	OUT=$(sudo -n env DEPTH=$DEPTH SCAN=${SCAN:-0} SCAN_BYTES=${SCAN_BYTES:-12288} "$LLDB" --batch --no-lldbinit -o "process attach --pid $PID" -o "script exec(open('$T.py').read())" -o "detach" 2>&1 </dev/null)
fi
printf '%s\n' "$OUT" > "$T.raw"
python3 -I - "$LOG" "$T.raw" "$T.k" <<'PY'
import sys, re, os, subprocess
log, raw, kf = sys.argv[1:4]
imgs = {}
for ln in open(log, 'rb').read().decode('latin-1').split('\n'):
    m = re.match(r'\[dyld-trace\]\s+\[\d+\] mh=(0x[0-9a-f]+) (.+?)( <-NEW)?$', ln)
    if m and m.group(2) not in imgs: imgs[m.group(2)] = int(m.group(1), 16)
table = sorted((v, k) for k, v in imgs.items())
def where(a):
    best = None
    for b, n in table:
        if b <= a < b + 0x20000000: best = (n, a - b)
    return best
ov = os.environ.get('DARLING_OVERLAY', '')
cache = {}
def sym(name, off):
    if name not in cache:
        s = []
        for d in ('usr/lib/system', 'usr/lib'):
            p = os.path.join(ov, d, os.path.basename(name))
            if ov and os.path.isfile(p):
                o = subprocess.run(['llvm-nm', '-n', '--defined-only', p], capture_output=True, text=True).stdout
                for l in o.split('\n'):
                    m = re.match(r'([0-9a-f]+) [A-Za-z] (\S+)', l)
                    if m: s.append((int(m.group(1), 16), m.group(2)))
                break
        cache[name] = s
    b = None
    for a, n in cache[name]:
        if a <= off: b = (a, n)
        else: break
    return '=%s+0x%x' % (b[1], off - b[0]) if b and off - b[0] < 0x4000 else ''
wch = {}
for l in open(kf).read().split('\n')[1:]:
    f = l.split()
    if len(f) >= 7: wch[f[1]] = f[-2] if len(f) > 7 else f[-1]
def fmt(a):
    w = where(a)
    if not w: return '0x%x(mldr/native/unmapped)' % a
    n, o = w
    n = n.replace('Google Chrome for Testing Framework', 'ChromeFW')
    return '%s+0x%x%s' % (n, o, sym(n, o) if n != 'ChromeFW' else '')
ths = {}; order = []
for ln in open(raw, errors='replace'):
    m = re.match(r'TH (\d+) (\d+)', ln)
    if m: ths[m.group(1)] = {'lwp': m.group(2), 'fr': []}; order.append(m.group(1))
    m = re.match(r'FR (\d+) (\w+) (0x[0-9a-f]+)', ln)
    if m and m.group(1) in ths: ths[m.group(1)]['fr'].append((m.group(2), int(m.group(3), 16)))
    m = re.match(r'SC (\d+) (\d+) (0x[0-9a-f]+)', ln)
    if m and m.group(1) in ths and where(int(m.group(3), 16)): ths[m.group(1)].setdefault('sc', []).append((m.group(2), int(m.group(3), 16)))
if not ths:
    print('SNAPSHOT no threads (attach failed?)'); print(open(raw, errors='replace').read()[-600:]); sys.exit(1)
print('SNAPSHOT threads=%d' % len(ths))
for k in order:
    t = ths[k]
    print('THREAD %s lwp=%s state=%s' % (k, t['lwp'], wch.get(t['lwp'], '?')))
    for tag, a in t['fr']:
        print('  #%s %s' % (tag, fmt(a)))
    sc = t.get('sc', [])
    if sc:
        print('  stack-scan (words at rsp+off that point into guest images, first 48):')
        for off, a in sc[:48]:
            print('    +%s %s' % (off, fmt(a)))
PY
