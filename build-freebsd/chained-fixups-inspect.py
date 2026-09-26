#!/usr/bin/env python3
"""
chained-fixups-inspect.py — offline LC_DYLD_CHAINED_FIXUPS validator for a
Mach-O binary. Dumps the fixups header, imports table stats, chain starts
(pointer formats), and validates bounds/ordinals the way dyld-7xx would —
so mismatches between what a modern toolchain emitted and what the in-tree
dyld supports are visible offline instead of as a wild jump at initializer
time.

Usage: python3 chained-fixups-inspect.py <macho>
"""
import struct, sys

LC_DYLD_CHAINED_FIXUPS = 0x80000034
LC_SEGMENT_64          = 0x19

# dyld_chain_format
IMPORT_FORMATS = {1: "IMPORT", 2: "IMPORT_ADDEND", 3: "IMPORT_ADDEND64"}
# dyld_chained_pointer_format (x86_64-relevant)
PTR_FORMATS = {1: "PTR_64", 2: "PTR_64_OFFSET", 3: "PTR_64_KERNEL_CACHE",
               4: "PTR_X86_64_KERNEL_CACHE", 5: "PTR_32", 6: "PTR_32_CACHE",
               7: "PTR_ARM64E", 8: "PTR_ARM64E_USERLAND",
               9: "PTR_ARM64E_FIRMWARE", 10: "PTR_ARM64E_KERNEL",
               11: "PTR_ARM64E_USERLAND24"}

def uleb(buf, off):
    result = 0; shift = 0
    while True:
        b = buf[off]; off += 1
        result |= (b & 0x7F) << shift
        if not (b & 0x80): break
        shift += 7
    return result, off

def main():
    path = sys.argv[1]
    with open(path, "rb") as f:
        d = f.read()
    if d[:4] != b"\xcf\xfa\xed\xfe":
        print("not a thin 64-bit mach-o"); return
    ncmds = struct.unpack_from("<I", d, 16)[0]
    off = 32
    segs = []           # (segname, fileoff, filesize, vmaddr, vmsize)
    cf = None
    for _ in range(ncmds):
        cmd, cs = struct.unpack_from("<II", d, off)
        if cmd == LC_SEGMENT_64:
            name = d[off+8:off+24].rstrip(b"\0").decode()
            vmaddr, vmsize, fileoff, filesize = struct.unpack_from("<QQQQ", d, off+24)
            segs.append((name, fileoff, filesize, vmaddr, vmsize))
        elif cmd == LC_DYLD_CHAINED_FIXUPS:
            dataoff, datasize = struct.unpack_from("<II", d, off+8)
            cf = (dataoff, datasize)
        off += cs

    if not cf:
        print("no LC_DYLD_CHAINED_FIXUPS"); return
    dataoff, datasize = cf
    print(f"LC_DYLD_CHAINED_FIXUPS dataoff={dataoff:#x} datasize={datasize:#x}")
    h = dataoff
    ver, starts_off, imports_off, symbols_off, imports_count, imports_fmt, symbols_fmt = \
        struct.unpack_from("<IIIIIII", d, h)
    print(f"  fixups_version={ver} starts_offset={starts_off:#x} "
          f"imports_offset={imports_off:#x} symbols_offset={symbols_off:#x} "
          f"imports_count={imports_count} "
          f"imports_format={imports_fmt} ({IMPORT_FORMATS.get(imports_fmt,'?')}) "
          f"symbols_format={symbols_fmt}")
    if ver != 0:
        print("  !! unknown fixups_version (dyld-7xx expects 0)")

    # imports table
    ordinals = {}
    weak = 0
    names = []
    for i in range(imports_count):
        io = h + imports_off + i * (4 if imports_fmt == 1 else 8 if imports_fmt == 2 else 12)
        if imports_fmt == 1:
            # struct dyld_chained_import: uint32 bitfields:
            # lib_ordinal:10, weak_import:1, reserved:1, name_offset:20
            v = struct.unpack_from("<I", d, io)[0]
            lo = v & 0x3FF
            weak = weak + ((v >> 10) & 1)
            no = (v >> 12) & 0xFFFFF
            if lo & 0x200: lo -= 0x400        # sign-extend 10 bits
            addend = 0
        elif imports_fmt == 2:
            v = struct.unpack_from("<I", d, io)[0]
            lo = v & 0x3FF
            weak = weak + ((v >> 10) & 1)
            no = (v >> 12) & 0xFFFFF
            if lo & 0x200: lo -= 0x400
            addend = struct.unpack_from("<i", d, io+4)[0]
        else:
            v = struct.unpack_from("<I", d, io)[0]
            lo = v & 0x3FF
            weak = weak + ((v >> 10) & 1)
            no = (v >> 12) & 0xFFFFF
            if lo & 0x200: lo -= 0x400
            addend = struct.unpack_from("<q", d, io+4)[0]
        ordinals[lo] = ordinals.get(lo, 0) + 1
        s = h + symbols_off + no
        e = d.find(b"\0", s)
        names.append((i, lo, d[s:e].decode(errors="replace")))

    print(f"  weak imports: {weak}")
    print("  lib_ordinal histogram:")
    for k in sorted(ordinals):
        print(f"    {k}: {ordinals[k]}")
    # valid ordinal range: -3..dylib_count (66) for Chrome; specials are 0xF0+
    dylibs = 0
    off2 = 32
    for _ in range(ncmds):
        cmd, cs = struct.unpack_from("<II", d, off2)
        if cmd in (0xC, 0x80000018, 0x8000001F, 0x80000022):
            dylibs += 1
        off2 += cs
    bad = [k for k in ordinals if k > 0xF0 and k not in (0xF1, 0xF2, 0xF3, 0xF4, 0xF5, 0xFE, 0xFF) and k - 0x10000 < -dylibs]
    print(f"  dylib-count={dylibs}; special-ordinal histogram keys beyond 0xF0 are specials")

    # symbols sample
    print("  first 8 imports:")
    for i, lo, nm in names[:8]:
        print(f"    [{i}] lib={lo} {nm}")

    # chain starts
    so = h + starts_off
    starts_fmt = struct.unpack_from("<I", d, so)[0]
    if starts_fmt != 0:
        print(f"  !! starts format {starts_fmt} != 0")
    seg_count = struct.unpack_from("<I", d, so+4)[0]
    print(f"  starts: seg_count={seg_count}")
    p = so + 8
    for _ in range(seg_count):
        seg_info_off = struct.unpack_from("<I", d, p)[0]
        if seg_info_off == 0:
            p += 4
            continue
        si = so + seg_info_off
        size, page_size, fptr = struct.unpack_from("<IBI", d, si)
        pfmt = (fptr >> 28) & 0xF
        start_off = fptr & 0x0FFFFFFF
        slack = size - page_size
        print(f"    segment start: page_size={page_size:#x} format={pfmt} "
              f"({PTR_FORMATS.get(pfmt,'UNSUPPORTED?')}) slack={slack:#x} start={start_off:#x}")
        if pfmt not in (1, 2):
            print(f"    !! pointer format {pfmt} unsupported by dyld-7xx classic x86_64 paths")
        p += 4

if __name__ == "__main__":
    main()
