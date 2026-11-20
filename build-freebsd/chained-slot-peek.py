#!/usr/bin/env python3
"""
chained-slot-peek.py — what does one __DATA slot of a chained-fixups Mach-O
actually hold: a rebase or a bind, and if a bind, to which import (library
ordinal + symbol name)? Offline, no run.

Usage: python3 chained-slot-peek.py <macho> <vmaddr-in-hex> [page|-]
  <vmaddr-in-hex>  absolute vmaddr of the slot (e.g. 0xf1a3098)
  page             print every entry of that page instead of one slot
"""
import struct, sys

LC_SEGMENT_64 = 0x19
LC_DYLD_CHAINED_FIXUPS = 0x80000034


def u32(b, o):
    return struct.unpack_from('<I', b, o)[0]


def u64(b, o):
    return struct.unpack_from('<Q', b, o)[0]


def u16(b, o):
    return struct.unpack_from('<H', b, o)[0]


def load(d):
    magic = u32(d, 0)
    assert magic == 0xfeedfacf, hex(magic)
    ncmds = u32(d, 16)
    off = 32
    segs, fix = {}, None
    for _ in range(ncmds):
        cmd, size = u32(d, off), u32(d, off + 4)
        if cmd == LC_SEGMENT_64:
            name = d[off + 8:off + 24].split(b'\0')[0].decode()
            vmaddr, vmsize, fileoff, filesize = struct.unpack_from('<QQQQ', d, off + 24)
            segs[name] = (vmaddr, vmsize, fileoff, filesize)
        elif cmd == LC_DYLD_CHAINED_FIXUPS:
            fix = (u32(d, off + 8), u32(d, off + 12))  # dataoff, datasize
        off += size
    return segs, fix


def main():
    path, addr = sys.argv[1], int(sys.argv[2], 16)
    whole_page = len(sys.argv) > 3 and sys.argv[3] == 'page'
    d = open(path, 'rb').read()
    segs, (dataoff, datasize) = load(d)
    blob = d[dataoff:dataoff + datasize]

    starts_off = u32(blob, 4)
    imports_off = u32(blob, 8)
    symbols_off = u32(blob, 12)
    imports_count = u32(blob, 16)

    imports = []
    for i in range(imports_count):
        v = u32(blob, imports_off + 4 * i)
        imports.append((v & 0xff, (v >> 8) & 1, (v >> 9) & 0x7fffff))

    def sym(noff):
        s = symbols_off + noff
        return blob[s:blob.index(b'\0', s)].decode()

    seg_count = u32(blob, starts_off)
    seg_info = [u32(blob, starts_off + 4 + 4 * i) for i in range(seg_count)]

    for si, so in enumerate(seg_info):
        if so == 0:
            continue
        base = starts_off + so
        page_size = u16(blob, base + 4)
        fmt = u16(blob, base + 6)
        seg_off = u64(blob, base + 8)
        page_count = u16(blob, base + 20)
        starts = [u16(blob, base + 22 + 2 * i) for i in range(page_count)]
        if not (seg_off <= addr < seg_off + page_count * page_size):
            continue
        page = (addr - seg_off) // page_size
        # pointer load address = file offset of the slotted page
        segname = [n for n, v in segs.items() if v[0] <= addr < v[0] + v[1]][0]
        foff0 = segs[segname][2] + (page * page_size)
        first = starts[page]
        if first == 0xffff:
            print(f"{segname} page {page}: no chain (start slot 0xffff)")
            return
        off = first
        while True:
            v = u64(d, foff0 + off)
            nxt = (v >> 51) & 0xfff
            is_bind = (v >> 63) & 1
            line = f"{segname}+0x{page * page_size + off:x}"
            if is_bind:
                ordinal = v & 0xffffff
                addend = (v >> 24) & 0xff
                lib, weak, noff = imports[ordinal]
                line += (f" BIND import[{ordinal}] lib_ordinal={lib} weak={weak}"
                         f" name={sym(noff)} addend={addend}")
            else:
                target = (v & 0xfffffffff) | (((v >> 36) & 0xff) << 56)
                line += f" REBASE target=0x{target:x}"
            if whole_page or page * page_size + off == addr - seg_off:
                print(line)
            if nxt == 0:
                break
            off += nxt * 4
        return
    print("address not in a chained-fixup segment")


main()
