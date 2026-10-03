#!/usr/bin/env python3
"""stage-patch.py — patch LC_ID_DYLIB version fields in staged Mach-Os.

Modes:
  stage-patch.py <mach-o> <cur> <compat>   one file
  stage-patch.py --paths-file <file>       every listed path, cur=compat=0xFFFFFFFF

Walks load commands exactly (thin + fat, all slices); patches
current_version (cmd+16) and compatibility_version (cmd+20) of every
LC_ID_DYLIB found. Prints per-file before/after and a re-parsed read-back;
paths without a Mach-O magic or without LC_ID_DYLIB are reported, not fatal.
Stage copies only — the live overlay is never touched.
"""
import os
import struct
import sys


def walk(data, base, size, cur, compat, entries, do_write):
    mb = bytes(data[base:base + 4])
    if mb == b"\xcf\xfa\xed\xfe":
        slices = [(base, size)]
    elif mb in (b"\xca\xfe\xba\xbe", b"\xbe\xba\xfe\xca"):
        bo = ">" if mb == b"\xca\xfe\xba\xbe" else "<"
        n = struct.unpack_from(bo + "I", data, base + 4)[0]
        slices = []
        for s in range(n):
            off = base + 8 + s * 20
            _ct, _cs, offset, sz, _al = struct.unpack_from(bo + "IIIII", data, off)
            slices.append((base + offset, sz))
    else:
        return 0, f"not a Mach-O (magic bytes {mb.hex()})"
    found = 0
    for sbase, ssize in slices:
        if bytes(data[sbase:sbase + 4]) != b"\xcf\xfa\xed\xfe":
            return found, f"slice magic {bytes(data[sbase:sbase + 4]).hex()}, expected feedfacf"
        ncmds = struct.unpack_from("<I", data, sbase + 16)[0]
        o = sbase + 32
        for _ in range(ncmds):
            cmd, cmdsize = struct.unpack_from("<II", data, o)
            if cmdsize < 8:
                return found, f"bad cmdsize {cmdsize} @ {o:#x}"
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
                entries.append(
                    (name, o + 16, old_cur, new_cur, o + 20, old_compat, new_compat)
                )
                found += 1
            o += cmdsize
    return found, None


def patch_one(path, cur, compat):
    data = bytearray(open(path, "rb").read())
    probe = []
    _n, err = walk(data, 0, len(data), cur, compat, probe, False)
    if err and not probe:
        return f"SKIP ({err}): {path}"
    entries = []
    walk(data, 0, len(data), cur, compat, entries, do_write=True)
    open(path, "wb").write(data)
    data2 = open(path, "rb").read()
    entries2 = []
    walk(data2, 0, len(data2), 0, 0, entries2, do_write=False)
    ok = len(entries2) == len(entries) and all(
        e2[3] == 0xFFFFFFFF and e2[6] == 0xFFFFFFFF for e2 in entries2
    )
    summary = "; ".join(
        f"{e[0].rsplit('/', 1)[-1]} cur {e[2]:#010x}->{e[3]:#010x} "
        f"compat {e[5]:#010x}->{e[6]:#010x}" for e in entries
    )
    return f"{'OK' if ok else 'VERIFY-FAIL'} [{len(entries)} LC_ID] {summary} <- {path}"


def main():
    args = sys.argv[1:]
    if args and args[0] == "--paths-file":
        paths = [l.strip() for l in open(args[1]) if l.strip()]
        cur = compat = 0xFFFFFFFF
    else:
        paths = [args[0]]
        cur = int(args[1], 0)
        compat = int(args[2], 0)
    patched = 0
    for p in paths:
        if not os.path.exists(p):
            print(f"ABSENT: {p}")
            continue
        try:
            res = patch_one(p, cur, compat)
        except Exception as e:
            res = f"SKIP ({type(e).__name__}: {e}): {p}"
        print(res)
        if res.startswith("OK"):
            patched += 1
    print(f"SUMMARY: patched={patched}/{len(paths)} (read-backs must be 0xffffffff/0xffffffff)")


if __name__ == "__main__":
    main()
