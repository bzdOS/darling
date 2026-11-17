#!/bin/sh
# guest-abort-symbolize.sh LOG - one line for the guest-side abort of a Chrome run.
#   ABORT <tag> rc=<rc> frames=<image+off[=sym]> > ... str=<text the abort passed> file=<nearby source path strings> stderr=<last guest stderr lines>
# Input: the log of a run with MLDR_ABORT_TRACE=1 (mldr prints ABORT-TRACE/ABORT-FRAME/ABORT-STR lines at the
# kill(self, SIGABRT)); without them the "fatal stack" lines of the FATAL signal block are used instead.
# Image names and load addresses come from the log's "[dyld-trace]   [i] mh=0x... name" table. Chrome's own
# framework is stripped (three exports), so a frame gets the nearest export plus the C strings that the code just
# before the return address loads (llvm-objdump -d on the framework: rip-relative references into __cstring), which
# for a CHECK failure is the source path of the LogMessage.
# Env: CHROME_APP (host path of the .app) for the disassembly; DARLING_OVERLAY for the nearest symbol of system dylibs; ABORT_FRAMES (default 8) frames to annotate.
# Exit 0 when a stack was found, 1 otherwise.
L="${1:?usage: guest-abort-symbolize.sh LOG}"
[ -r "$L" ] || { echo "ABORT $L unreadable" >&2; exit 2; }
exec python3 -I - "$L" <<'PY'
import sys, re, os, subprocess
log = sys.argv[1]
tag = os.path.basename(log)[:-4].split('-')[-1] if log.endswith('.log') else log
raw = open(log, 'rb').read().decode('latin-1').split('\n')
rc = ''
imgs = {}
for ln in raw:
    m = re.match(r'\[dyld-trace\]\s+\[\d+\] mh=(0x[0-9a-f]+) (.+?)( <-NEW)?$', ln)
    if m and m.group(2) not in imgs:
        imgs[m.group(2)] = int(m.group(1), 16)
    m = re.match(r'WRAPPER_RC=(\d+)', ln)
    if m: rc = m.group(1)
table = sorted((v, k) for k, v in imgs.items())
def where(a):
    best = None
    for base, name in table:
        if base <= a < base + 0x20000000: best = (name, a - base)
    return best
frames = []; ai = None; strs = []
for i, ln in enumerate(raw):
    if 'ABORT-TRACE' in ln and ai is None: ai = i
for ln in raw[ai or 0:]:
    if frames and 'ABORT-TRACE' in ln: break
    m = re.search(r'ABORT-FRAME \d+ (0x[0-9a-f]+)', ln)
    if m: frames.append(('raw', int(m.group(1), 16)))
    m = re.search(r'ABORT-STR (\S+)=0x[0-9a-f]+ "(.*)"', ln)
    if m: strs.append('%s:%s' % (m.group(1), m.group(2)[:200]))
if not frames:
    fl = next((i for i, ln in enumerate(raw) if 'FATAL signal' in ln), None)
    if fl is not None:
        for ln in raw[fl:fl + 60]:
            m = re.match(r'\[darling-mldr\]   at (.+) rip=(0x[0-9a-f]+)', ln)
            if m: frames.append(('raw', int(m.group(2), 16)))
if not frames:
    # SIGTRAP/SEGV delivered to the guest: the faulting rip, then the chain of return addresses
    for ln in raw:
        m = re.search(r'SIGTRAP-AUDIT-DELIVER:.* rip=(0x[0-9a-f]+)', ln)
        if m and not frames: frames.append(('raw', int(m.group(1), 16)))
        m = re.search(r'SIGTRAP-AUDIT-CHAIN: #\d+ ret=(0x[0-9a-f]+)', ln)
        if m and frames: frames.append(('raw', int(m.group(1), 16)))
if not frames:
    print('ABORT %s rc=%s none(no abort or fatal stack in log)' % (tag, rc)); sys.exit(1)
app = os.environ.get('CHROME_APP', '')
fw = None
if app:
    fw = os.path.join(app, 'Contents/Frameworks/Google Chrome for Testing Framework.framework/Versions/Current/Google Chrome for Testing Framework')
    if not os.path.exists(fw): fw = None
segs = []
if fw:
    import struct
    with open(fw, 'rb') as f:
        h = f.read(32); nc = struct.unpack('<I', h[16:20])[0]; cmds = f.read(struct.unpack('<I', h[20:24])[0])
    o = 0
    for _ in range(nc):
        c, sz = struct.unpack('<II', cmds[o:o + 8])
        if c == 0x19:
            vm, vs, fo, fs = struct.unpack('<QQQQ', cmds[o + 24:o + 56])
            segs.append((vm, vs, fo, fs))
        o += sz
def readstr(a):
    for vm, vs, fo, fs in segs:
        if vm <= a < vm + min(vs, fs):
            with open(fw, 'rb') as f:
                f.seek(fo + a - vm); return f.read(120).split(b'\0')[0]
    return b''
def cstrings(off):
    out = []
    try:
        d = subprocess.run(['llvm-objdump', '-d', '--no-show-raw-insn', '--start-address=%d' % (off - 0x90), '--stop-address=%d' % off, fw],
                           capture_output=True, text=True, timeout=60).stdout
    except Exception:
        return out
    for m in re.finditer(r'## 0x([0-9a-f]+)', d):
        t = readstr(int(m.group(1), 16))
        if len(t) >= 4 and all(32 <= c < 127 for c in t): out.append(t.decode()[:100])
    return out
ov = os.environ.get('DARLING_OVERLAY', '')
symcache = {}
def nearest(name, off):
    """nearest preceding symbol of a system dylib found under the overlay (usr/lib, usr/lib/system, frameworks)"""
    if name not in symcache:
        syms = []
        for d in ('usr/lib/system', 'usr/lib'):
            p = os.path.join(ov, d, name)
            if ov and os.path.isfile(p):
                o = subprocess.run(['llvm-nm', '-n', '--defined-only', p], capture_output=True, text=True).stdout
                for l in o.split('\n'):
                    m = re.match(r'([0-9a-f]+) [A-Za-z] (\S+)', l)
                    if m: syms.append((int(m.group(1), 16), m.group(2)))
                break
        symcache[name] = syms
    best = None
    for a, n in symcache[name]:
        if a <= off: best = (a, n)
        else: break
    return '%s+0x%x' % (best[1], off - best[0]) if best and off - best[0] < 0x4000 else ''
nf = int(os.environ.get('ABORT_FRAMES', '8'))
parts = []; files = []
for n, (_, a) in enumerate(frames):
    w = where(a)
    if w is None:
        if n < 4 or a < 0x10000000: parts.append('0x%x(mldr/unmapped)' % a)
        continue
    name, off = w
    short = name.replace('Google Chrome for Testing Framework', 'ChromeFW')
    s = '%s+0x%x' % (short, off)
    if short == 'ChromeFW' and fw and len(files) < nf:
        cs = cstrings(off)
        if cs: s += '[%s]' % '|'.join(cs[-3:]); files += [c for c in cs if '/' in c or '.cc' in c]
    elif short != 'ChromeFW':
        sy = nearest(name, off)
        if sy: s += '=' + sy
    parts.append(s)
# last guest stderr lines before the abort (skip the mldr/traplog/dyld-trace chatter)
end = ai if ai is not None else next((i for i, ln in enumerate(raw) if 'FATAL signal' in ln), len(raw))
noise = re.compile(r'^\[(traplog|darling-mldr|dyld-trace|launch)\]|^WRAPPER_|^SNAP|^sh: ')
tail = [ln.strip()[:160] for ln in raw[:end] if ln.strip() and not noise.match(ln)][-20:]
keyimg = ('chrome-macho', 'Google Chrome for Testing Framework', 'libsystem_kernel.dylib', 'dyld', 'libdispatch.dylib', 'libsystem_c.dylib', 'libsystem_pthread.dylib', 'CoreFoundation', 'Foundation', 'AppKit')
print('IMAGES %s ' % tag + ' '.join('%s@0x%x' % (k.replace('Google Chrome for Testing Framework', 'ChromeFW'), v) for k, v in imgs.items() if k in keyimg or k.split('/')[-1] in keyimg))
print('ABORT %s rc=%s frames=%s str=%s stderr=<%s>' % (tag, rc, ' > '.join(parts), ' ; '.join(strs) or 'none', ' / '.join(tail)))
PY
