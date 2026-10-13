#!/usr/bin/env python3
"""
resolve-chained-import.py — resolve a chained-fixups import ordinal to its
symbol and providing library, in one Mach-O image.

Reads only the bytes it needs (seeks, never a full read) so it is safe on
the chrome framework's ~600 MB image. Answers the "which image's class is
this object bound to" question offline, from the file.

Usage: python3 resolve-chained-import.py <macho> <ordinal>
"""
import struct
import sys

LC_LOAD_DYLIB = 0x0C
LC_LOAD_WEAK_DYLIB = 0x80000018
LC_REEXPORT_DYLIB = 0x8000001F
LC_LOAD_UPWARD_DYLIB = 0x80000022
LC_DYLD_CHAINED_FIXUPS = 0x80000034
LC_SEGMENT_64 = 0x19


def read_at(f, off, n):
    f.seek(off)
    return f.read(n)


def cstr_at(f, off, limit=512):
    f.seek(off)
    out = b""
    while len(out) < limit:
        b = f.read(1)
        if b == b"" or b == b"\0":
            break
        out += b
    return out.decode(errors="replace")


def main():
    path = sys.argv[1]
    ordinal = int(sys.argv[2], 0)
    with open(path, "rb") as f:
        head = read_at(f, 0, 32)
        if head[:4] == b"\xca\xfe\xba\xbe":
            nfat = struct.unpack_from(">I", head, 4)[0]
            fh = read_at(f, 8, nfat * 20)
            base = None
            for i in range(nfat):
                cputype, cpusub, off, size, _al = struct.unpack_from(">iiIII", fh, i * 20)
                if cputype == 0x01000007:
                    base = off
                    break
            if base is None:
                print("fat binary has no x86_64 slice")
                return
            print(f"fat binary: x86_64 slice at file offset {base:#x}")
        else:
            base = 0
        hdr = read_at(f, base, 32)
        if hdr[:4] != b"\xcf\xfa\xed\xfe":
            print("not a thin 64-bit mach-o")
            return
        ncmds = struct.unpack_from("<I", hdr, 16)[0]
        off = base + 32
        cf = None
        dylibs = []
        for _ in range(ncmds):
            lc = read_at(f, off, 8)
            cmd, cs = struct.unpack_from("<II", lc, 0)
            if cmd == LC_DYLD_CHAINED_FIXUPS:
                dataoff, datasize = struct.unpack_from("<II", lc + b"\0" * 0, 8) if False else \
                    struct.unpack_from("<II", read_at(f, off + 8, 8), 0)
                cf = (base + dataoff, datasize)
            elif cmd in (LC_LOAD_DYLIB, LC_LOAD_WEAK_DYLIB, LC_REEXPORT_DYLIB, LC_LOAD_UPWARD_DYLIB):
                # struct dylib_command { uint32 cmd, cmdsize; struct lc_str name; ... }
                # lc_str's offset counts from the command start.
                name_off = struct.unpack_from("<I", read_at(f, off + 8, 4), 0)[0]
                dylibs.append(cstr_at(f, off + name_off))
            off += cs
        if not cf:
            print("no LC_DYLD_CHAINED_FIXUPS")
            return
        dataoff, datasize = cf
        ver, starts_off, imports_off, symbols_off, imports_count, imports_fmt, symbols_fmt = \
            struct.unpack_from("<IIIIIII", read_at(f, dataoff, 28), 0)
        print(f"fixups: dataoff={dataoff:#x} datasize={datasize:#x} version={ver} "
              f"starts_offset={starts_off:#x} imports_offset={imports_off:#x} "
              f"symbols_offset={symbols_off:#x} imports_count={imports_count} "
              f"imports_format={imports_fmt} ({ {1: 'IMPORT', 2: 'IMPORT_ADDEND', 3: 'IMPORT_ADDEND64'}.get(imports_fmt, '?') })")
        if ordinal < 0 or ordinal >= imports_count:
            print(f"ordinal {ordinal} outside the import table (0..{imports_count - 1})")
            return
        if imports_fmt != 1:
            print(f"imports_format {imports_fmt} not handled by this tool (only the 4-byte IMPORT layout)")
            return
        io = dataoff + imports_off + ordinal * 4
        # struct dyld_chained_import { lib_ordinal:8, weak_import:1, name_offset:23 }
        # (dyld's mach-o/fixup-chains.h — the import table's 4-byte layout)
        v = struct.unpack_from("<I", read_at(f, io, 4), 0)[0]
        lib_ordinal = v & 0xFF
        weak = (v >> 8) & 1
        name_offset = v >> 9
        name = cstr_at(f, dataoff + symbols_off + name_offset)
        # dyld's numbering: 0 = special (self?), 1..n = LC_LOAD_* order
        SPECIALS = {0: "special/self", 0xFE: "flat lookup", 0xFF: "main executable"}
        if lib_ordinal in SPECIALS:
            libname = SPECIALS[lib_ordinal]
        elif 1 <= lib_ordinal <= len(dylibs):
            libname = dylibs[lib_ordinal - 1]
        else:
            libname = f"<out of range: {lib_ordinal} of {len(dylibs)}>"
        print(f"import[{ordinal:#x}] = lib_ordinal {lib_ordinal} ({lib_ordinal:#x}), "
              f"weak={weak}, name={name}")
        print(f"  -> provided by: {libname}")
        print(f"  -> dylib list has {len(dylibs)} entries")


if __name__ == "__main__":
    main()
