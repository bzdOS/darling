#!/usr/bin/env python3
"""Add LC_REEXPORT_DYLIB for libobjc.A.dylib to a Mach-O dylib.
Inserts without shifting — uses the gap between load commands and __TEXT."""

import struct
import sys

LC_SEGMENT_64 = 0x19
LC_REEXPORT_DYLIB = 0x80000018

def align8(n):
    return (n + 7) & ~7

def read_cstring(data, offset):
    end = data.index(b'\x00', offset)
    return data[offset:end].decode('utf-8', errors='replace')

def patch_macho(path, reexport_path):
    with open(path, 'rb') as f:
        data = bytearray(f.read())

    magic_le = struct.unpack_from('<I', data, 0)[0]
    if magic_le == 0xFEEDFACF:
        patch_single_slice(data, 0, reexport_path)
    else:
        print(f"Not a 64-bit Mach-O: 0x{magic_le:08x}")
        sys.exit(1)

    with open(path, 'wb') as f:
        f.write(data)
    print(f"Patched: {path}")

def patch_single_slice(data, offset, reexport_path):
    ncmds = struct.unpack_from('<I', data, offset + 16)[0]
    sizeofcmds = struct.unpack_from('<I', data, offset + 20)[0]

    lc_start = offset + 32
    lc_end = lc_start + sizeofcmds

    # Find first segment's fileoff to know where data starts
    pos = lc_start
    first_fileoff = None
    has_reexport = False
    while pos < lc_end:
        cmd = struct.unpack_from('<I', data, pos)[0]
        cmdsize = struct.unpack_from('<I', data, pos + 4)[0]
        if cmdsize == 0:
            break
        if cmd == LC_SEGMENT_64:
            name = read_cstring(data, pos + 8)
            fileoff = struct.unpack_from('<Q', data, pos + 48)[0]
            if first_fileoff is None or (fileoff > 0 and fileoff < first_fileoff):
                first_fileoff = fileoff
        if cmd == LC_REEXPORT_DYLIB:
            name_off = struct.unpack_from('<I', data, pos + 8)[0]
            name = read_cstring(data, pos + name_off)
            if 'libobjc' in name:
                has_reexport = True
                print(f"  Already has LC_REEXPORT_DYLIB for {name}")
                return
        pos += cmdsize

    if first_fileoff is None:
        print("  Could not find any segment fileoff")
        return

    # Build new LC_REEXPORT_DYLIB
    reexport_bytes = reexport_path.encode('utf-8') + b'\x00'
    name_offset_from_cmd = 28
    cmdsize = align8(name_offset_from_cmd + len(reexport_bytes))

    cmd_data = struct.pack('<II', LC_REEXPORT_DYLIB, cmdsize)
    cmd_data += struct.pack('<I', name_offset_from_cmd)
    cmd_data += struct.pack('<I', 0)  # timestamp
    cmd_data += struct.pack('<I', 0)  # current_version
    cmd_data += struct.pack('<I', 0)  # compatibility_version
    cmd_data += struct.pack('<I', 0)  # data_offset
    cmd_data += reexport_bytes
    cmd_data += b'\x00' * (cmdsize - len(cmd_data))

    # Check if there's room between lc_end and first segment data
    gap = first_fileoff - lc_end
    if gap < cmdsize:
        print(f"  Not enough room: gap={gap} need={cmdsize}")
        return

    # Insert load command right after existing ones, no shifting needed
    new_data = bytearray(data[:lc_end])
    new_data += cmd_data
    new_data += bytearray(data[lc_end:])

    # Update header
    struct.pack_into('<I', new_data, offset + 16, ncmds + 1)
    struct.pack_into('<I', new_data, offset + 20, sizeofcmds + cmdsize)

    data[:] = new_data
    print(f"  Inserted LC_REEXPORT_DYLIB ({cmdsize} bytes) at 0x{lc_end:x}, no shift needed")

if __name__ == '__main__':
    if len(sys.argv) != 3:
        print(f"Usage: {sys.argv[0]} <macho-binary> <reexport-lib-path>")
        sys.exit(1)
    patch_macho(sys.argv[1], sys.argv[2])
