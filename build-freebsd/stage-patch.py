#!/usr/bin/env python3
"""stage-patch.py — patch LC_ID_DYLIB version fields of a staged Mach-O.

Usage: stage-patch.py <mach-o> <current_version> <compatibility_version>
Walks load commands exactly (thin + fat, all slices); patches current_version
(cmd+16) and compatibility_version (cmd+20) of every LC_ID_DYLIB; prints
before/after and read-back hex of the patched fields.
"""
import struct
import sys


def walk(data, base, size, cur, compat, log, do_write):
    magic = struct.unpack_from("<I", data, base)[0]
    if magic == 0xfeedfacf:
        slices = [(base, size)]
    elif magic in (0xcafebabe, 0xbebafeca):
        bo = ">" if magic == 0xcafebabe else "<"
        n = struct.unpack_from(bo + "I", data, base + 4)[0]
        slices = []
        for s in range(n):
            off = base + 8 + s * 20
            _ct, _cs, offset, sz, _al = struct.unpack_from(bo + "IIIII", data, off)
            slices.append((base + offset, sz))
    else:
        raise SystemExit(f"not a Mach-O: magic={magic:#x}")
    found = 0
    for sbase, ssize in slices:
        magic = struct.unpack_from("<I", data, sbase)[0]
        if magic != 0xfeedfacf:
            raise SystemExit(f"slice magic {magic:#x}, expected 0xfeedfacf")
        ncmds = struct.unpack_from("<I", data, sbase + 16)[0]
        o = sbase + 32
        for _ in range(ncmds):
            cmd, cmdsize = struct.unpack_from("<II", data, o)
            if cmdsize < 8:
                raise SystemExit(f"bad cmdsize {cmdsize} @ {o:#x}")
            if cmd == 0xd:
                stroff = struct.unpack_from("<I", data, o + 8)[0]
                name = data[o + stroff:o + cmdsize].split(b"\x00")[0].decode(errors="replace")
                old_cur = struct.unpack_from("<I", data, o + 16)[0]
                old_compat = struct.unpack_from("<I", data, o + 20)[0]
                if do_write:
                    struct.pack_into("<I", data, o + 16, cur)
                    struct.pack_into("<I", data, o + 20, compat)
                new_cur = struct.unpack_from("<I", data, o + 16)[0]
                new_compat = struct.unpack_from("<I", data, o + 20)[0]
                log.append(
                    f"LC_ID_DYLIB slice@{sbase:#x} cmd@{o:#x}: {name}\n"
                    f"  cur    {old_cur:#010x} -> {new_cur:#010x}  (field file-off {o + 16})\n"
                    f"  compat {old_compat:#010x} -> {new_compat:#010x}  (field file-off {o + 20})\n"
                    f"  read-back compat bytes: {data[o + 20:o + 24].hex(' ')}"
                )
                found += 1
            o += cmdsize
    if not found:
        raise SystemExit("no LC_ID_DYLIB found")


def main():
    path, cur_s, compat_s = sys.argv[1], sys.argv[2], sys.argv[3]
    cur = int(cur_s, 0)
    compat = int(compat_s, 0)
    data = bytearray(open(path, "rb").read())
    log = []
    walk(data, 0, len(data), cur, compat, log, do_write=True)
    open(path, "wb").write(data)
    data2 = open(path, "rb").read()
    log2 = []
    walk(data2, 0, len(data2), 0, 0, log2, do_write=False)  # verify parse still valid
    print("\n".join(log))
    print("READ-BACK (re-parsed from disk):")
    print("\n".join(log2))


if __name__ == "__main__":
    main()
