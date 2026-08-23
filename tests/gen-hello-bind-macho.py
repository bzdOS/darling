#!/usr/bin/env python3
"""
gen-hello-bind-macho.py — generate a minimal x86-64 Mach-O MH_EXECUTE that
actually binds and calls a real libSystem symbol (_puts), instead of just
making raw macOS BSD syscalls directly like hello-dynamic-macho does.

hello-dynamic-macho proved dyld can find, map and jump into libSystem.B.dylib
and its dependency closure. It does NOT exercise symbol binding: it has
nsyms=0 and empty LC_DYSYMTAB tables, and its own code makes raw syscalls
rather than calling into libSystem. This binary is the next rung: it carries
real LC_DYLD_INFO_ONLY bind opcodes that tell dyld to resolve "_puts" against
libSystem.B.dylib and write the resolved function pointer into a __DATA slot
before the entry point ever runs — then the entry point loads that pointer
and calls through it. If this prints "hello-bind" via puts() rather than a
direct write(2) syscall, dyld's bind-opcode parser, symbol lookup against a
real Darwin dylib, and the DATA-segment fixup write path all worked.

Binds to _write rather than _puts. _puts was tried first and did resolve to
a real address inside the loaded dylib set (confirmed live: the __DATA slot
held a plausible non-null pointer after load, in the same ~4MB range as the
libraries dyld had mapped) — so export-trie symbol resolution against
libSystem.B.dylib's re-exports works. But calling it crashed (SIGBUS) three
call frames deep inside libSystem's own code, consistent with puts() needing
stdio machinery (locks, __stdoutp, buffering) that a minimal binary with no
other initializers doesn't set up. _write is libSystem's thin wrapper
directly around the write(2) syscall — no stdio state involved — so it
exercises the same bind/resolve/call path without that dependency.

Usage:
  python3 gen-hello-bind-macho.py [output-path]
  default output: tests/hello-bind-macho
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
LC_VERSION_MIN_MACOSX = 0x24
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

# bind opcode stream constants (mach-o/loader.h — not vendored in this repo,
# values are the standard, unchanged-since-10.6 Apple constants)
BIND_OPCODE_DONE                        = 0x00
BIND_OPCODE_SET_DYLIB_ORDINAL_IMM       = 0x10
BIND_OPCODE_SET_SYMBOL_TRAILING_FLAGS_IMM = 0x40
BIND_OPCODE_SET_TYPE_IMM                = 0x50
BIND_OPCODE_SET_SEGMENT_AND_OFFSET_ULEB = 0x70
BIND_OPCODE_DO_BIND                     = 0x90
BIND_TYPE_POINTER                       = 1

# ── Alignment helpers ─────────────────────────────────────────────────────────

def align_up(n, a):
    return (n + a - 1) & ~(a - 1)

def pad_to(buf, length, byte=b'\x00'):
    assert len(buf) <= length, f"buffer overflow: {len(buf)} > {length}"
    return buf + byte * (length - len(buf))

def uleb128(n):
    out = bytearray()
    while True:
        b = n & 0x7f
        n >>= 7
        if n:
            out.append(b | 0x80)
        else:
            out.append(b)
            return bytes(out)

# ── x86-64 code stub ─────────────────────────────────────────────────────────
# _start:
#   lea  rsi, [rip + D1]      → msg ("hello-bind\n")
#   mov  edx, len(msg)
#   mov  edi, 1                (fd 1 = stdout)
#   mov  rax, [rip + D2]      → load the _write pointer dyld bound into __DATA
#   call rax                  → write(1, msg, len)   [libSystem's _write]
#   xor  edi, edi
#   mov  eax, 0x2000001       → exit(0) via raw syscall (avoids needing to
#                                bind _exit too — one bound symbol is the point)
#   syscall
#
# D1/D2 are filled in by build() once segment vmaddrs are known: D1 spans two
# segments' worth of nothing (msg is right after the code, same __TEXT
# segment) but D2 crosses into __DATA, so it's computed from vmaddrs, not
# buffer offsets — RIP-relative addressing works the same either way since it
# only cares about the runtime distance between instruction and target.

MSG = b"hello-bind\n"

LEA_LEN  = 7   # 48 8D 35 <disp32>          lea rsi, [rip+d1]
MOVEDX_LEN = 5 # BA <imm32>                 mov edx, len
MOVEDI_LEN = 5 # BF <imm32>                 mov edi, 1
MOVQ_LEN = 7   # 48 8B 05 <disp32>          mov rax, [rip+d2]
CALL_LEN = 2   # FF D0                      call rax
TAIL = (
    b"\x31\xff"                        # xor edi, edi
    b"\xb8\x01\x00\x00\x02"           # mov eax, 0x2000001  (exit)
    b"\x0f\x05"                        # syscall
)

def build_code(msg_vmaddr, got_vmaddr, code_vmaddr):
    lea_end   = code_vmaddr + LEA_LEN
    movq_off  = LEA_LEN + MOVEDX_LEN + MOVEDI_LEN
    movq_end  = code_vmaddr + movq_off + MOVQ_LEN
    d1 = msg_vmaddr - lea_end
    d2 = got_vmaddr - movq_end
    code = (
        b"\x48\x8d\x35" + struct.pack("<i", d1) +   # lea rsi, [rip+d1]
        b"\xba" + struct.pack("<I", len(MSG)) +      # mov edx, len(msg)
        b"\xbf\x01\x00\x00\x00" +                     # mov edi, 1
        b"\x48\x8b\x05" + struct.pack("<i", d2) +    # mov rax, [rip+d2]
        b"\xff\xd0"                                   # call rax
        + TAIL
    )
    assert len(code) == LEA_LEN + MOVEDX_LEN + MOVEDI_LEN + MOVQ_LEN + CALL_LEN + len(TAIL)
    return code + MSG

# ── Build helper: LC_SEGMENT_64 ───────────────────────────────────────────────

def lc_segment_64(segname, vmaddr, vmsize, fileoff, filesize,
                  maxprot, initprot, flags=0, sections=None):
    sections = sections or []
    nsects = len(sections)
    sect_size = 80
    cmdsize = 72 + nsects * sect_size
    buf = struct.pack("<II", LC_SEGMENT_64, cmdsize)
    buf += segname.encode().ljust(16, b'\x00')
    buf += struct.pack("<QQQQIIII",
        vmaddr, vmsize, fileoff, filesize,
        maxprot, initprot, nsects, flags)
    for s in sections:
        buf += s['sectname'].encode().ljust(16, b'\x00')
        buf += s['segname'].encode().ljust(16, b'\x00')
        buf += struct.pack("<QQIIIIIIII",
            s['addr'], s['size'],
            s['offset'], s['align'],
            0, 0,
            s.get('flags', S_REGULAR),
            0, 0, 0)
    return buf

def lc_load_dylinker(path):
    path_bytes = path.encode() + b'\x00'
    name_offset = 12
    total = align_up(name_offset + len(path_bytes), 8)
    buf = struct.pack("<III", LC_LOAD_DYLINKER, total, name_offset)
    buf += path_bytes
    return pad_to(buf, total)

def lc_load_dylib(path, timestamp=0, current_version=0x00010000,
                  compatibility_version=0x00010000):
    path_bytes = path.encode() + b'\x00'
    name_offset = 24
    total = align_up(name_offset + len(path_bytes), 8)
    buf = struct.pack("<IIIIII",
        LC_LOAD_DYLIB, total,
        name_offset, timestamp,
        current_version, compatibility_version)
    buf += path_bytes
    return pad_to(buf, total)

def lc_main(entryoff, stacksize=0):
    return struct.pack("<IIQQ", LC_MAIN, 24, entryoff, stacksize)

def lc_version_min_macosx(version=0x000A0900, sdk=0x000A0900):
    return struct.pack("<IIII", LC_VERSION_MIN_MACOSX, 16, version, sdk)

def lc_symtab(symoff, nsyms, stroff, strsize):
    return struct.pack("<IIIIII", LC_SYMTAB, 24, symoff, nsyms, stroff, strsize)

def lc_dysymtab():
    return struct.pack("<IIIIIIIIIIIIIIIIIIII", LC_DYSYMTAB, 80, *([0] * 18))

def lc_dyld_info_only(bind_off, bind_size):
    """dyld_info_command (48 bytes): 10 uint32 fields after cmd/cmdsize.
    Only bind_off/bind_size are used here — rebase/weak_bind/lazy_bind/export
    all stay zero-sized, which eachBind()/doBind() treat as simply nothing to
    do in those categories (confirmed by reading ImageLoaderMachOCompressed.cpp
    directly: each category is parsed independently, off fDyldInfo->*_off/
    *_size, and a zero-size region means its parse loop never executes)."""
    return struct.pack("<IIIIIIIIIIII",
        LC_DYLD_INFO_ONLY, 48,
        0, 0,                    # rebase_off, rebase_size
        bind_off, bind_size,     # bind_off, bind_size
        0, 0,                    # weak_bind_off, weak_bind_size
        0, 0,                    # lazy_bind_off, lazy_bind_size
        0, 0)                    # export_off, export_size

def build_bind_opcodes(data_segment_index, got_offset_in_segment, symbol):
    """Bind exactly one pointer-sized __DATA slot to `symbol`, resolved
    against ordinal 1 (the first LC_LOAD_DYLIB — libSystem.B.dylib here)."""
    out = bytearray()
    out.append(BIND_OPCODE_SET_DYLIB_ORDINAL_IMM | 1)
    out.append(BIND_OPCODE_SET_SYMBOL_TRAILING_FLAGS_IMM | 0)
    out += symbol.encode() + b'\x00'
    out.append(BIND_OPCODE_SET_TYPE_IMM | BIND_TYPE_POINTER)
    out.append(BIND_OPCODE_SET_SEGMENT_AND_OFFSET_ULEB | data_segment_index)
    out += uleb128(got_offset_in_segment)
    out.append(BIND_OPCODE_DO_BIND)
    out.append(BIND_OPCODE_DONE)
    return bytes(out)

# ── Assemble the binary ───────────────────────────────────────────────────────

def build():
    PAGE = 0x1000

    PAGEZERO_VMADDR  = 0
    PAGEZERO_VMSIZE  = 0x100000000

    TEXT_VMADDR      = 0x100000000
    TEXT_VMSIZE      = 2 * PAGE
    TEXT_FILEOFF     = 0

    code_vmaddr   = TEXT_VMADDR + PAGE
    code_fileoff  = TEXT_FILEOFF + PAGE

    DATA_VMADDR      = TEXT_VMADDR + TEXT_VMSIZE
    DATA_VMSIZE      = PAGE
    DATA_FILEOFF     = TEXT_FILEOFF + TEXT_VMSIZE

    LINKEDIT_VMADDR   = DATA_VMADDR + DATA_VMSIZE
    LINKEDIT_VMSIZE   = PAGE
    LINKEDIT_FILEOFF  = DATA_FILEOFF + PAGE
    LINKEDIT_FILESIZE = PAGE

    # __DATA offset 0: the 8-byte GOT-style slot dyld's bind opcodes fill in
    # with the resolved address of _puts before our entry point runs.
    GOT_OFFSET_IN_DATA = 0
    got_vmaddr = DATA_VMADDR + GOT_OFFSET_IN_DATA

    msg_vmaddr = (code_vmaddr + LEA_LEN + MOVEDX_LEN + MOVEDI_LEN
                  + MOVQ_LEN + CALL_LEN + len(TAIL))
    CODE = build_code(msg_vmaddr, got_vmaddr, code_vmaddr)

    # segment order defines segmentIndex for BIND_OPCODE_SET_SEGMENT_AND_OFFSET_ULEB:
    # 0=__PAGEZERO, 1=__TEXT, 2=__DATA, 3=__LINKEDIT
    DATA_SEGMENT_INDEX = 2
    bind_opcodes = build_bind_opcodes(DATA_SEGMENT_INDEX, GOT_OFFSET_IN_DATA, "_write")
    BIND_FILEOFF = LINKEDIT_FILEOFF
    assert len(bind_opcodes) <= LINKEDIT_FILESIZE

    seg_pagezero = lc_segment_64(
        "__PAGEZERO", PAGEZERO_VMADDR, PAGEZERO_VMSIZE, 0, 0,
        VM_PROT_NONE, VM_PROT_NONE)

    text_section = {
        'sectname': "__text", 'segname': "__TEXT",
        'addr': code_vmaddr, 'size': len(CODE),
        'offset': code_fileoff, 'align': 4,
        'flags': S_ATTR_PURE_INSTRUCTIONS | S_ATTR_SOME_INSTRUCTIONS,
    }
    seg_text = lc_segment_64(
        "__TEXT", TEXT_VMADDR, TEXT_VMSIZE, TEXT_FILEOFF, TEXT_VMSIZE,
        VM_PROT_RX, VM_PROT_RX, sections=[text_section])

    # filesize must cover the GOT slot on disk (dyld's bind writer expects the
    # segment's mapped memory to already exist with the right protections;
    # zero-filesize/anonymous worked for hello-dynamic-macho's unused __DATA,
    # but here dyld also needs segWriteable(segmentIndex) to hold, which reads
    # the segment's initprot, not its filesize — filesize=0 is still fine, but
    # give it one full page on disk for clarity and to have a well-defined
    # initial value to sanity-check while debugging).
    seg_data = lc_segment_64(
        "__DATA", DATA_VMADDR, DATA_VMSIZE, DATA_FILEOFF, PAGE,
        VM_PROT_RW, VM_PROT_RW)

    seg_linkedit = lc_segment_64(
        "__LINKEDIT", LINKEDIT_VMADDR, LINKEDIT_VMSIZE,
        LINKEDIT_FILEOFF, LINKEDIT_FILESIZE,
        VM_PROT_READ, VM_PROT_READ)

    lc_dylinker = lc_load_dylinker("/usr/lib/dyld")
    lc_dylib    = lc_load_dylib("/usr/lib/libSystem.B.dylib",
                                 timestamp=2,
                                 current_version=0x051AFF00,
                                 compatibility_version=0x00010000)
    lc_main_cmd  = lc_main(code_vmaddr - TEXT_VMADDR)
    lc_vermin    = lc_version_min_macosx()
    lc_symtab_cmd    = lc_symtab(LINKEDIT_FILEOFF, 0, LINKEDIT_FILEOFF, 0)
    lc_dysymtab_cmd  = lc_dysymtab()
    lc_dyldinfo_cmd  = lc_dyld_info_only(BIND_FILEOFF, len(bind_opcodes))

    lc_all = (seg_pagezero + seg_text + seg_data + seg_linkedit +
              lc_dylinker + lc_dylib +
              lc_dyldinfo_cmd + lc_symtab_cmd + lc_dysymtab_cmd +
              lc_main_cmd + lc_vermin)

    ncmds       = 11  # __PAGEZERO, __TEXT, __DATA, __LINKEDIT, dylinker, dylib,
                       # LC_DYLD_INFO_ONLY, LC_SYMTAB, LC_DYSYMTAB, LC_MAIN,
                       # LC_VERSION_MIN
    sizeofcmds  = len(lc_all)
    flags       = MH_TWOLEVEL | MH_PIE

    hdr = struct.pack("<IIIIIII",
        MH_MAGIC_64, CPU_TYPE_X86_64, CPU_SUBTYPE_ALL,
        MH_EXECUTE, ncmds, sizeofcmds, flags)
    hdr += b'\x00' * 4

    header_page = hdr + lc_all
    assert len(header_page) <= PAGE, (
        f"header+lcmds overflow one page: {len(header_page)} bytes")
    header_page = pad_to(header_page, PAGE)

    text_page = pad_to(CODE, PAGE)
    data_page = b'\x00' * PAGE

    linkedit_page = pad_to(bind_opcodes, LINKEDIT_FILESIZE)

    return header_page + text_page + data_page + linkedit_page


def main():
    out_path = sys.argv[1] if len(sys.argv) > 1 else os.path.join(
        os.path.dirname(__file__), "hello-bind-macho")
    data = build()
    with open(out_path, 'wb') as f:
        f.write(data)
    os.chmod(out_path, 0o755)
    print(f"Written {len(data)} bytes → {out_path}")
    assert data[:4] == struct.pack("<I", 0xFEEDFACF), "bad magic"
    print("Magic OK (0xFEEDFACF)")


if __name__ == "__main__":
    main()
