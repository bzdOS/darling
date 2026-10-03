#!/usr/bin/env python3
"""Insert the LC_LOAD_DYLIB(UPWARD) record for libsystem_c into a linked
dylib, byte-cloned from the original overlay dylib's own record.

ld64.lld does not implement -upward-l / -upward_library, so the upward
edge is added post-link with exact-length surgery:
- the record bytes are copied verbatim from the original (same cmd,
  name offset, timestamp, versions -- the encoding cannot be wrong);
- ncmds += 1, sizeofcmds += len(record);
- __TEXT filesize/vmsize += len(record) (the insertion lies inside it);
- every file-offset field pointing at or past the insertion point is
  shifted by len(record): LC_SEGMENT_64 fileoff, every section offset,
  symtab dysymtab file offsets, dyld_info offsets, linkedit_data offsets.

Usage: add-upward-lc.py <built.dylib> <original-with-upward> <symbol-name>
"""
import struct
import sys

DYLIB_CMDS = {0xC, 0x80000018, 0x8000001F, 0x80000023}
LC_SYMTAB, LC_DYSYMTAB = 0x2, 0xB
LC_DYLD_INFO, LC_DYLD_INFO_ONLY = 0x22, 0x80000022
LINKEDIT_DATA = {0x1D, 0x1E, 0x1F, 0x20, 0x21, 0x24, 0x25, 0x26, 0x29}


def iter_lcs(dd):
    ncmds = int.from_bytes(dd[16:20], "little")
    o = 32
    for _ in range(ncmds):
        cmd, csz = struct.unpack_from("<II", dd, o)
        yield o, cmd, csz
        o += csz
    return o


def lc_name(dd, o, csz):
    so = int.from_bytes(dd[o + 8:o + 12], "little")
    return dd[o + so:o + csz].split(b"\0")[0].decode(errors="replace")


def find_record(d, want):
    if d[:4] == b"\xca\xfe\xba\xbe":
        off = 8
        _, _, o, sz, _ = struct.unpack_from(">IIIII", d, off)
        d = d[o:o + sz]
    for o, cmd, csz in iter_lcs(d):
        if cmd in DYLIB_CMDS and lc_name(d, o, csz) == want:
            kind = int.from_bytes(d[o + 12:o + 16], "little")  # unused
            rec = bytearray(d[o:o + csz])
            rec[0:4] = (0x80000023).to_bytes(4, "little")  # UPWARD
            return bytes(rec)
    raise SystemExit(f"upward record for {want} not found in original")


def main():
    built, orig, name = sys.argv[1:4]
    rec = find_record(open(orig, "rb").read(), name)
    print(f"upward record: cmdsize={len(rec)} name={name}")
    dd = bytearray(open(built, "rb").read())
    assert dd[:4] != b"\xca\xfe\xba\xbe", "single-arch dylib expected"
    lcs = list(iter_lcs(dd))
    insert_at = lcs[-1][0] + lcs[-1][2]  # after the last LC
    # sanity: no LC at/after insert_at that we would clobber
    assert insert_at + len(rec) <= len(dd), "record does not fit"
    dd[insert_at:insert_at] = rec
    # header: ncmds + sizeofcmds
    ncmds = int.from_bytes(dd[16:20], "little")
    sizeofcmds = int.from_bytes(dd[20:24], "little")
    dd[16:20] = (ncmds + 1).to_bytes(4, "little")
    dd[20:24] = (sizeofcmds + len(rec)).to_bytes(4, "little")

    def shift(off):
        return off + len(rec) if off >= insert_at else off

    # walk the LC region AFTER insertion and patch file offsets
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
            for f in (8, 12, 16, 20, 24, 28, 32):  # file offsets in dysymtab
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
    print(f"inserted at {insert_at}; ncmds {ncmds}->{ncmds+1}; "
          f"sizeofcmds {sizeofcmds}->{sizeofcmds+len(rec)}")


if __name__ == "__main__":
    main()
