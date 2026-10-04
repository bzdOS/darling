#!/usr/bin/env python3
"""Insert a __DATA,__mod_init_func entry for a named symbol into a linked
dylib.

ld64.lld does not implement -init (silently ignored, measured: link.log
"Option `-init' is not yet implemented"), and dyld forbids -init in any
image that does not link with libSystem.dylib (ImageLoaderMachO.cpp:2261-
2263: the check requires libSystemInitialized=true, which is only true
after libSystem.B's own initializer has run).  The stock overlay members
(libSystem.B.dylib, libc++.1.dylib) register their initializers through
__DATA,__mod_init_func instead — a section of function pointers that dyld
calls after the image is loaded, without the -init gate.  This script
repeats that mechanism for ___malloc_init:

- if __DATA,__mod_init_func already exists, append one 8-byte pointer;
- otherwise create the section (80-byte section header + 8 bytes of data)
  at the end of __DATA;
- every file-offset field pointing at or past the insertion point is
  shifted by 8: LC_SEGMENT_64 fileoff, every section offset, symtab
  dysymtab file offsets, dyld_info offsets, linkedit_data offsets;
- __DATA filesize/vmsize += 8.

Usage: add-mod-init-func.py <built.dylib> <symbol-name>
"""
import struct
import sys

LC_SYMTAB, LC_DYSYMTAB = 0x2, 0xB
LC_DYLD_INFO, LC_DYLD_INFO_ONLY = 0x22, 0x80000022
LINKEDIT_DATA = {0x1D, 0x1E, 0x1F, 0x20, 0x21, 0x24, 0x25, 0x26, 0x29}
SECT_MOD_INIT_FUNC = 0x9  # S_MOD_INIT_TERM_POINTERS


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

    # locate __DATA segment and its sections
    data_seg_off = None
    data_seg_nsects = 0
    data_seg_fileoff = 0
    data_seg_filesize = 0
    data_seg_vmsize = 0
    mod_init_sect_off = None  # file offset of existing __mod_init_func data
    mod_init_sect_size = 0
    last_sect_end = 0  # file offset just past the last __DATA section

    for o, cmd, csz in iter_lcs(dd):
        if cmd == 0x19:  # LC_SEGMENT_64
            segname = dd[o + 8:o + 24].split(b"\0")[0].decode()
            if segname == "__DATA":
                data_seg_off = o
                data_seg_nsects = int.from_bytes(dd[o + 64:o + 68], "little")
                data_seg_fileoff = int.from_bytes(dd[o + 40:o + 48], "little")
                data_seg_filesize = int.from_bytes(dd[o + 48:o + 56], "little")
                data_seg_vmsize = int.from_bytes(dd[o + 32:o + 40], "little")
                so = o + 72
                for _ in range(data_seg_nsects):
                    sectname = dd[so:so + 16].split(b"\0")[0].decode()
                    sect_off = int.from_bytes(dd[so + 48:so + 56], "little")
                    sect_size = int.from_bytes(dd[so + 40:so + 48], "little")
                    if sectname == "__mod_init_func":
                        mod_init_sect_off = sect_off
                        mod_init_sect_size = sect_size
                    last_sect_end = max(last_sect_end, sect_off + sect_size)
                    so += 80
                break

    if data_seg_off is None:
        raise SystemExit("no __DATA segment found")

    if mod_init_sect_off is not None:
        # append to existing section: insert 8 bytes at end of section data
        insert_at = mod_init_sect_off + mod_init_sect_size
        print(f"appending to existing __mod_init_func at file offset {insert_at:#x}")
    else:
        # create new section: insert 8 bytes at end of __DATA file content
        insert_at = data_seg_fileoff + data_seg_filesize
        print(f"creating new __mod_init_func section at file offset {insert_at:#x}")

    # shift all file content from insert_at onward by 8 bytes
    dd[insert_at:insert_at] = b"\0" * 8

    # update __DATA segment filesize/vmsize
    struct.pack_into("<Q", dd, data_seg_off + 48, data_seg_filesize + 8)
    struct.pack_into("<Q", dd, data_seg_off + 32, data_seg_vmsize + 8)

    # shift every file-offset field that points at or past insert_at
    def shift(off):
        return off + 8 if off >= insert_at else off

    o = 32
    ncmds = int.from_bytes(dd[16:20], "little")
    for _ in range(ncmds):
        cmd, csz = struct.unpack_from("<II", dd, o)
        if cmd == 0x19:  # LC_SEGMENT_64
            fileoff = int.from_bytes(dd[o + 40:o + 48], "little")
            if fileoff != 0:  # __TEXT holds header+LCs at fileoff 0
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
            for f in (32, 40, 48, 56, 64, 72):
                v = int.from_bytes(dd[o + f:o + f + 4], "little")
                dd[o + f:o + f + 4] = shift(v).to_bytes(4, "little")
        elif cmd in (LC_DYLD_INFO, LC_DYLD_INFO_ONLY):
            for f in (8, 16, 24, 32, 40, 48):
                v = int.from_bytes(dd[o + f:o + f + 4], "little")
                dd[o + f:o + f + 4] = shift(v).to_bytes(4, "little")
        elif cmd in LINKEDIT_DATA:
            off_field = int.from_bytes(dd[o + 8:o + 12], "little")
            dd[o + 8:o + 12] = shift(off_field).to_bytes(4, "little")
        o += csz

    if mod_init_sect_off is not None:
        # update existing section size
        so = data_seg_off + 72
        for _ in range(data_seg_nsects):
            sectname = dd[so:so + 16].split(b"\0")[0].decode()
            if sectname == "__mod_init_func":
                struct.pack_into("<Q", dd, so + 40, mod_init_sect_size + 8)
                break
            so += 80
    else:
        # append a new section header (80 bytes) at end of __DATA section list
        # the section list ends right before the next LC after __DATA
        # find the offset just past the last __DATA section header
        sect_list_end = data_seg_off + 72 + data_seg_nsects * 80
        # build the new section header
        new_sect = bytearray(80)
        new_sect[0:16] = b"__mod_init_func".ljust(16, b"\0")
        new_sect[16:32] = b"__DATA".ljust(16, b"\0")
        struct.pack_into("<Q", new_sect, 32, 0)  # addr (vmaddr, patched below)
        struct.pack_into("<Q", new_sect, 40, 8)  # size
        struct.pack_into("<I", new_sect, 48, insert_at)  # offset
        struct.pack_into("<I", new_sect, 56, 2)  # align 2^3
        struct.pack_into("<I", new_sect, 64, 0)  # reloff
        struct.pack_into("<I", new_sect, 68, 0)  # nreloc
        struct.pack_into("<I", new_sect, 72, SECT_MOD_INIT_FUNC)  # flags
        # insert the new section header
        dd[sect_list_end:sect_list_end] = new_sect
        # update nsects
        struct.pack_into("<I", dd, data_seg_off + 64, data_seg_nsects + 1)
        # patch the section addr (vmaddr) — it must match the file offset
        # relative to the segment vmaddr
        data_seg_vmaddr = int.from_bytes(dd[data_seg_off + 24:data_seg_off + 32], "little")
        new_sect_addr = data_seg_vmaddr + (insert_at - data_seg_fileoff)
        struct.pack_into("<Q", dd, sect_list_end + 32, new_sect_addr)

    # write the function pointer into the (now zeroed) 8-byte slot
    struct.pack_into("<Q", dd, insert_at, vmaddr)

    open(built, "wb").write(dd)
    print(f"inserted __mod_init_func entry for {name} at file offset {insert_at:#x}")


if __name__ == "__main__":
    main()
