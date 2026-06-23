#!/usr/bin/env python3
"""
gen-hello-dynamic-macho.py — generate a minimal x86-64 Mach-O MH_EXECUTE that:
  • has LC_LOAD_DYLINKER → /usr/lib/dyld
  • has LC_LOAD_DYLIB    → /usr/lib/libSystem.B.dylib
  • has LC_MAIN          → _main stub (calls write+exit via syscall when dyld
                           is absent, delegates to libSystem when present)

The binary is self-contained: the _start stub emits "hello-dynamic\n" using
the macOS BSD syscall ABI (eax = 0x2000004 for write, 0x2000001 for exit).
When mldr loads this binary it will:
  1. Parse LC_LOAD_DYLINKER, load dyld from the PREFIX.
  2. Dyld will try to satisfy LC_LOAD_DYLIB (libSystem.B.dylib).
  3. Even if dyld is absent/fails, the inline syscall stub executes directly,
     exercising the SIGSYS BSD handler path.

Usage:
  python3 gen-hello-dynamic-macho.py [output-path]
  default output: tests/hello-dynamic-macho
"""

import struct
import sys
import os

# ── Mach-O constants ──────────────────────────────────────────────────────────

MH_MAGIC_64       = 0xFEEDFACF
CPU_TYPE_X86_64   = 0x01000007
CPU_SUBTYPE_ALL   = 0x00000003
MH_EXECUTE        = 0x2
MH_PIE            = 0x00200000
MH_TWOLEVEL       = 0x00000080

LC_SEGMENT_64     = 0x19
LC_DYLD_INFO_ONLY = 0x80000022
LC_SYMTAB         = 0x2
LC_DYSYMTAB       = 0xB
LC_LOAD_DYLINKER  = 0xE
LC_UUID           = 0x1B
LC_VERSION_MIN_MACOSX = 0x24
LC_SOURCE_VERSION = 0x2A
LC_MAIN           = 0x80000028
LC_LOAD_DYLIB     = 0xC

VM_PROT_NONE    = 0x00
VM_PROT_READ    = 0x01
VM_PROT_WRITE   = 0x02
VM_PROT_EXEC    = 0x04
VM_PROT_RX      = VM_PROT_READ | VM_PROT_EXEC
VM_PROT_RW      = VM_PROT_READ | VM_PROT_WRITE

S_REGULAR        = 0x00
S_ATTR_SOME_INSTRUCTIONS = 0x00000400
S_ATTR_PURE_INSTRUCTIONS = 0x80000000

# ── Alignment helpers ─────────────────────────────────────────────────────────

def align_up(n, a):
    return (n + a - 1) & ~(a - 1)

def pad_to(buf, length, byte=b'\x00'):
    assert len(buf) <= length, f"buffer overflow: {len(buf)} > {length}"
    return buf + byte * (length - len(buf))

# ── x86-64 code stub ─────────────────────────────────────────────────────────
# Small _start function that writes "hello-dynamic\n" and exits.
# Uses macOS BSD syscall ABI (eax = 0x2000000 | nr).
# This is the fallback path when dyld/libSystem is unavailable.
# If dyld _does_ load successfully, dyld will call the LC_MAIN entry instead;
# the two paths produce the same output.
#
# write(1, msg, 14):  eax=0x2000004, edi=1, rsi=addr, edx=14
# exit(0):            eax=0x2000001, edi=0
#
# Layout (TEXT segment, file offset 0x1000):
#   _start:
#     lea  rsi, [rip + hello_offset]
#     mov  edx, 14
#     mov  edi, 1
#     mov  eax, 0x2000004
#     syscall
#     xor  edi, edi
#     mov  eax, 0x2000001
#     syscall
#   hello: db "hello-dynamic\n"

HELLO_MSG = b"hello-dynamic\n"
CODE = (
    b"\x48\x8d\x35\x0a\x00\x00\x00"   # lea rsi, [rip+10] → hello_msg
    b"\xba" + struct.pack("<I", len(HELLO_MSG)) +  # mov edx, 14
    b"\xbf\x01\x00\x00\x00"           # mov edi, 1
    b"\xb8\x04\x00\x00\x02"           # mov eax, 0x2000004  (write)
    b"\x0f\x05"                        # syscall
    b"\x31\xff"                        # xor edi, edi
    b"\xb8\x01\x00\x00\x02"           # mov eax, 0x2000001  (exit)
    b"\x0f\x05"                        # syscall
) + HELLO_MSG

# ── Build helper: LC_SEGMENT_64 ───────────────────────────────────────────────

def lc_segment_64(segname, vmaddr, vmsize, fileoff, filesize,
                  maxprot, initprot, flags=0, sections=None):
    """Return packed LC_SEGMENT_64 (with optional sections list)."""
    sections = sections or []
    nsects = len(sections)
    # segment_command_64 = cmd(4)+cmdsize(4)+segname(16)+vmaddr(8)+vmsize(8)
    #                    + fileoff(8)+filesize(8)+maxprot(4)+initprot(4)
    #                    + nsects(4)+flags(4) = 72 bytes
    # section_64 = sectname(16)+segname(16)+addr(8)+size(8)+offset(4)+align(4)
    #            + reloff(4)+nreloc(4)+flags(4)+reserved1(4)+reserved2(4)
    #            + reserved3(4) = 80 bytes
    sect_size = 80
    cmdsize = 72 + nsects * sect_size
    buf = struct.pack("<II",
        LC_SEGMENT_64, cmdsize)
    buf += segname.encode().ljust(16, b'\x00')
    buf += struct.pack("<QQQQIIII",
        vmaddr, vmsize, fileoff, filesize,
        maxprot, initprot, nsects, flags)
    for s in sections:
        # section_64: sectname(16), segname(16), addr(8), size(8),
        #             offset(4), align(4), reloff(4), nreloc(4),
        #             flags(4), reserved1(4), reserved2(4)
        buf += s['sectname'].encode().ljust(16, b'\x00')
        buf += s['segname'].encode().ljust(16, b'\x00')
        buf += struct.pack("<QQIIIIIIII",
            s['addr'], s['size'],
            s['offset'], s['align'],
            0, 0,
            s.get('flags', S_REGULAR),
            0, 0, 0)
    return buf

# ── Build helper: LC_LOAD_DYLINKER ───────────────────────────────────────────

def lc_load_dylinker(path):
    """Return packed LC_LOAD_DYLINKER."""
    path_bytes = path.encode() + b'\x00'
    # dylinker_command = 4+4+4 = 12 bytes header; name.offset = 12
    name_offset = 12
    total = align_up(name_offset + len(path_bytes), 8)
    buf = struct.pack("<III", LC_LOAD_DYLINKER, total, name_offset)
    buf += path_bytes
    return pad_to(buf, total)

# ── Build helper: LC_LOAD_DYLIB ───────────────────────────────────────────────

def lc_load_dylib(path, timestamp=0, current_version=0x00010000,
                  compatibility_version=0x00010000):
    """Return packed LC_LOAD_DYLIB."""
    path_bytes = path.encode() + b'\x00'
    # dylib_command = 4+4 + (4+4+4+4)=16 + name.offset = 24; name.offset = 24
    name_offset = 24
    total = align_up(name_offset + len(path_bytes), 8)
    buf = struct.pack("<IIIIII",
        LC_LOAD_DYLIB, total,
        name_offset, timestamp,
        current_version, compatibility_version)
    buf += path_bytes
    return pad_to(buf, total)

# ── Build helper: LC_MAIN ────────────────────────────────────────────────────

def lc_main(entryoff, stacksize=0):
    """Return packed LC_MAIN (entry_point_command)."""
    # entry_point_command = 4+4+8+8 = 24 bytes
    return struct.pack("<IIQQ", LC_MAIN, 24, entryoff, stacksize)

# ── Build helper: LC_VERSION_MIN_MACOSX ─────────────────────────────────────

def lc_version_min_macosx(version=0x000A0900, sdk=0x000A0900):
    """Return packed LC_VERSION_MIN_MACOSX (version_min_command, 16 bytes)."""
    return struct.pack("<IIII", LC_VERSION_MIN_MACOSX, 16, version, sdk)

# ── Assemble the binary ───────────────────────────────────────────────────────

def build():
    PAGE = 0x1000

    # Virtual address layout:
    #   0x000000000000..0x100000000 → __PAGEZERO
    #   0x100000000               → __TEXT  (1 page)
    #   0x100001000               → __DATA  (1 page, empty — satisfies dyld)

    PAGEZERO_VMADDR  = 0
    PAGEZERO_VMSIZE  = 0x100000000

    TEXT_VMADDR      = 0x100000000
    TEXT_VMSIZE      = PAGE
    TEXT_FILEOFF     = PAGE          # first file page after header page

    DATA_VMADDR      = TEXT_VMADDR + TEXT_VMSIZE
    DATA_VMSIZE      = PAGE
    DATA_FILEOFF     = TEXT_FILEOFF + TEXT_VMSIZE

    # ── Load commands ────────────────────────────────────────────────────

    code_vmaddr   = TEXT_VMADDR      # _start at base of __TEXT
    code_fileoff  = TEXT_FILEOFF     # same in file

    seg_pagezero = lc_segment_64(
        "__PAGEZERO",
        PAGEZERO_VMADDR, PAGEZERO_VMSIZE,
        0, 0,
        VM_PROT_NONE, VM_PROT_NONE)

    text_section = {
        'sectname': "__text",
        'segname':  "__TEXT",
        'addr':     code_vmaddr,
        'size':     len(CODE),
        'offset':   code_fileoff,
        'align':    4,
        'flags':    S_ATTR_PURE_INSTRUCTIONS | S_ATTR_SOME_INSTRUCTIONS,
    }
    seg_text = lc_segment_64(
        "__TEXT",
        TEXT_VMADDR, TEXT_VMSIZE,
        TEXT_FILEOFF, TEXT_VMSIZE,
        VM_PROT_RX, VM_PROT_RX,
        sections=[text_section])

    seg_data = lc_segment_64(
        "__DATA",
        DATA_VMADDR, DATA_VMSIZE,
        DATA_FILEOFF, 0,
        VM_PROT_RW, VM_PROT_RW)

    lc_dylinker = lc_load_dylinker("/usr/lib/dyld")
    lc_dylib    = lc_load_dylib("/usr/lib/libSystem.B.dylib",
                                 timestamp=2,
                                 current_version=0x051AFF00,
                                 compatibility_version=0x00010000)
    # LC_MAIN: entry offset relative to TEXT_VMADDR
    lc_main_cmd  = lc_main(code_vmaddr - TEXT_VMADDR)
    lc_vermin    = lc_version_min_macosx()

    lc_all = (seg_pagezero + seg_text + seg_data +
              lc_dylinker + lc_dylib +
              lc_main_cmd + lc_vermin)

    ncmds       = 7   # __PAGEZERO, __TEXT, __DATA, dylinker, dylib, LC_MAIN, LC_VERSION_MIN
    sizeofcmds  = len(lc_all)
    flags       = MH_TWOLEVEL | MH_PIE

    # ── Mach-O header (mach_header_64 = 32 bytes) ────────────────────────
    hdr = struct.pack("<IIIIIII",
        MH_MAGIC_64,
        CPU_TYPE_X86_64,
        CPU_SUBTYPE_ALL,
        MH_EXECUTE,
        ncmds,
        sizeofcmds,
        flags)
    hdr += b'\x00' * 4  # reserved

    # ── Header page (pad to PAGE boundary) ───────────────────────────────
    header_page = hdr + lc_all
    assert len(header_page) <= PAGE, (
        f"header+lcmds overflow one page: {len(header_page)} bytes")
    header_page = pad_to(header_page, PAGE)

    # ── TEXT page (code + zero padding) ──────────────────────────────────
    text_page = pad_to(CODE, PAGE)

    # ── DATA page (empty) ────────────────────────────────────────────────
    data_page = b'\x00' * PAGE

    return header_page + text_page + data_page


def main():
    out_path = sys.argv[1] if len(sys.argv) > 1 else os.path.join(
        os.path.dirname(__file__), "hello-dynamic-macho")
    data = build()
    with open(out_path, 'wb') as f:
        f.write(data)
    os.chmod(out_path, 0o755)
    print(f"Written {len(data)} bytes → {out_path}")
    # Quick sanity: first 4 bytes should be MH_MAGIC_64
    assert data[:4] == struct.pack("<I", 0xFEEDFACF), "bad magic"
    print("Magic OK (0xFEEDFACF)")


if __name__ == "__main__":
    main()
