#!/usr/bin/env python3
"""chrome-imports-by-ordinal.py — per-ordinal import inventory for a
chained-fixups Mach-O (the Chrome framework).

Parses LC_LOAD_DYLIB/LC_LOAD_WEAK_DYLIB/LC_REEXPORT_DYLIB/LC_LOAD_UPWARD_DYLIB
in load-command order (1-based ordinals, as dyld bind opcodes use) and the
LC_DYLD_CHAINED_FIXUPS imports table; prints each ordinal's dylib path and
its imported symbol list, then diffs a chosen ordinal's symbols against an
export list (nm -gU output) to produce the missing set.

Usage:
  chrome-imports-by-ordinal.py <macho>                       # dump ordinal table
  chrome-imports-by-ordinal.py <macho> --missing <ordinal> <exports.txt>
"""
import struct
import subprocess
import sys

LC_LOAD_DYLIB = 0xC
LC_ID_DYLIB = 0xD
LC_LOAD_WEAK = 0x80000018
LC_REEXPORT = 0x8000001F
LC_UPWARD = 0x80000023
LC_SEGMENT_64 = 0x19
LC_DYLD_CHAINED_FIXUPS = 0x80000034
DYLIB_LCS = (LC_LOAD_DYLIB, LC_LOAD_WEAK, LC_REEXPORT, LC_UPWARD)


def load_dylibs(d, ncmds):
    off = 32
    libs = []
    for _ in range(ncmds):
        cmd, cs = struct.unpack_from("<II", d, off)
        if cmd in DYLIB_LCS:
            stroff = struct.unpack_from("<I", d, off + 8)[0]
            name = d[off + stroff:off + cs].split(b"\0")[0].decode(errors="replace")
            libs.append((cmd, name))
        off += cs
    return libs


def imports(d, ncmds):
    off = 32
    cf = None
    for _ in range(ncmds):
        cmd, cs = struct.unpack_from("<II", d, off)
        if cmd == LC_DYLD_CHAINED_FIXUPS:
            dataoff, datasize = struct.unpack_from("<II", d, off + 8)
            cf = (dataoff, datasize)
        off += cs
    if cf is None:
        return None
    h = cf[0]
    _ver, _so, imports_off, symbols_off, count, ifmt, _sfmt = struct.unpack_from(
        "<IIIIIII", d, h)
    ent = {1: 4, 2: 8, 3: 12}[ifmt]
    out = []
    for i in range(count):
        io = h + imports_off + i * ent
        v = struct.unpack_from("<I", d, io)[0]
        # Empirically validated against this toolchain's output: every one of
        # the 2696 imports resolves to a name in the binary's nm -u set with
        # lib_ordinal in bits 0-7, a flag at bit 8, name_offset at bit 9.
        lo = v & 0xFF
        no = (v >> 9) & 0x7FFFFF
        s = h + symbols_off + no
        e = d.find(b"\0", s)
        out.append((lo, d[s:e].decode(errors="replace")))
    return out


def main():
    path = sys.argv[1]
    d = open(path, "rb").read()
    assert d[:4] == b"\xcf\xfa\xed\xfe", "thin MH_MAGIC_64 expected"
    ncmds = struct.unpack_from("<I", d, 16)[0]
    libs = load_dylibs(d, ncmds)
    imps = imports(d, ncmds)
    if imps is None:
        print("no LC_DYLD_CHAINED_FIXUPS")
        return
    print(f"dylib load commands ({len(libs)}):")
    for i, (cmd, name) in enumerate(libs, 1):
        print(f"  ordinal {i:3d} cmd={cmd:#x} {name}")
    by_ord = {}
    for lo, nm in imps:
        by_ord.setdefault(lo, []).append(nm)
    print(f"\nimports total={len(imps)} distinct-ordinals={len(by_ord)}")
    if "--missing" in sys.argv:
        k = sys.argv.index("--missing")
        ordinal = int(sys.argv[k + 1])
        exports_path = sys.argv[k + 2]
        want = sorted(by_ord.get(ordinal, []))
        have = set()
        for line in open(exports_path):
            parts = line.split()
            if len(parts) >= 3:
                have.add(parts[2])
            elif len(parts) == 2 and parts[0] in ("T", "t", "D", "d", "S", "s", "C"):
                have.add(parts[1])
        missing = [s for s in want if s not in have]
        print(f"\nordinal {ordinal} -> {libs[ordinal - 1][1]}")
        print(f"  imports: {len(want)}; exported by stub: {len(want) - len(missing)}; MISSING: {len(missing)}")
        for s in missing:
            print(f"    {s}")
        with open(exports_path + ".missing", "w") as f:
            f.write("\n".join(missing) + "\n")
        print(f"\nmissing list written to {exports_path}.missing")


if __name__ == "__main__":
    main()
