#!/usr/bin/env python3
"""repoint-real-providers.py — move framework imports off supplement stubs
and onto genuine providers where the closure has one.

For every chained-fixups import whose lib_ordinal is the supplement's, search
the closure's images (the load list plus the runtime images that provide
builtins) for a SAME-NAME genuine export (nm 'TDBRWS' on the thin x86_64
slice); repoint those imports to that provider's load ordinal. Names whose
implementation lives under a VARIANT spelling (e.g. _platform_memset_pattern16
for _memset_pattern16) are NOT matched here — that family needs a
transformation pass, which is the caller's next step.

Usage: repoint-real-providers.py <framework> [--list-only]
"""
import struct
import sys
import os
import subprocess

OV = os.environ.get("DARLING_OVERLAY")
if not OV:
    raise SystemExit("set DARLING_OVERLAY to your overlay dir")
SUPPL_ORDINAL = 67


def load_cmds(d):
    n = struct.unpack_from("<I", d, 16)[0]
    off = 32
    cf = None
    loads = {}
    k = 0
    for _ in range(n):
        cmd, cs = struct.unpack_from("<II", d, off)
        if cmd in (0x0C, 0x80000018, 0x8000001F, 0x80000022):
            no = struct.unpack_from("<I", d, off + 8)[0]
            s = d.index(b"\0", off + no)
            k += 1
            loads[k] = d[off + no:s].decode("utf-8", "replace")
        if cmd == 0x80000034:
            cf = struct.unpack_from("<II", d, off + 8)[0]
        off += cs
    return cf, loads


def thin_exports(path):
    if not os.path.exists(path):
        return set()
    dd = open(path, "rb").read()
    if dd[:4] == b"\xca\xfe\xba\xbe":
        nf = struct.unpack_from(">I", dd, 4)[0]
        for i in range(nf):
            ct, cs, o, size, al = struct.unpack_from(">iiIII", dd, 8 + i * 20)
            if ct == 0x01000007:
                dd = dd[o:o + size]
                break
    open("/tmp/timg-repoint", "wb").write(dd)
    r = subprocess.run(["llvm-nm", "-g", "/tmp/timg-repoint"], capture_output=True, text=True)
    return set(l.split()[2] for l in r.stdout.splitlines()
               if len(l.split()) >= 3 and l.split()[1] in "TDBRWS")


def main():
    fw = sys.argv[1]
    list_only = "--list-only" in sys.argv
    d = open(fw, "rb").read()
    cf, loads = load_cmds(d)
    h = cf
    ver, starts, imp_off, sym_off, cnt, fmt_, sfmt = struct.unpack_from("<IIIIIII", d, h)
    prov = {}
    for o, p in loads.items():
        if p.startswith("/"):
            prov[o] = thin_exports(OV + p)
    for extra in ("usr/lib/system/libsystem_platform.dylib",
                  "usr/lib/system/libcompiler_rt.dylib",
                  "usr/lib/libc++.1.dylib", "usr/lib/libc++abi.dylib"):
        prov[-len(prov) - 1] = thin_exports(OV + "/" + extra)
    plan = {}
    for i in range(cnt):
        e = h + imp_off + i * 4
        v = struct.unpack_from("<I", d, e)[0]
        lib = v & 0xFF
        no = v >> 9
        s = h + sym_off + no
        nm = bytes(d[s:d.index(b"\0", s)]).decode("utf-8", "replace")
        if lib != SUPPL_ORDINAL or nm.startswith("_OBJC_"):
            continue
        for o, names in prov.items():
            if o > 0 and nm in names:
                plan[nm] = o
                break
    print(f"stubbed imports with a same-name genuine provider: {len(plan)}")
    if list_only:
        for nm, o in sorted(plan.items()):
            print(f"  {nm} -> ordinal {o}")
        return
    d = bytearray(d)
    moved = 0
    for i in range(cnt):
        e = h + imp_off + i * 4
        v = struct.unpack_from("<I", d, e)[0]
        lib = v & 0xFF
        weak = (v >> 8) & 1
        no = v >> 9
        s = h + sym_off + no
        nm = bytes(d[s:d.index(b"\0", s)]).decode("utf-8", "replace")
        if nm in plan:
            struct.pack_into("<I", d, e, (lib & ~0xFF) | plan[nm] | (weak << 8) | (no << 9))
            moved += 1
    open(fw, "wb").write(bytes(d))
    print(f"repointed: {moved}")


if __name__ == "__main__":
    main()
