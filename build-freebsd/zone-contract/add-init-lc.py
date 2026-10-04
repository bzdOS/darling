#!/usr/bin/env python3
"""Insert the LC_ROUTINES_64 (-init) record for a named symbol into a linked
dylib.

ld64.lld does not implement -init (silently ignored, measured: link.log
"Option `-init' is not yet implemented"), so the -init edge is added
post-link with exact-length surgery:
- the record is built from the symbol's n_value (vmaddr) read from the
  built dylib's own symbol table;
- ncmds += 1, sizeofcmds += len(record);
- __TEXT filesize/vmsize += len(record) (the insertion lies inside it);
- every file-offset field pointing at or past the insertion point is
  shifted by len(record): LC_SEGMENT_64 fileoff, every section offset,
  symtab dysymtab file offsets, dyld_info offsets, linkedit_data offsets.

Usage: add-init-lc.py <built.dylib> <symbol-name>
"""
import struct
import sys

LC_SYMTAB, LC_DYSYMTAB = 0x2, 0xB
LC_DYLD_INFO, LC_DYLD_INFO_ONLY = 0x22, 0x80000022
LC_ROUTINES_64 = 0x1A
LINKEDIT_DATA = {0x1D, 0x1E, 0x1F, 0x20, 0x21, 0x24, 0x25, 0x26, 0x29}


def iter_lcs(dd):
    ncmds = int.from_bytes(dd[16:20], "little")
    o = 32
    for _ in range(ncmds):
        cmd, csz = struct.unpack_from("<II", dd, o)
        yield o, cmd, csz
        o += csz
    return o


def find_symbol_vmaddr(dd, name):
    symoff = stroff = nsyms = None
    for o, cmd, csz in iter_lcs(dd):
        if cmd == LC_SYMTAB:
            symoff, nsyms, stroff, _ = struct.unpack_from("<IIII", dd, o + 8)
            break
    if symoff is None:
        raise SystemExit("no LC_SYMTAB")
    want = name.encode() + b"\0"
    for i in range(nsyms):
        n_strx, n_type, n_sect, n_desc, n_value = struct.unpack_from("<IBBHQ", dd, symoff + i * 16)
        s = dd[stroff + n_strx:dd.index(b"\0", stroff + n_strx)]
        if s == name.encode() and (n_type & 0x01):  # N_EXT
            return n_value
    raise SystemExit(f"symbol {name} not found")


def main():
    built, name = sys.argv[1:3]
    dd = bytearray(open(built, "rb").read())
    assert dd[:4] != b"\xca\xfe\xba\xbe", "single-arch dylib expected"
    vmaddr = find_symbol_vmaddr(dd, name)
    print(f"{name} vmaddr=0x{vmaddr:x}")
    rec = bytearray(72)
    struct.pack_into("<II", rec, 0, LC_ROUTINES_64, 72)
    struct.pack_into("<QQ", rec, 8, vmaddr, 0)  # init_address, init_module
    rec = bytes(rec)
    lcs = list(iter_lcs(dd))
    insert_at = lcs[-1][0] + lcs[-1][2]  # after the last LC
    assert insert_at + len(rec) <= len(dd), "record does not fit"
    dd[insert_at:insert_at] = rec
    ncmds = int.from_bytes(dd[16:20], "little")
    sizeofcmds = int.from_bytes(dd[20:24], "little")
    dd[16:20] = (ncmds + 1).to_bytes(4, "little")
    dd[20:24] = (sizeofcmds + len(rec)).to_bytes(4, "little")

    def shift(off):
        return off + len(rec) if off >= insert_at else off

    o = 32
    n = ncmds + 1
    for _ in range(n):
        cmd, csz = struct.unpack_from("<II", dd, o)
        if cmd == 0x19:  # LC_SEGMENT_64
            fileoff = int.from_bytes(dd[o + 40:o + 48], "little")
            filesize = int.from_bytes(dd[o + 48:o + 56], "little")
            vmsize = int.from_bytes(dd[o + 32:o + 40], "little")
            if fileoff == 0:  # __TEXT holds the header+LCs
                dd[o + 48:o + 56] = (filesize + len(rec)).to_bytes(8, "little")
                dd[o + 32:o + 40] = (vmsize + len(rec)).to_bytes(8, "little")
            else:
                dd[o + 40:o + 48] = shift(fileoff).to_bytes(8, "little")
            nsects = int.from_bytes(dd[o + 64:o + 68], "little")
            so = o + 72
            for _ in range(nsects):
                sect_off = int.from_bytes(dd[so + 48:so + 56], "little")
                dd[so + 48:so + 56] = shift(sect_off).to_bytes(8, "little")
                so += 80
        elif cmd == LC_SYMTAB:
            symoff = int.from_bytes(dd[o + 8:o + 12], "little")
            stroff = int.from_bytes(dd[o + 16:o + 20], "little")
            dd[o + 8:o + 12] = shift(symoff).to_bytes(4, "little")
            dd[o + 16:o + 20] = shift(stroff).to_bytes(4, "little")
        elif cmd == LC_DYSYMTAB:
            for f in (32, 40, 48, 56, 64, 72):  # file offsets in dysymtab
                v = int.from_bytes(dd[o + f:o + f + 4], "little")
                dd[o + f:o + f + 4] = shift(v).to_bytes(4, "little")
        elif cmd in (LC_DYLD_INFO, LC_DYLD_INFO_ONLY):
            for f in (8, 16, 24, 32, 40, 48):  # rebases/weak/bind/weakbind/lazy/exports
                v = int.from_bytes(dd[o + f:o + f + 4], "little")
                dd[o + f:o + f + 4] = shift(v).to_bytes(4, "little")
        elif cmd in LINKEDIT_DATA:
            off_field = int.from_bytes(dd[o + 8:o + 12], "little")
            dd[o + 8:o + 12] = shift(off_field).to_bytes(4, "little")
        o += csz
    open(built, "wb").write(dd)
    print(f"inserted LC_ROUTINES_64 at {insert_at}; ncmds {ncmds}->{ncmds+1}; "
          f"sizeofcmds {sizeofcmds}->{sizeofcmds+len(rec)}")


if __name__ == "__main__":
    main()
