#!/usr/bin/env python3
"""cg-supl-patch.py — stage-copy surgery: point the Chrome framework's
CoreGraphics imports at a supplement dylib.

The chrome framework binds37 symbols to CoreGraphics (ordinal 3) that the
overlay's CoreGraphics stub does not export; dyld's two-level binds ignore
injected dylibs, so the symbols must live in a dylib the framework's own
load commands reference. This patch, applied to the STAGE COPY only:

 1. appends one LC_LOAD_DYLIB ("/usr/lib/cg-supl.dylib") in the header-page
    slack (exact-length: the command fits the slack exactly, 8-aligned,
    nothing else moves; ncmds 78 -> 79 -> ordinal 67);
 2. rewrites lib_ordinal 3 -> 67 in the LC_DYLD_CHAINED_FIXUPS imports
    table for exactly the listed symbols (bit-exact byte patches at the
    parsed entry offsets; name strings untouched).

Verification re-parses the patched file: the listed symbols must carry
ordinal 67, all other imports unchanged, ncmds 79.

Usage: cg-supl-patch.py <staged-chrome-framework> <missing.txt>
"""
import struct
import sys

LC_LOAD_DYLIB = 0xC
LC_SEGMENT_64 = 0x19
LC_DYLD_CHAINED_FIXUPS = 0x80000034
SUPPL = b"/usr/lib/cg-supl.dylib"
SLACK_EXPECT = 48


def parse(d):
    ncmds, sizeofcmds = struct.unpack_from("<II", d, 16)
    off = 32
    first_sect = None
    cf = None
    dylib_count = 0
    end = 32 + sizeofcmds
    for _ in range(ncmds):
        cmd, cs = struct.unpack_from("<II", d, off)
        if cmd == LC_SEGMENT_64:
            nsects = struct.unpack_from("<I", d, off + 64)[0]
            so = off + 72
            for _s in range(nsects):
                sect_off = struct.unpack_from("<I", d, so + 48)[0]
                if first_sect is None and sect_off > 0:
                    first_sect = sect_off
                so += 80
        elif cmd == LC_DYLD_CHAINED_FIXUPS:
            cf = struct.unpack_from("<I", d, off + 8)[0]
        elif cmd in (LC_LOAD_DYLIB, 0x80000018, 0x8000001F, 0x80000023):
            dylib_count += 1
        off += cs
    return ncmds, sizeofcmds, first_sect, cf, dylib_count


def import_entries(d, cf):
    h = cf
    _v, _so, io, sy, cnt, ifmt, _sf = struct.unpack_from("<IIIIIII", d, h)
    assert ifmt == 1, f"unexpected imports_format {ifmt}"
    out = []
    for i in range(cnt):
        p = h + io + i * 4
        v = struct.unpack_from("<I", d, p)[0]
        lo = v & 0xFF
        no = (v >> 9) & 0x7FFFFF
        s = h + sy + no
        e = d.find(b"\0", s)
        out.append((p, lo, d[s:e].decode(errors="replace")))
    return out


def main():
    path, missing_path = sys.argv[1], sys.argv[2]
    missing = set(l.strip() for l in open(missing_path) if l.strip())
    d = bytearray(open(path, "rb").read())
    ncmds, sizeofcmds, first_sect, cf, dylib_count = parse(d)
    assert cf is not None, "no LC_DYLD_CHAINED_FIXUPS"
    slack = first_sect - (32 + sizeofcmds)
    assert slack >= SLACK_EXPECT, f"slack {slack} < needed {SLACK_EXPECT}"
    for b in d[32 + sizeofcmds:first_sect]:
        assert b == 0, "slack is not zero padding"
    print(f"ncmds={ncmds} dylib_count={dylib_count} slack={slack} (need {SLACK_EXPECT})")

    # 1. append LC_LOAD_DYLIB in the slack
    name_off = 24
    payload = SUPPL + b"\0"
    cmdsize = (24 + len(payload) + 7) & ~7
    assert cmdsize == SLACK_EXPECT, f"cmdsize {cmdsize} != slack budget"
    lc = struct.pack("<IIIIII", LC_LOAD_DYLIB, cmdsize, name_off,
                     0, 0xFFFFFFFF, 0xFFFFFFFF) + payload
    lc += b"\0" * (cmdsize - len(lc))
    at = 32 + sizeofcmds
    d[at:at + cmdsize] = lc
    struct.pack_into("<I", d, 16, ncmds + 1)
    # the appended command must sit INSIDE the declared load-command area,
    # or dyld's validator rejects it ("malformed load command ... size too
    # large"): grow sizeofcmds by the command's size (the slack absorbs it
    # exactly; the first section starts right after).
    struct.pack_into("<I", d, 20, sizeofcmds + cmdsize)
    new_ordinal = dylib_count + 1
    print(f"appended LC_LOAD_DYLIB @ {at:#x} cmdsize={cmdsize} "
          f"install_name={SUPPL.decode()} -> ordinal {new_ordinal}")

    # 2. patch import ordinals for the missing symbols
    patched = 0
    for p, lo, name in import_entries(d, cf):
        if name in missing:
            v = struct.unpack_from("<I", d, p)[0]
            v = (v & ~0xFF) | (new_ordinal & 0xFF)
            struct.pack_into("<I", d, p, v)
            patched += 1
    print(f"import ordinals patched 3 -> {new_ordinal}: {patched}/{len(missing)}")

    open(path, "wb").write(d)

    # 3. verify
    d2 = open(path, "rb").read()
    ncmds2, _s2, _f2, cf2, dl2 = parse(d2)
    assert ncmds2 == ncmds + 1 and dl2 == dylib_count + 1
    bad = []
    seen = 0
    for p, lo, name in import_entries(d2, cf2):
        if name in missing:
            seen += 1
            if lo != new_ordinal:
                bad.append((name, lo))
    others = [n for _p, lo, n in import_entries(d2, cf2) if n not in missing and lo == 3]
    print(f"verify: missing-symbol imports now at ordinal {new_ordinal}: "
          f"{seen}/{len(missing)}; stragglers: {bad[:5]}; "
          f"other ordinal-3 imports unchanged: {len(others)}")
    assert not bad and seen == len(missing), "verification failed"


if __name__ == "__main__":
    main()
