#!/usr/bin/env python3
"""
convert-chained-imports.py — rewrite a Mach-O's LC_DYLD_CHAINED_FIXUPS import
table from the STANDARD modern bit layout (lib_ordinal:10, weak:1, reserved:1,
name_offset:20 — what modern ld64 emits, e.g. Chrome 154) to the layout the
in-tree Darling-on-FreeBSD dyld expects (lib_ordinal:8, weak:1, name_offset:23
— see src/external/dyld/include/mach-o/fixup-chains.h).

Without this conversion, dyld decodes every import with shifted bitfields:
wrong (name, library) pairs, garbage resolve targets, and a wild jump the
first time an initializer runs through an unbound import.

Same-size in-place rewrite (4 bytes per entry, both layouts). The chain
starts / pointer formats / bind ordinals are untouched — ordinals index this
same imports table.

Special ordinals: standard 10-bit -1/-2/-3 (0x3FF/0x3FE/0x3FD) map to
8-bit-signed 0xFF/0xFE/0xFD (subtract 0x300).

Usage: python3 convert-chained-imports.py <macho> [--dry]
"""
import struct, sys

LC_DYLD_CHAINED_FIXUPS = 0x80000034

def main():
    path = sys.argv[1]
    dry = "--dry" in sys.argv
    with open(path, "rb") as f:
        d = bytearray(f.read())
    if d[:4] != b"\xcf\xfa\xed\xfe":
        print("not a thin 64-bit mach-o"); return 1
    ncmds = struct.unpack_from("<I", d, 16)[0]
    off = 32
    cf = None
    for _ in range(ncmds):
        cmd, cs = struct.unpack_from("<II", d, off)
        if cmd == LC_DYLD_CHAINED_FIXUPS:
            cf = struct.unpack_from("<II", d, off + 8)
        off += cs
    if not cf:
        print("no LC_DYLD_CHAINED_FIXUPS"); return 1
    dataoff, datasize = cf
    h = dataoff
    ver, starts_off, imports_off, symbols_off, count, fmt, symfmt = \
        struct.unpack_from("<IIIIIII", d, h)
    if ver != 0 or fmt != 1:
        print(f"unsupported: version={ver} format={fmt} (need version 0, format 1)")
        return 1
    print(f"imports: {count} entries at chain_data+{imports_off:#x}")

    changed = bad = 0
    for i in range(count):
        io = h + imports_off + i * 4
        v = struct.unpack_from("<I", d, io)[0]
        lo = v & 0x3FF
        weak = (v >> 10) & 1
        no = (v >> 12) & 0xFFFFF
        # sanity: name offset must be inside the string pool
        if no >= datasize - symbols_off:
            bad += 1
            continue
        # map special ordinals: 10-bit -1..-3 → 8-bit -1..-3
        if lo >= 0x3FD:
            lo8 = lo - 0x300
        elif lo > 0xFF:
            lo8 = 0xFF            # clamp unexpected specials to flat lookup -1? keep safe
            bad += 1
        else:
            lo8 = lo
        v2 = (lo8 & 0xFF) | (weak << 8) | ((no & 0x7FFFFF) << 9)
        struct.pack_into("<I", d, io, v2)
        changed += 1
    print(f"converted {changed} entries ({bad} suspicious)")
    if bad:
        print("!! suspicious entries found — NOT writing")
        return 1
    if not dry:
        with open(path, "wb") as f:
            f.write(d)
        print("written")
    return 0

if __name__ == "__main__":
    sys.exit(main())
