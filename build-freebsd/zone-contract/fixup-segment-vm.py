#!/usr/bin/env python3
"""Post-link segment geometry fixup for the rebuilt libsystem_malloc.

Measured problem chain (Controls #24/#25):

- ld64.lld in this toolchain has no -segalign (silently ignored) and
  does not round a segment's vmsize up to a page boundary, so __TEXT
  [0, 0x4f040) overlaps __DATA at vmaddr 0x4f000 by 0x40 bytes; dyld3
  refuses verbatim:
    "malformed mach-o image: segment __DATA vm overlaps segment __TEXT"
- rounding only __TEXT's vmsize up (first fixup revision) trips the
  next dyld check verbatim:
    "malformed mach-o image: segment __TEXT has vmsize != filesize
     and is executable"

Fix (one pass, exact-length field surgery + one zero-pad insertion):
__TEXT's file content is padded with (align_up(end) - end) zero bytes so
its filesize equals its page-rounded vmsize; __DATA's fileoff moves to
the same boundary; every file offset that points at __LINKEDIT content
(LC_SYMTAB, LC_DYSYMTAB, LC_DYLD_INFO_ONLY, LC_DATA_IN_CODE,
LC_SEGMENT_SPLIT_INFO, segment fileoffs, __DATA section offsets) shifts
by the pad amount.  vmaddrs move the same way as the pad so segments
stay contiguous and page-aligned; the fixup streams in __LINKEDIT are
segment-relative (bind/rebase opcodes address targets by segment
ordinal + offset), so no stream is rewritten.
"""

import struct
import sys

PAGE = 0x1000


def main() -> int:
    path = sys.argv[1]
    data = bytearray(open(path, "rb").read())
    ncmds = int.from_bytes(data[16:20], "little")

    segs = []
    o = 32
    for i in range(ncmds):
        cmd, csz = struct.unpack_from("<II", data, o)
        if cmd == 0x19:
            name = data[o + 8:o + 24].split(b"\0")[0].decode()
            vmaddr, vmsize, fileoff, filesize = struct.unpack_from("<QQQQ", data, o + 24)
            nsects = struct.unpack_from("<I", data, o + 64)[0]
            segs.append({"idx": i, "off": o, "name": name, "vmaddr": vmaddr,
                         "vmsize": vmsize, "fileoff": fileoff,
                         "filesize": filesize, "nsects": nsects})
        o += csz

    by_name = {s["name"]: s for s in segs}
    text = by_name["__TEXT"]
    data_seg = by_name["__DATA"]
    linkedit = by_name["__LINKEDIT"]

    text_end = text["vmaddr"] + text["vmsize"]
    new_text_vmsize = (text_end + PAGE - 1) & ~(PAGE - 1)
    pad = new_text_vmsize - text["filesize"]          # zero bytes to insert
    old_data_fileoff = data_seg["fileoff"]
    print(f"__TEXT end {text_end:#x} vmsize-> {new_text_vmsize:#x}; "
          f"file pad {pad:#x} at {old_data_fileoff:#x}")

    # insert the pad at the end of __TEXT's file content
    data[old_data_fileoff:old_data_fileoff] = b"\0" * pad

    # __TEXT: vmsize == filesize, both page-rounded
    struct.pack_into("<Q", data, text["off"] + 32, new_text_vmsize)
    struct.pack_into("<Q", data, text["off"] + 48, new_text_vmsize)

    # __DATA: vmaddr follows __TEXT's page-rounded end; fileoff follows
    # the pad; section addrs shift by the vm delta, offsets by the pad
    new_data_vmaddr = (text["vmaddr"] + new_text_vmsize)
    vm_delta = new_data_vmaddr - data_seg["vmaddr"]
    struct.pack_into("<Q", data, data_seg["off"] + 24, new_data_vmaddr)
    struct.pack_into("<Q", data, data_seg["off"] + 40, old_data_fileoff + pad)
    so = data_seg["off"] + 72
    for s in range(data_seg["nsects"]):
        off = struct.unpack_from("<I", data, so + 48)[0]
        struct.pack_into("<I", data, so + 48, off + pad)
        addr = struct.unpack_from("<Q", data, so + 32)[0]
        struct.pack_into("<Q", data, so + 32, addr + vm_delta)
        so += 80

    # __LINKEDIT: vmaddr follows __DATA's new end (measured refusal when
    # omitted: "segment __LINKEDIT vm overlaps segment __DATA"), fileoff
    # follows the pad
    struct.pack_into("<Q", data, linkedit["off"] + 24,
                     new_data_vmaddr + data_seg["vmsize"])
    struct.pack_into("<Q", data, linkedit["off"] + 40, linkedit["fileoff"] + pad)

    # every offset-bearing LC field that points into __LINKEDIT shifts
    def shift_u32(lc_off, field_off):
        v = struct.unpack_from("<I", data, lc_off + field_off)[0]
        if v >= old_data_fileoff:
            struct.pack_into("<I", data, lc_off + field_off, v + pad)

    o = 32
    for i in range(ncmds):
        cmd, csz = struct.unpack_from("<II", data, o)
        if cmd == 0x2:            # LC_SYMTAB: symoff, stroff
            shift_u32(o, 8); shift_u32(o, 16)
        elif cmd == 0xb:          # LC_DYSYMTAB: tocoff/modtaboff/extrefsymoff/
            #                     indirectsymoff/extreloff/locreloff
            for fo in (24, 32, 40, 48, 56, 64):
                shift_u32(o, fo)
        elif cmd == 0x80000022:   # LC_DYLD_INFO_ONLY: rebase/bind/weak/lazy/export offs
            for fo in (8, 16, 24, 32, 40):
                shift_u32(o, fo)
        elif cmd == 0x26:         # LC_DATA_IN_CODE: dataoff
            shift_u32(o, 8)
        elif cmd == 0x29:         # LC_SEGMENT_SPLIT_INFO: dataoff
            shift_u32(o, 8)
        o += csz

    open(path, "wb").write(data)

    # static verification
    o = 32
    prev_end = 0
    ok = True
    for i in range(ncmds):
        cmd, csz = struct.unpack_from("<II", data, o)
        if cmd == 0x19:
            name = data[o + 8:o + 24].split(b"\0")[0].decode()
            vmaddr, vmsize, fileoff, filesize = struct.unpack_from("<QQQQ", data, o + 24)
            aligned = vmaddr % PAGE == 0 and vmsize % PAGE == 0
            contiguous = vmaddr == prev_end or i == 0
            exec_ok = name != "__TEXT" or vmsize == filesize
            if name != "__LINKEDIT":
                ok = ok and aligned and contiguous and exec_ok
            print(f"  {name}: vm [{vmaddr:#x}, {vmaddr + vmsize:#x}) "
                  f"file [{fileoff:#x}, {fileoff + filesize:#x}) "
                  f"page-aligned={aligned} contiguous={contiguous} "
                  f"exec-clean={exec_ok}")
            prev_end = vmaddr + vmsize
        o += csz
    print("GEOMETRY_OK" if ok else "GEOMETRY_BAD")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
