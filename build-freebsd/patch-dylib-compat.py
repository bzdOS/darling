#!/usr/bin/env python3
"""patch-dylib-compat.py — set a Mach-O dylib's current and compatibility
versions to 0xFFFFFFFF (65535.255.255, the series' provider convention).

The guest loader refuses a dylib whose compatibility version is lower than
what the loading binary requires — dyld3/ClosureBuilder.cpp:
    if ((foundCompatVers < compatVersion) && mh->enforceCompatVersion())
        error("Incompatible library version: '%s' requires version X or
               later, but '%s' provides version Y")
This patches the LC_ID_DYLIB version words so an overlay-provided dylib can
never fail that gate. Idempotent; works on thin and fat inputs (the x86_64
slice is chosen in a fat image). Verifies the write by re-reading.

Usage: python3 patch-dylib-compat.py <macho> [<macho> ...]
"""
import struct
import sys

LC_ID_DYLIB = 0x0D
LC_SEGMENT_64 = 0x19


def patch_thin(d):
    ncmds = struct.unpack_from("<I", d, 16)[0]
    off = 32
    patched = False
    for _ in range(ncmds):
        cmd, cs = struct.unpack_from("<II", d, off)
        if cmd == LC_ID_DYLIB:
            # struct dylib_command { cmd, cmdsize, dylib{name lc_str, timestamp,
            # current_version, compatibility_version} }; dylib starts at +8:
            # name(4) timestamp(4) current(4) compat(4)
            cur = off + 16
            old_cur, old_compat = struct.unpack_from("<II", d, cur)
            if (old_cur, old_compat) != (0xFFFFFFFF, 0xFFFFFFFF):
                struct.pack_into("<I", d, cur, 0xFFFFFFFF)
                struct.pack_into("<I", d, cur + 4, 0xFFFFFFFF)
                patched = True
                print(f"  LC_ID_DYLIB: current {old_cur:#x}/{old_cur >> 16}.{old_cur & 0xFFFF:#x} "
                      f"-> 0xffffffff, compat {old_compat:#x} -> 0xffffffff")
            else:
                print("  LC_ID_DYLIB already 0xffffffff/0xffffffff (idempotent)")
        off += cs
    return patched


def main():
    for path in sys.argv[1:]:
        with open(path, "rb") as f:
            d = bytearray(f.read())
        print(f"{path}: {len(d)} bytes")
        if d[:4] == b"\xca\xfe\xba\xbe":
            nfat = struct.unpack_from(">I", d, 4)[0]
            for i in range(nfat):
                cputype, cpusub, off, size, _al = struct.unpack_from(">iiIII", d, 8 + i * 20)
                if cputype == 0x01000007:
                    print(f"  fat image: patching the x86_64 slice at {off:#x}")
                    patch_thin(memoryview(d)[off:])
                    break
        elif d[:4] == b"\xcf\xfa\xed\xfe":
            patch_thin(d)
        else:
            print("  not a mach-o — skipped")
            continue
        with open(path, "wb") as f:
            f.write(bytes(d))
        print("  written")


if __name__ == "__main__":
    main()
