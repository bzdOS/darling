#!/usr/bin/env python3
"""Insert the LC_LOAD_DYLIB(UPWARD) record for libsystem_c into a linked
dylib, byte-cloned from the original overlay dylib's own record.

ld64.lld does not implement -upward-l / -upward_library, so the upward
edge is added post-link as a PURE IN-PLACE OVERWRITE of the zero header
slack that -headerpad reserves: the record bytes are copied verbatim
from the original (same cmd, name offset, timestamp, versions -- the
encoding cannot be wrong); ncmds += 1, sizeofcmds += len(record).  The
file does not grow, so nothing moves: section file offsets, segment
fileoff/filesize/vmsize and every __LINKEDIT offset stay exactly as the
linker wrote them.

The previous revision shifted every file offset at or past the insertion
point and grew __TEXT's vmsize/filesize by the record length.  That
broke the file<->vm identity dyld relies on (ImageLoaderMachO.cpp maps
each segment 1:1: mmap(vmaddr, vmsize, fd, fileoff)): every __TEXT
section ended up at file offset vm+0x40, so the code dyld executed was
the instruction stream of the wrong addresses, and __TEXT's inflated
vmsize overlapped __DATA's vmaddr ("malformed mach-o image: segment
__DATA vm overlaps segment __TEXT").

Requires the link to pass -headerpad with at least len(record) bytes of
slack between the last load command and the first section; the slack is
already part of __TEXT's file/vm range, so filling it with the record
changes no offset anywhere.

Usage: add-upward-lc.py <built.dylib> <original-with-upward> <symbol-name>
"""
import struct
import sys

DYLIB_CMDS = {0xC, 0x80000018, 0x8000001F, 0x80000023}


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

    # first section's file offset bounds the header slack
    first_sect_off = None
    for o, cmd, csz in lcs:
        if cmd == 0x19:  # LC_SEGMENT_64
            nsects = int.from_bytes(dd[o + 64:o + 68], "little")
            so = o + 72
            for _ in range(nsects):
                sect_off = int.from_bytes(dd[so + 48:so + 52], "little")
                if sect_off != 0 and (first_sect_off is None or sect_off < first_sect_off):
                    first_sect_off = sect_off
                so += 80
    slack = (first_sect_off or len(dd)) - insert_at
    if slack < len(rec):
        raise SystemExit(
            f"FATAL: header slack {slack:#x} < record {len(rec):#x} at "
            f"{insert_at:#x}; link with -headerpad larger than the record"
        )
    print(f"header slack {slack:#x} at {insert_at:#x}; writing {len(rec):#x}")

    # overwrite the zero slack in place -- the file does NOT grow, so no
    # file offset anywhere moves
    end = insert_at + len(rec)
    assert dd[insert_at:end] == b"\0" * len(rec), "header slack is not zero padding"
    dd[insert_at:end] = rec
    ncmds = int.from_bytes(dd[16:20], "little")
    sizeofcmds = int.from_bytes(dd[20:24], "little")
    dd[16:20] = (ncmds + 1).to_bytes(4, "little")
    dd[20:24] = (sizeofcmds + len(rec)).to_bytes(4, "little")
    open(built, "wb").write(dd)
    print(f"overwrote [{insert_at:#x},{end:#x}); ncmds {ncmds}->{ncmds+1}; "
          f"sizeofcmds {sizeofcmds}->{sizeofcmds+len(rec)}")


if __name__ == "__main__":
    main()
