#!/usr/bin/env python3
"""fat-wrap.py — wrap a thin x86_64 Mach-O in a FAT container matching a stock layout.

Usage: fat-wrap.py <stock-fat> <thin-x86_64> <out>

The container reproduces the stock arch table exactly (cputype, cpusubtype,
offset, align of both entries): the x86_64 slot keeps the stock offset and
holds the thin rebuild slice; the i386 slot keeps the stock offset and holds
the i386 slice copied byte-for-byte from the stock container. Slice sizes
are the true sizes. Prints the resulting arch table and both slice LC_UUIDs.
"""
import struct
import sys
import uuid


def slice_uuid(data, base):
    mb = bytes(data[base:base + 4])
    if mb == b"\xcf\xfa\xed\xfe":
        lc0 = base + 32
    elif mb == b"\xce\xfa\xed\xfe":
        lc0 = base + 28
    else:
        return f"unexpected slice magic {mb.hex()}"
    ncmds = struct.unpack_from("<I", data, base + 16)[0]
    o = lc0
    for _ in range(ncmds):
        cmd, cmdsize = struct.unpack_from("<II", data, o)
        if cmd == 0x1b and cmdsize >= 24:
            return str(uuid.UUID(bytes_le=bytes(data[o + 8:o + 24]))).upper()
        o += cmdsize
    return None


def main():
    stock_p, thin_p, out_p = sys.argv[1], sys.argv[2], sys.argv[3]
    stock = open(stock_p, "rb").read()
    thin = open(thin_p, "rb").read()
    if struct.unpack_from(">I", stock, 0)[0] != 0xcafebabe:
        raise SystemExit("stock is not a FAT container")
    if struct.unpack_from("<I", thin, 0)[0] != 0xfeedfacf:
        raise SystemExit("thin input is not a thin x86_64 Mach-O")
    n = struct.unpack_from(">I", stock, 4)[0]
    arch = []
    for s in range(n):
        off = 8 + s * 20
        ct, cs, offset, sz, al = struct.unpack_from(">IIIII", stock, off)
        arch.append({"cputype": ct, "cpusubtype": cs, "offset": offset,
                     "size": sz, "align": al})
        if struct.unpack_from("<I", stock, offset)[0] != (0xfeedfacf if ct == 0x1000007 else 0xfeedface):
            raise SystemExit(f"stock arch[{s}] slice magic unexpected")
    a64 = next(a for a in arch if a["cputype"] == 0x1000007)
    a32 = next(a for a in arch if a["cputype"] == 0x7)
    i386_slice = stock[a32["offset"]:a32["offset"] + a32["size"]]
    if len(i386_slice) != a32["size"]:
        raise SystemExit("stock i386 slice truncated")
    x64_slice = thin
    if len(x64_slice) >= a32["offset"] - a64["offset"]:
        raise SystemExit("thin slice does not fit the stock x86_64 slot")
    out = bytearray(a32["offset"] + len(i386_slice))
    struct.pack_into(">II", out, 0, 0xcafebabe, len(arch))
    for s, (a, sl) in enumerate(((a64, x64_slice), (a32, i386_slice))):
        off = 8 + s * 20
        struct.pack_into(">IIIII", out, off, a["cputype"], a["cpusubtype"],
                         a["offset"], len(sl), a["align"])
        out[a["offset"]:a["offset"] + len(sl)] = sl
    open(out_p, "wb").write(out)
    data = open(out_p, "rb").read()
    print(f"wrote {out_p}: size={len(data)}")
    n2 = struct.unpack_from(">I", data, 4)[0]
    for s in range(n2):
        ct, cs, offset, sz, al = struct.unpack_from(">IIIII", data, 8 + s * 20)
        u = slice_uuid(data, offset)
        print(f"  arch[{s}] cputype={ct:#x} cpusubtype={cs:#x} offset={offset:#x} "
              f"size={sz} align={al} LC_UUID={u}")


if __name__ == "__main__":
    main()
