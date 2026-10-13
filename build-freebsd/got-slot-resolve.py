#!/usr/bin/env python3
"""
got-slot-resolve.py — offline resolver for a Mach-O image's __got slots.

Walks the LC_DYLD_INFO (or LC_DYLD_INFO_ONLY) bind opcode stream and maps
each slot in the __got section to the imported symbol the loader will bind
there, so a runtime comparison inside the image can be checked against the
file without a run.

Usage: python3 got-slot-resolve.py <macho> [vaddr]
With [vaddr] it prints the slot's symbol and stops; without it it lists
every mapped __got slot that has a bind target.
"""
import struct
import sys

LC_DYLD_INFO = 0x22
LC_DYLD_INFO_ONLY = 0x80000022
LC_SEGMENT_64 = 0x19


def read_cstrings(d, pool_off):
    s = pool_off
    e = d.find(b"\0", s)
    return d[s:e].decode(errors="replace")


def main():
    path = sys.argv[1]
    want = int(sys.argv[2], 0) if len(sys.argv) > 2 else None
    with open(path, "rb") as f:
        d = f.read()
    if d[:4] == b"\xca\xfe\xba\xbe":
        # fat universal binary: pick the x86_64 slice (CPU_TYPE_X86_64 = 0x01000007)
        nfat = struct.unpack_from(">I", d, 4)[0]
        base = None
        for i in range(nfat):
            cputype, cpusub, off, size, _al = struct.unpack_from(">iiIII", d, 8 + i * 20)
            if cputype == 0x01000007:
                base = off
                break
        if base is None:
            print("fat binary has no x86_64 slice")
            return
        print(f"fat binary: using the x86_64 slice at file offset {base:#x}")
        d = d[base:]
    if d[:4] != b"\xcf\xfa\xed\xfe":
        print("not a thin 64-bit mach-o")
        return
    ncmds = struct.unpack_from("<I", d, 16)[0]
    off = 32
    segs = []           # (vmaddr, vmsize, fileoff, filesize)
    got_section = None  # (vmaddr, size, index)
    sec_idx = 0
    dy = None           # (bind_off, bind_size)
    for _ in range(ncmds):
        cmd, cs = struct.unpack_from("<II", d, off)
        if cmd == LC_SEGMENT_64:
            vmaddr, vmsize, fileoff, filesize = struct.unpack_from("<QQQQ", d, off + 24)
            nsec = struct.unpack_from("<I", d, off + 64)[0]
            so = off + 72
            for _i in range(nsec):
                sname = d[so:so + 16].rstrip(b"\0").decode()
                svm, ssize = struct.unpack_from("<QQ", d, so + 32)
                sfo = struct.unpack_from("<I", d, so + 48)[0]
                if sname == "__got":
                    got_section = (svm, ssize, sec_idx)
                sec_idx += 1
                so += 80
            segs.append((vmaddr, vmsize, fileoff, filesize))
        elif cmd in (LC_DYLD_INFO, LC_DYLD_INFO_ONLY):
            _, _, _, _, bind_off, bind_size = struct.unpack_from("<IIIIII", d, off + 8)
            dy = (bind_off, bind_size)
        off += cs

    if not dy or not got_section:
        print("no LC_DYLD_INFO bind info or no __got section")
        return
    got_vm, got_size, _ = got_section
    print(f"__got section: vmaddr={got_vm:#x} size={got_size:#x} slots={got_size // 8}")

    boff, bsize = dy
    p = boff
    end = boff + bsize
    seg_index = 0
    seg_offset = 0
    ordinal = 0
    addend = 0
    sym = None
    ptr_size = 8
    # BIND_OPCODE_* constants
    DONE, SET_DYLIB_ORDINAL_IM, SET_DYLIB_ORDINAL_ULEB = 0x00, 0x01, 0x02
    SET_DYLIB_SPECIAL, SET_SYMBOL_TRAILING, SET_TYPE_INTEGER = 0x03, 0x10, 0x11
    SET_ADDEND_SLEB, SET_SEGMENT_AND_OFFSET, SET_DO_SEGMENT = 0x21, 0x30, 0x31
    BIND_ULEB, THROW, DO_BIND, DO_BIND_ADDEND, DO_BIND_ULEB = 0x50, 0x51, 0x52, 0x53, 0x54
    binding = None  # (ordinal, sym) pending
    slots = {}
    count = 0
    while p < end:
        op = d[p]
        imm = d[p + 1]
        p += 2
        if op == DONE:
            break
        if op == SET_DYLIB_ORDINAL_IM:
            ordinal = imm
        elif op == SET_DYLIB_ORDINAL_ULEB:
            v, p = read_uleb(d, p)
            ordinal = v
        elif op == SET_DYLIB_SPECIAL:
            ordinal = -imm
        elif op == SET_SYMBOL_TRAILING:
            sym = read_cstrings(d, p)
            p = d.index(b"\0", p) + 1
        elif op in (DO_BIND, DO_BIND_ADDEND, DO_BIND_ULEB):
            if op != DO_BIND:
                if op == DO_BIND_ADDEND:
                    addend, p = read_sleb(d, p)
                else:
                    addend, p = read_uleb(d, p)
            slots[(seg_index, seg_offset)] = (ordinal, sym)
            seg_offset += ptr_size
            count += 1
            if op == DO_BIND_ULEB:
                n, p = read_uleb(d, p)
                seg_offset += ptr_size * (n - 1)
                count += n - 1
        elif op == BIND_ULEB:
            n, p = read_uleb(d, p)
            for _i in range(n):
                slots[(seg_index, seg_offset)] = (ordinal, sym)
                seg_offset += ptr_size
                count += 1
        elif op == SET_SEGMENT_AND_OFFSET:
            seg_index = imm
            seg_offset, p = read_uleb(d, p)
        elif op == SET_ADDEND_SLEB:
            addend, p = read_sleb(d, p)
        elif op == THROW:
            print("!! THROW opcode in bind stream")
            break
        # SET_TYPE_INTEGER and unknown opcodes: skip

    printed = 0
    for (si, so), (ordn, s) in sorted(slots.items()):
        if si >= len(segs):
            continue
        vm = segs[si][0] + so
        if not (got_vm <= vm < got_vm + got_size):
            continue
        if want is not None and vm != want:
            continue
        print(f"  slot vmaddr={vm:#x} = dylib_ordinal {ordn} symbol {s}")
        printed += 1
    if printed == 0:
        print("  (no mapped __got slots found)")
    print(f"total bound pointers in stream: {count}; mapped inside __got: "
          f"{sum(1 for (si, so) in slots if si < len(segs) and got_vm <= segs[si][0] + so < got_vm + got_size)}")


def read_uleb(d, p):
    r = 0
    s = 0
    while True:
        b = d[p]
        p += 1
        r |= (b & 0x7F) << s
        if not (b & 0x80):
            break
        s += 7
    return r, p


def read_sleb(d, p):
    r = 0
    s = 0
    while True:
        b = d[p]
        p += 1
        r |= (b & 0x7F) << s
        s += 7
        if not (b & 0x80):
            if b & 0x40:
                r -= 1 << s
            break
    return r, p


if __name__ == "__main__":
    main()
