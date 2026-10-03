#!/usr/bin/env python3
"""msl-diff.py — static diff of the rebuilt libsystem_malloc vs the original
overlay dylib, by the dispatch's priority:
  (i)   __DATA at the same vmaddr (non-zero divergences -> field/object)
  (ii)  bind/rebase/fixup streams (dyld_info opcode stream statistics)
  (iii) export trie (set of exported names)
  (iv)  sections (presence/size/flags/alignment)
The original is FAT — the x86-64 slice is compared. No external tools.
"""
import struct
import sys

MH_MAGIC_64 = 0xfeedfacf
FAT_MAGIC = 0xcafebabe
LC_SEGMENT_64 = 0x19
LC_DYLD_INFO = 0x22
LC_DYLD_INFO_ONLY = 0x80000022


def load_slice(path):
    d = open(path, "rb").read()
    magic = struct.unpack_from(">I", d, 0)[0]
    if magic == FAT_MAGIC:
        nfat = struct.unpack_from(">I", d, 4)[0]
        for i in range(nfat):
            off = 8 + i * 20
            cputype, cpusub, offset, size, align = struct.unpack_from(">IIIII", d, off)
            if cputype == 0x01000007:  # CPU_TYPE_X86_64
                return d[offset:offset + size], f"FAT x86-64 slice @0x{offset:x} size=0x{size:x}"
    if struct.unpack_from("<I", d, 0)[0] == MH_MAGIC_64:
        return d, "thin x86-64"
    raise SystemExit(f"{path}: not a Mach-O/fat I can read")


def parse(d):
    ncmds = struct.unpack_from("<I", d, 16)[0]
    o = 32
    segs = []
    dyld_info = None
    for _ in range(ncmds):
        cmd, cmdsize = struct.unpack_from("<II", d, o)
        if cmd == LC_SEGMENT_64:
            name = d[o + 8:o + 24].split(b"\0")[0].decode()
            vmaddr, vmsize, fileoff, filesize = struct.unpack_from("<QQQQ", d, o + 24)
            nsects = struct.unpack_from("<I", d, o + 64)[0]
            sects = []
            so = o + 72
            for _ in range(nsects):
                sn = d[so:so + 16].split(b"\0")[0].decode()
                sg = d[so + 16:so + 32].split(b"\0")[0].decode()
                saddr, ssize = struct.unpack_from("<QQ", d, so + 32)
                salign, sflags = struct.unpack_from("<II", d, so + 40)
                sects.append((f"{sg}.{sn}", saddr, ssize, salign, sflags))
                so += 80
            segs.append((name, vmaddr, vmsize, fileoff, filesize, sects))
        elif cmd in (LC_DYLD_INFO, LC_DYLD_INFO_ONLY):
            rebase_off, rebase_sz, bind_off, bind_sz, weak_off, weak_sz, \
                lazy_off, lazy_sz, export_off, export_sz = struct.unpack_from("<10I", d, o + 8)
            dyld_info = dict(rebase=(rebase_off, rebase_sz), bind=(bind_off, bind_sz),
                             weak=(weak_off, weak_sz), lazy=(lazy_off, lazy_sz),
                             export=(export_off, export_sz))
        o += cmdsize
    return segs, dyld_info


def rebase_stats(d, off, size):
    """Walk the rebase opcode stream; return (count-by-type, page-starts)."""
    b = d[off:off + size]
    i = 0
    counts = {}
    pages = []
    cur_seg = None
    while i < len(b):
        op = b[i]; i += 1
        imm = op & 0xF
        t = op >> 4
        if t == 0:  # DONE
            break
        if t == 1:  # SET_TYPE
            counts[imm] = counts.get(imm, 0) + 1
        elif t == 2:  # SET_SEGMENT_AND_OFFSET_ULEB
            cur_seg = imm
            while b[i] & 0x80:
                i += 1
            i += 1
        elif t == 3:  # ADD_ADDR_ULEB
            while b[i] & 0x80:
                i += 1
            i += 1
        elif t == 4:  # ADD_ADDR_IMM
            counts["add_imm"] = counts.get("add_imm", 0) + 1
        elif t == 5:  # DO_REBASE_IMM_TIMES
            counts["do_imm"] = counts.get("do_imm", 0) + imm
        elif t == 6:  # DO_REBASE_ULEB_TIMES
            while b[i] & 0x80:
                i += 1
            i += 1
            counts["do_uleb"] = counts.get("do_uleb", 0) + 1
        elif t == 7:  # DO_REBASE_ADD_ADDR_ULEB
            while b[i] & 0x80:
                i += 1
            i += 1
            counts["do_add_uleb"] = counts.get("do_add_uleb", 0) + 1
        elif t == 8:  # DO_REBASE_ULEB_TIMES_SKIPPING_ULEB
            while b[i] & 0x80:
                i += 1
            i += 1
            while b[i] & 0x80:
                i += 1
            i += 1
            counts["do_skip"] = counts.get("do_skip", 0) + 1
        else:
            counts["unk"] = counts.get("unk", 0) + 1
            break
    return counts


def export_set(d, off, size):
    """Minimal export-trie walk -> set of names."""
    b = d[off:off + size]
    out = set()

    def uleb(i):
        r = 0
        s = 0
        while b[i] & 0x80:
            r |= (b[i] & 0x7F) << s
            s += 7
            i += 1
        r |= b[i] << s
        return r, i + 1

    def walk(i, prefix):
        while True:
            term, i = uleb(i)
            if term:
                out.add(prefix)
            nchild, i = uleb(i)
            if not nchild:
                return i
            for _ in range(nchild):
                j = i
                while b[j]:
                    j += 1
                name = b[i:j].decode(errors="replace")
                i = j + 1
                off2, i = uleb(i)
                walk(off2, prefix + name)
            return i

    walk(0, "")
    return out


def main():
    orig_path, rebuild_path = sys.argv[1], sys.argv[2]
    od, olabel = load_slice(orig_path)
    rd, rlabel = load_slice(rebuild_path)
    print(f"original: {olabel} ({len(od)} B)")
    print(f"rebuild : {rlabel} ({len(rd)} B)")
    osegs, odi = parse(od)
    rsegs, rdi = parse(rd)

    print("\n=== (iv) sections ===")
    osects = {s[0]: s for seg in osegs for s in seg[5]}
    rsects = {s[0]: s for seg in rsegs for s in seg[5]}
    for name in sorted(set(osects) | set(rsects)):
        o = osects.get(name)
        r = rsects.get(name)
        if o and not r:
            print(f"  only-original : {name} vmaddr=0x{o[1]:x} size=0x{o[2]:x} align={o[3]} flags=0x{o[4]:x}")
        elif r and not o:
            print(f"  only-rebuild  : {name} vmaddr=0x{r[1]:x} size=0x{r[2]:x} align={r[3]} flags=0x{r[4]:x}")
        elif o[1] != r[1] or o[2] != r[2] or o[4] != r[4]:
            print(f"  differs       : {name}")
            print(f"      original vmaddr=0x{o[1]:x} size=0x{o[2]:x} align={o[3]} flags=0x{o[4]:x}")
            print(f"      rebuild  vmaddr=0x{r[1]:x} size=0x{r[2]:x} align={r[3]} flags=0x{r[4]:x}")

    print("\n=== (i) __DATA.__v_zone + same-vmaddr __DATA bytes ===")
    for label, d, segs in (("original", od, osegs), ("rebuild", rd, rsegs)):
        for seg in segs:
            for s in seg[5]:
                if s[0] in ("__DATA.__v_zone", "__DATA.__malloc_zone"):
                    fo = seg[3] + (s[1] - seg[1])
                    blob = d[fo:fo + min(s[2], 64)]
                    print(f"  {label}: {s[0]} vmaddr=0x{s[1]:x} size=0x{s[2]:x}")
                    print(f"      {blob.hex()}")

    print("\n=== (ii) dyld_info streams ===")
    for label, di in (("original", odi), ("rebuild", rdi)):
        if not di:
            print(f"  {label}: NO LC_DYLD_INFO(_ONLY)")
            continue
        rcount = rebase_stats(od if label == "original" else rd, *di["rebase"])
        print(f"  {label}: rebase stream ops={rcount} sizes=" +
              ", ".join(f"{k}=0x{v[1]:x}" for k, v in di.items()))

    print("\n=== (iii) export set ===")
    oexp = export_set(od, *odi["export"]) if odi else set()
    rexp = export_set(rd, *rdi["export"]) if rdi else set()
    print(f"  original exports: {len(oexp)}")
    print(f"  rebuild  exports: {len(rexp)}")
    only_o = sorted(oexp - rexp)
    only_r = sorted(rexp - oexp)
    print(f"  only-original ({len(only_o)}): {only_o[:25]}{' ...' if len(only_o) > 25 else ''}")
    print(f"  only-rebuild  ({len(only_r)}): {only_r[:25]}{' ...' if len(only_r) > 25 else ''}")


if __name__ == "__main__":
    main()
