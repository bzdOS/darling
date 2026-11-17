#!/usr/bin/env python3
"""data-shadow-census.py -- data symbols (constants) that an injected image exports zero filled
on top of a real definition in another overlay image, and the value check behind CATX-KEYS.

The injected image (darling-extras.dylib) is first in the flat lookup order, so a dlsym(RTLD_DEFAULT)
or a flat bind finds its zero object before the framework's real one.

Usage:
  data-shadow-census.py census [--overlay DIR] [--image usr/lib/darling-extras.dylib]
      one line per symbol: <symbol> <image holding the real definition> [<more images>]
      only data symbols whose object in the injected image is zero (no bytes, no rebase, no bind)
      and whose provider holds a non-zero object (bytes, rebase or bind).
  data-shadow-census.py value IMAGE SYMBOL
      prints "non-null" or "null" for the first pointer of the exported object (a rebase or a bind
      at that address counts as non-null), "absent" if IMAGE does not export SYMBOL.
Needs llvm-nm and llvm-objdump. Reads images only through those tools and seek-sized reads.
"""
import argparse
import os
import re
import struct
import subprocess
import sys

DEFAULT_OV = os.environ.get("DARLING_OVERLAY", "/opt/darling/overlay")
TREES = ("System/Library/Frameworks", "System/Library/PrivateFrameworks", "usr/lib")
MAX_SIZE = 200 * 1024 * 1024


def run(*a):
    return subprocess.run(a, capture_output=True, text=True).stdout


def is_macho64(path):
    try:
        with open(path, "rb") as f:
            m = f.read(4)
        return m == b"\xcf\xfa\xed\xfe"
    except OSError:
        return False


def data_exports(path):
    """{name: vmaddr} of the externally defined symbols living in a __DATA* segment"""
    out = {}
    for l in run("llvm-nm", "-m", "--defined-only", "-U", path).splitlines():
        m = re.match(r"^([0-9a-f]+) \((__DATA\w*),(\S+)\) external (\S+)", l)
        if m:
            out[m.group(4)] = int(m.group(1), 16)
    return out


def sections(path):
    """[(vmaddr, size, fileoff)] of the 64 bit Mach-O sections"""
    with open(path, "rb") as f:
        hdr = f.read(32)
        ncmds = struct.unpack_from("<I", hdr, 16)[0]
        cmds = f.read(struct.unpack_from("<I", hdr, 20)[0])
    out, off = [], 0
    for _ in range(ncmds):
        cmd, size = struct.unpack_from("<II", cmds, off)
        if cmd == 0x19:
            nsects = struct.unpack_from("<I", cmds, off + 64)[0]
            for i in range(nsects):
                s = off + 72 + i * 80
                addr, sz, foff = struct.unpack_from("<QQI", cmds, s + 32)
                out.append((addr, sz, foff))
        off += size
    return out


def fixup_addrs(path):
    """addresses covered by a rebase or a bind (opcode based images)"""
    addrs = set()
    for flag in ("--rebase", "--bind", "--weak-bind"):
        for l in run("llvm-objdump", "--macho", flag, path).splitlines():
            m = re.search(r"\b0x([0-9A-Fa-f]{6,})\b", l)
            if m:
                addrs.add(int(m.group(1), 16))
    return addrs


def is_null(path, addr, secs, fixups):
    if addr in fixups:
        return False
    for a, sz, foff in secs:
        if a <= addr < a + sz:
            with open(path, "rb") as f:
                f.seek(foff + addr - a)
                return f.read(8).strip(b"\0") == b""
    return True


def images(ov):
    for t in TREES:
        for d, _, fs in os.walk(os.path.join(ov, t)):
            if d[len(ov):].count("/") > 9:
                continue
            for fn in fs:
                p = os.path.join(d, fn)
                if os.path.islink(p) or ".pre-" in fn or ".bak" in fn or fn.endswith((".objc", ".py", ".sh", ".a")):
                    continue
                try:
                    if os.path.getsize(p) > MAX_SIZE:
                        continue
                except OSError:
                    continue
                if is_macho64(p):
                    yield p


def main():
    ap = argparse.ArgumentParser()
    sub = ap.add_subparsers(dest="cmd", required=True)
    c = sub.add_parser("census")
    c.add_argument("--overlay", default=DEFAULT_OV)
    c.add_argument("--image", default="usr/lib/darling-extras.dylib")
    v = sub.add_parser("value")
    v.add_argument("image")
    v.add_argument("symbol")
    a = ap.parse_args()
    if a.cmd == "value":
        ex = data_exports(a.image)
        if a.symbol not in ex:
            print("absent")
            return 2
        print("null" if is_null(a.image, ex[a.symbol], sections(a.image), fixup_addrs(a.image)) else "non-null")
        return 0
    ov = a.overlay.rstrip("/")
    inj = os.path.join(ov, a.image)
    iex = data_exports(inj)
    secs, fx = sections(inj), fixup_addrs(inj)
    zero = {n for n, ad in iex.items() if is_null(inj, ad, secs, fx)}
    real = {}
    for p in images(ov):
        if os.path.realpath(p) == os.path.realpath(inj):
            continue
        pex = data_exports(p)
        hit = [n for n in pex if n in zero]
        if not hit:
            continue
        # a provider counts when its own object is not zero (a zero stub is no real definition)
        psecs, pfx = sections(p), fixup_addrs(p)
        for n in hit:
            if not is_null(p, pex[n], psecs, pfx):
                real.setdefault(n, []).append(os.path.relpath(p, ov))
    for n in sorted(real):
        print("%s %s" % (n, " ".join(real[n])))
    print("# %d zero data exports in %s, %d with a real definition elsewhere" % (len(zero), a.image, len(real)),
          file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())
