#!/usr/bin/env python3
"""strip-mod-init-func.py — remove the __mod_init_func initializer registration
from a Mach-O dylib, in place.

Why flags and not the section name: dyld's ImageLoaderMachO::doModInitFunctions
selects initializer sections by `sect->flags & SECTION_TYPE ==
S_MOD_INIT_FUNC_POINTERS` (src/external/dyld/src/ImageLoaderMachO.cpp:2301) —
the section NAME is never consulted.  Renaming __mod_init_func to a
non-matching name would therefore change nothing; decrementing the segment's
nsects would drop the LAST section of the segment, which is not __mod_init_func.
The narrowest in-place fix is to clear the low 8 bits (SECTION_TYPE) of the
section's `flags` field (4 bytes, no load-command shift).

Thin and FAT Mach-O are both walked; for FAT every architecture slice is fixed.

Usage:
  strip-mod-init-func.py <file> [--name __mod_init_func]
"""
import struct
import sys

LC_SEGMENT = 0x1
LC_SEGMENT_64 = 0x19
SECTION_TYPE = 0xFF
S_MOD_INIT_FUNC_POINTERS = 0x9

MH_MAGIC = 0xFEEDFACE
MH_CIGAM = 0xCEFAEDFE
MH_MAGIC_64 = 0xFEEDFACF
MH_CIGAM_64 = 0xCFFAEDFE
FAT_MAGIC = 0xCAFEBABE
FAT_CIGAM = 0xBEBAFECA


def _strip_slice(data, base, size, want_name):
    magic = struct.unpack_from("<I", data, base)[0]
    if magic in (MH_MAGIC, MH_CIGAM):
        hdr, sect_size = 28, 68
    elif magic in (MH_MAGIC_64, MH_CIGAM_64):
        hdr, sect_size = 32, 80
    else:
        return 0
    ncmds = struct.unpack_from("<I", data, base + 16)[0]
    off = base + hdr
    end = base + size
    count = 0
    for _ in range(ncmds):
        if off + 8 > end:
            break
        cmd, cmdsize = struct.unpack_from("<II", data, off)
        if cmdsize < 8 or off + cmdsize > end:
            break
        if cmd in (LC_SEGMENT, LC_SEGMENT_64):
            nsects = struct.unpack_from("<I", data, off + 64)[0]
            so = off + (72 if cmd == LC_SEGMENT_64 else 56)
            for _ in range(nsects):
                if so + sect_size > off + cmdsize:
                    break
                name = data[so:so + 16].split(b"\0", 1)[0].decode("latin1")
                flags = struct.unpack_from("<I", data, so + 64)[0]
                if (flags & SECTION_TYPE) == S_MOD_INIT_FUNC_POINTERS and \
                        (want_name is None or name == want_name):
                    newflags = flags & ~SECTION_TYPE
                    struct.pack_into("<I", data, so + 64, newflags)
                    count += 1
                so += sect_size
        off += cmdsize
    return count


def strip(path, want_name):
    with open(path, "rb") as f:
        data = bytearray(f.read())
    magic = struct.unpack_from(">I", data, 0)[0]
    total = 0
    if magic in (FAT_MAGIC, FAT_CIGAM):
        nfat = struct.unpack_from(">I", data, 4)[0]
        for i in range(nfat):
            _, _, offset, size, _ = struct.unpack_from(">IIIII", data, 8 + i * 20)
            total += _strip_slice(data, offset, size, want_name)
    else:
        total = _strip_slice(data, 0, len(data), want_name)
    with open(path, "wb") as f:
        f.write(data)
    return total


def main():
    args = sys.argv[1:]
    want_name = "__mod_init_func"
    if "--name" in args:
        i = args.index("--name")
        want_name = args[i + 1]
        del args[i:i + 2]
    if len(args) != 1:
        raise SystemExit(__doc__)
    n = strip(args[0], want_name)
    print(f"stripped {n} __mod_init_func section(s) in {args[0]} "
          f"(flags SECTION_TYPE cleared, name filter {want_name!r})")
    if n == 0:
        raise SystemExit("FATAL: no S_MOD_INIT_FUNC_POINTERS section found")


if __name__ == "__main__":
    main()
