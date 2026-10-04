#!/usr/bin/env python3
"""rewrite-dylib-name.py — in-place rewrite of a dylib's load-command names.

Handles LC_ID_DYLIB / LC_LOAD_DYLIB / LC_REEXPORT_DYLIB / LC_LOAD_WEAK_DYLIB /
LC_LOAD_UPWARD_DYLIB.  Thin and FAT (universal) Mach-O are both walked; for FAT
every architecture slice is rewritten.

The load-command walk is strictly by cmdsize: an unknown command (including the
Darling 0x1e command llvm-install-name-tool chokes on) is skipped, never parsed.
The new name must fit the ORIGINAL name slot (cmdsize - name.offset) and is
written with NUL padding; load commands are never shifted (regression lesson:
a 1-byte shift corrupts the file — PLAN 9.9).

Usage:
  rewrite-dylib-name.py <file> <old-name> <new-name> [--id-only|--load-only|--all]

--all (default): every dylib command whose name equals <old-name>.
--id-only:      only LC_ID_DYLIB.
--load-only:    only LC_LOAD/REEXPORT/WEAK/UPWARD_DYLIB (never the ID).
"""
import struct
import sys

LC_ID_DYLIB = 0xD
LC_LOAD_DYLIB = 0xC
LC_LOAD_WEAK_DYLIB = 0x18
LC_REEXPORT_DYLIB = 0x1F
LC_LOAD_UPWARD_DYLIB = 0x23

DYLIB_CMDS = {
    LC_ID_DYLIB,
    LC_LOAD_DYLIB,
    LC_LOAD_WEAK_DYLIB,
    LC_REEXPORT_DYLIB,
    LC_LOAD_UPWARD_DYLIB,
}
LOAD_CMDS = DYLIB_CMDS - {LC_ID_DYLIB}

MH_MAGIC = 0xFEEDFACE
MH_CIGAM = 0xCEFAEDFE
MH_MAGIC_64 = 0xFEEDFACF
MH_CIGAM_64 = 0xCFFAEDFE
FAT_MAGIC = 0xCAFEBABE
FAT_CIGAM = 0xBEBAFECA


def _rewrite_slice(data, base, size, old, new, mode):
    """Rewrite one Mach-O slice located at data[base:base+size].  Returns count."""
    magic = struct.unpack_from("<I", data, base)[0]
    if magic in (MH_MAGIC, MH_CIGAM):
        hdr = 28
    elif magic in (MH_MAGIC_64, MH_CIGAM_64):
        hdr = 32
    else:
        return 0
    ncmds = struct.unpack_from("<I", data, base + 16)[0]
    off = base + hdr
    end = base + size
    count = 0
    for _ in range(ncmds):
        if off + 8 > end:
            break
        cmd, cmdsize = struct.unpack_from("<II", data, off)
        if cmdsize < 8 or off + cmdsize > end:
            break
        if cmd in DYLIB_CMDS:
            if mode == "id-only" and cmd != LC_ID_DYLIB:
                off += cmdsize
                continue
            if mode == "load-only" and cmd not in LOAD_CMDS:
                off += cmdsize
                continue
            name_off = struct.unpack_from("<I", data, off + 8)[0]
            name_start = off + name_off
            slot = cmdsize - name_off
            if name_off >= cmdsize or slot <= 0:
                off += cmdsize
                continue
            raw = data[name_start:name_start + slot]
            cur = raw.split(b"\0", 1)[0].decode("utf-8", "replace")
            if cur == old:
                nb = new.encode("utf-8")
                if len(nb) + 1 > slot:
                    raise SystemExit(
                        f"FATAL: new name {new!r} ({len(nb)+1} B incl NUL) does not "
                        f"fit slot {slot} B for {cur!r} (cmdsize {cmdsize}, off {name_off})"
                    )
                data[name_start:name_start + slot] = nb + b"\0" * (slot - len(nb))
                count += 1
        off += cmdsize
    return count


def rewrite(path, old, new, mode):
    with open(path, "rb") as f:
        data = bytearray(f.read())
    magic = struct.unpack_from(">I", data, 0)[0]
    total = 0
    if magic in (FAT_MAGIC, FAT_CIGAM):
        nfat = struct.unpack_from(">I", data, 4)[0]
        for i in range(nfat):
            cputype, cpusubtype, offset, size, align = struct.unpack_from(
                ">IIIII", data, 8 + i * 20)
            total += _rewrite_slice(data, offset, size, old, new, mode)
    else:
        total = _rewrite_slice(data, 0, len(data), old, new, mode)
    with open(path, "wb") as f:
        f.write(data)
    return total


def main():
    args = [a for a in sys.argv[1:]]
    mode = "all"
    for flag, m in (("--id-only", "id-only"), ("--load-only", "load-only"),
                    ("--all", "all")):
        if flag in args:
            args.remove(flag)
            mode = m
    if len(args) != 3:
        raise SystemExit(__doc__)
    path, old, new = args
    n = rewrite(path, old, new, mode)
    print(f"rewrote {n} load command(s) in {path}: {old!r} -> {new!r} (mode {mode})")
    if n == 0:
        raise SystemExit("FATAL: no matching load command rewritten")


if __name__ == "__main__":
    main()
