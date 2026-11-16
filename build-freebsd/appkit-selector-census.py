#!/usr/bin/env python3
"""appkit-selector-census.py - selectors a Mach-O sends that nothing in the overlay implements.

Usage: appkit-selector-census.py CHROME_FRAMEWORK_BINARY OVERLAY [--list] [--grep REGEX]
  sent     = selectors in the image's __objc_selrefs (every selector the image sends or defines)
  own      = selectors the image itself defines (method lists of its classes/categories, __objc_methlist)
  provided = every selector name string (__objc_methname) of the overlay frameworks and dylibs the image
             can reach (AppKit, Foundation, CoreFoundation, CoreGraphics, CoreText, QuartzCore, ... , usr/lib)
  missing  = sent - own - provided
A name in a provider's __objc_methname is a defined or a referenced selector, so `provided` over-approximates
and `missing` is a lower bound: every name listed is certainly implemented by no overlay image.
Output: one summary line  CENSUS sent=N own=N provided=N missing=N ; with --list the names, one per line.
Reads only the sections it needs (the framework is ~600 MB). Exit 0.
"""
import os
import re
import struct
import sys


def sections(f):
    f.seek(0)
    hd = f.read(32)
    if len(hd) < 32:
        return {}
    magic, _c, _s, _t, ncmds, sz, _fl, _r = struct.unpack('<8I', hd)
    if magic != 0xfeedfacf:
        return {}
    d = f.read(sz)
    out = {}
    o = 0
    for _ in range(ncmds):
        c, s = struct.unpack_from('<2I', d, o)
        if c == 0x19:
            ns = struct.unpack_from('<I', d, o + 64)[0]
            p = o + 72
            for _j in range(ns):
                sn = d[p:p + 16].rstrip(b'\0').decode()
                a, sz2, off = struct.unpack_from('<2QI', d, p + 32)
                out[sn] = (a, sz2, off)
                p += 80
        o += s
    return out


def rd(f, off, n):
    f.seek(off)
    return f.read(n)


def methnames(path):
    try:
        if path.endswith('Extras.dylib'):
            # behaviour code registers its selectors by name (sel_registerName("x")): every C string counts
            with open(path, 'rb') as f:
                raw = f.read()
            return {x.decode('latin-1') for x in raw.split(b'\0') if 2 < len(x) < 200 and
                    re.fullmatch(rb'[A-Za-z_][A-Za-z0-9_:]*', x)} | methnames_macho(path)
        return methnames_macho(path)
    except OSError:
        return set()


def methnames_macho(path):
    try:
        with open(path, 'rb') as f:
            sec = sections(f)
            if '__objc_methname' not in sec:
                return set()
            a, n, off = sec['__objc_methname']
            return {x.decode('latin-1') for x in rd(f, off, n).split(b'\0') if x}
    except OSError:
        return set()


def image_sent_and_own(path):
    f = open(path, 'rb')
    sec = sections(f)
    ma, mn, mo = sec['__objc_methname']
    names = rd(f, mo, mn)

    def cstr(vm):
        i = vm - ma
        if i < 0 or i >= mn:
            return None
        e = names.find(b'\0', i)
        return names[i:e].decode('latin-1')
    sa, sn, so = sec['__objc_selrefs']
    sent = set()
    refs = {}
    raw = rd(f, so, sn)
    for i in range(0, sn, 8):
        v = struct.unpack_from('<Q', raw, i)[0] & 0xFFFFFFFFF
        s = cstr(v)
        if s is not None:
            sent.add(s)
            refs[sa + i] = s
    own = set()
    if '__objc_methlist' in sec:
        la, ln, lo = sec['__objc_methlist']
        raw = rd(f, lo, ln)
        p = 0
        while p + 8 <= ln:
            esz, cnt = struct.unpack_from('<2I', raw, p)
            if esz & 0x80000000 == 0 or (esz & 0xffff) != 12 or cnt > 100000:
                p += 4
                continue
            q = p + 8
            for _ in range(cnt):
                noff = struct.unpack_from('<i', raw, q)[0]
                tgt = la + q + noff
                s = refs.get(tgt) or cstr(tgt)
                if s:
                    own.add(s)
                q += 12
            p = (q + 3) & ~3
    return sent, own


def provider_files(overlay):
    for root in ('System/Library/Frameworks', 'System/Library/PrivateFrameworks', 'usr/lib'):
        for dp, _dn, fn in os.walk(os.path.join(overlay, root)):
            for x in fn:
                p = os.path.join(dp, x)
                if os.path.islink(p) or '.pre-' in x or x.endswith(('.bak', '.h', '.plist')):
                    continue
                yield p


def main():
    args = [a for a in sys.argv[1:] if not a.startswith('--')]
    chrome, overlay = args[0], args[1]
    sent, own = image_sent_and_own(chrome)
    prov = set()
    seen = set()
    for p in provider_files(overlay):
        rp = os.path.realpath(p)
        if rp in seen:
            continue
        seen.add(rp)
        prov |= methnames(rp)
    missing = sorted(sent - own - prov)
    rx = None
    if '--grep' in sys.argv:
        rx = re.compile(sys.argv[sys.argv.index('--grep') + 1])
        missing = [m for m in missing if rx.search(m)]
    print('CENSUS sent=%d own=%d provided=%d missing=%d' % (len(sent), len(own), len(prov), len(missing)))
    if '--list' in sys.argv:
        print('\n'.join(missing))


if __name__ == '__main__':
    main()
