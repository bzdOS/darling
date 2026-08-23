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
    # lea rsi, [rip+26] → hello_msg.
    # The displacement is relative to the END of this 7-byte instruction, and
    # hello_msg starts at offset 33 of CODE, so it must be 33-7 = 26. It used
    # to be 10, which made the stub write 14 bytes of its own machine code to
    # stdout instead of the message.
    b"\x48\x8d\x35\x1a\x00\x00\x00"
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

# ── Build helper: LC_SYMTAB / LC_DYSYMTAB ───────────────────────────────────
# dyld requires at least one of LC_SYMTAB/LC_DYLD_INFO/LC_DYLD_CHAINED_FIXUPS,
# and separately requires LC_DYSYMTAB, to consider __LINKEDIT's content
# valid (ImageLoaderMachO.cpp). This binary defines no symbols and does no
# rebasing/binding, so both commands point at zero-length regions inside
# the (otherwise empty) __LINKEDIT segment — enough to satisfy the
# structural checks without any real symbol/string table content.

def lc_symtab(symoff, nsyms, stroff, strsize):
    """Return packed LC_SYMTAB (symtab_command, 24 bytes)."""
    return struct.pack("<IIIIII", LC_SYMTAB, 24, symoff, nsyms, stroff, strsize)

def lc_dysymtab():
    """Return packed LC_DYSYMTAB (dysymtab_command, 80 bytes) — all-zero
    tables/counts (no locals, externs, undefs, TOC, module table, etc)."""
    return struct.pack("<IIIIIIIIIIIIIIIIIIII", LC_DYSYMTAB, 80, *([0] * 18))

# ── Assemble the binary ───────────────────────────────────────────────────────

def build():
    PAGE = 0x1000

    # Virtual address layout (matches standard macOS binary layout):
    #
    #   file off 0x0000: Mach-O header + load commands  (first page of __TEXT)
    #   file off 0x1000: __text machine code             (second page of __TEXT)
    #   file off 0x2000: __DATA                          (one page, empty)
    #
    #   vmaddr 0x000000000000..0x100000000 → __PAGEZERO  (4 GB guard, no file backing)
    #   vmaddr 0x100000000                → __TEXT       (2 pages, fileoff=0)
    #     vmaddr 0x100000000              →   header page (Mach-O header + lcmds)
    #     vmaddr 0x100001000              →   __text section (code)
    #   vmaddr 0x100002000                → __DATA       (1 page, anonymous)
    #
    # Having __TEXT at fileoff=0 is critical: loader.c sets lr->mh from the
    # segment that has fileoff==0, so the Mach header address passed to dyld
    # (via the stack) is TEXT_VMADDR + slide — the correct mapped address.

    PAGEZERO_VMADDR  = 0
    PAGEZERO_VMSIZE  = 0x100000000

    TEXT_VMADDR      = 0x100000000
    TEXT_VMSIZE      = 2 * PAGE     # header page + code page
    TEXT_FILEOFF     = 0            # __TEXT starts at beginning of file

    # The __text section (actual code) lives at page 1 of __TEXT.
    code_vmaddr   = TEXT_VMADDR + PAGE   # 0x100001000
    code_fileoff  = TEXT_FILEOFF + PAGE  # 0x00001000

    DATA_VMADDR      = TEXT_VMADDR + TEXT_VMSIZE   # 0x100002000
    DATA_VMSIZE      = PAGE
    DATA_FILEOFF     = TEXT_FILEOFF + TEXT_VMSIZE  # 0x00002000

    # dyld refuses to load any Mach-O without a __LINKEDIT segment (see
    # ImageLoaderMachO.cpp / MachOAnalyzer.cpp: "missing __LINKEDIT
    # segment") — real binaries always have one (symtab/strtab, rebase/
    # bind info, export trie, etc). This synthetic binary has none of that
    # content to link, so LINKEDIT is present but empty; it just needs to
    # exist as a non-overlapping segment for dyld's structural checks.
    LINKEDIT_VMADDR  = DATA_VMADDR + DATA_VMSIZE   # 0x100003000
    LINKEDIT_VMSIZE  = PAGE
    LINKEDIT_FILEOFF = DATA_FILEOFF + PAGE         # 0x00003000
    LINKEDIT_FILESIZE = PAGE

    # ── Load commands ────────────────────────────────────────────────────

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
        TEXT_FILEOFF, TEXT_VMSIZE,  # fileoff=0, filesize covers both pages
        VM_PROT_RX, VM_PROT_RX,
        sections=[text_section])

    seg_data = lc_segment_64(
        "__DATA",
        DATA_VMADDR, DATA_VMSIZE,
        DATA_FILEOFF, 0,
        VM_PROT_RW, VM_PROT_RW)

    seg_linkedit = lc_segment_64(
        "__LINKEDIT",
        LINKEDIT_VMADDR, LINKEDIT_VMSIZE,
        LINKEDIT_FILEOFF, LINKEDIT_FILESIZE,
        VM_PROT_READ, VM_PROT_READ)

    lc_dylinker = lc_load_dylinker("/usr/lib/dyld")
    lc_dylib    = lc_load_dylib("/usr/lib/libSystem.B.dylib",
                                 timestamp=2,
                                 current_version=0x051AFF00,
                                 compatibility_version=0x00010000)
    # LC_MAIN: entry offset is relative to TEXT_VMADDR (not file offset).
    # code lives at TEXT_VMADDR + PAGE, so entry offset = PAGE.
    lc_main_cmd  = lc_main(code_vmaddr - TEXT_VMADDR)   # = PAGE = 0x1000
    lc_vermin    = lc_version_min_macosx()

    # Empty symbol/string tables, placed at the start of __LINKEDIT's
    # (all-zero) content — nsyms=0 and strsize=0 mean nothing is ever
    # actually read from these offsets.
    lc_symtab_cmd   = lc_symtab(LINKEDIT_FILEOFF, 0, LINKEDIT_FILEOFF, 0)
    lc_dysymtab_cmd = lc_dysymtab()

    lc_all = (seg_pagezero + seg_text + seg_data + seg_linkedit +
              lc_dylinker + lc_dylib +
              lc_symtab_cmd + lc_dysymtab_cmd +
              lc_main_cmd + lc_vermin)

    ncmds       = 10  # __PAGEZERO, __TEXT, __DATA, __LINKEDIT, dylinker, dylib,
                       # LC_SYMTAB, LC_DYSYMTAB, LC_MAIN, LC_VERSION_MIN
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
    # This page is the first page of __TEXT (vmaddr 0x100000000, fileoff 0).
    header_page = hdr + lc_all
    assert len(header_page) <= PAGE, (
        f"header+lcmds overflow one page: {len(header_page)} bytes")
    header_page = pad_to(header_page, PAGE)

    # ── CODE page (code at __text section, page 1 of __TEXT) ─────────────
    text_page = pad_to(CODE, PAGE)

    # ── DATA page (empty, anonymous — filesize=0 so no file bytes needed) ─
    # DATA_FILEOFF is 0x2000 but filesize=0 so we still emit a zero page
    # to keep file offsets consistent (loader doesn't care: filesize=0).
    data_page = b'\x00' * PAGE

    # ── LINKEDIT page (empty placeholder — see seg_linkedit comment above) ─
    linkedit_page = b'\x00' * LINKEDIT_FILESIZE

    return header_page + text_page + data_page + linkedit_page


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
