#!/usr/bin/env python3
# check-guest-dylib-compat.py — verify that every dylib a guest binary wants is
# actually present in the overlay at a compatibility version high enough.
#
# WHY THIS EXISTS
#
# The guest loader refuses a dylib whose compatibility version is lower than
# the one the loading binary requires. In dyld's own words, at
# dyld3/ClosureBuilder.cpp:372:
#
#     if ( (foundCompatVers < compatVersion) && mh->enforceCompatVersion() )
#         error("found '%s' which has compat version (%s) which is less than
#                required (%s).  Needed by '%s'", ...)
#
# so a mismatch is a load failure that happens before any of our code runs, and
# it reads like a missing library rather than a version problem. The probe
# links Foundation and AppKit, and the overlay's copies of those declare
# themselves as 65535.255.255 -- which is exactly what the probe requires, so
# it passes. That is worth checking rather than assuming: the versions are
# picked by the linker from whichever dylibs it finds, and the overlay is
# rebuilt from time to time.
#
# One trap that makes this harder to eyeball: llvm-objdump prints the
# version of a dylib whose version words are 0xFFFFFFFF as "current version
# n/a / compatibility version n/a", which looks like "no version" and is
# really the 65535.255.255 sentinel. Read the raw words instead.
#
# USAGE: python3 build-freebsd/check-guest-dylib-compat.py <binary> <overlay>
#
# EXIT: 0 if every dependency resolves at a high enough version, 1 otherwise.

import os
import struct
import sys

MH_MAGIC_64 = 0xFEEDFACF
MH_CIGAM_64 = 0xCFFAEDFE
FAT_MAGIC = 0xCAFEBABE
FAT_CIGAM = 0xBEBAFECA
FAT_MAGIC_64 = 0xCAFEBABF
FAT_CIGAM_64 = 0xBFBAFECA
CPU_TYPE_X86_64 = 0x01000007
LC_LOAD_DYLIB = 0xC
LC_ID_DYLIB = 0xD
LC_REEXPORT_DYLIB = 0x8000001F
LC_LOAD_WEAK_DYLIB = 0x80000018
LC_LAZY_LOAD_DYLIB = 0x20
DYLIB_COMMANDS = (LC_LOAD_DYLIB, LC_ID_DYLIB, LC_REEXPORT_DYLIB,
                  LC_LOAD_WEAK_DYLIB, LC_LAZY_LOAD_DYLIB)


def vstr(v):
    return "%d.%d.%d" % ((v >> 16) & 0xFFFF, (v >> 8) & 0xFF, v & 0xFF)


def thin_slice(d):
    """Return a 64-bit thin Mach-O view of `d`.

    Several guest libraries in the overlay are universal (libSystem.B.dylib,
    libobjc.A.dylib, libicucore.A.dylib all carry x86_64 and arm64). dyld
    picks the x86_64 slice, so that is the slice whose LC_ID_DYLIB matters;
    reading the fat header instead would just fail to parse.
    """
    magic = struct.unpack_from(">I", d, 0)[0]
    if magic in (FAT_MAGIC, FAT_CIGAM):
        nfat = struct.unpack_from(">I", d, 4)[0]
        is64 = magic == FAT_MAGIC_64
        for i in range(nfat):
            base = 8 + i * (32 if is64 else 20)
            cputype, _sub, offset = struct.unpack_from(">III", d, base)
            if cputype == CPU_TYPE_X86_64:
                return d[offset:]
        raise SystemExit("universal binary with no x86_64 slice")
    return d


def commands(d):
    """yield (cmd, offset) for each load command"""
    magic, _, _, _, ncmds, _, _, _ = struct.unpack_from("<IiiIIIII", d, 0)
    if magic not in (MH_MAGIC_64, MH_CIGAM_64):
        raise SystemExit("not a 64-bit thin Mach-O: magic %08x" % magic)
    if magic == MH_CIGAM_64:
        raise SystemExit("big-endian Mach-O, not expected here")
    off = 32
    for _ in range(ncmds):
        cmd, cmdsize = struct.unpack_from("<II", d, off)
        if cmdsize == 0:
            break
        yield cmd, off
        off += cmdsize


def _dylib_name_and_compat(d, off, cmdsize):
    """(name, compat version) from a dylib load command.

    There are two layouts and getting this wrong reads ASCII out of the
    version fields and prints nonsense like "needs 12141.101.116":

      dylib_command    (32-bit name offset)  name@8,  current@16, compat@20
      dylib_command_64 (64-bit name offset)  name@8,  current@20, compat@24

    They are told apart by whether the 4-byte name offset yields a path.
    """
    name_off = struct.unpack_from("<I", d, off + 8)[0]
    end = d.index(b"\0", off + name_off)
    name = d[off + name_off:end].decode("utf-8", "replace")
    if name.startswith("/"):
        cur, comp = struct.unpack_from("<II", d, off + 16)
        return name, comp
    name_off = struct.unpack_from("<Q", d, off + 8)[0]
    end = d.index(b"\0", off + name_off)
    name = d[off + name_off:end].decode("utf-8", "replace")
    cur, comp = struct.unpack_from("<II", d, off + 24)
    return name, comp


def load_commands(d):
    """(install name, required compat version) for every dylib command"""
    d = thin_slice(d)
    out = []
    for cmd, off in commands(d):
        if cmd not in DYLIB_COMMANDS:
            continue
        cmdsize = struct.unpack_from("<I", d, off + 4)[0]
        out.append(_dylib_name_and_compat(d, off, cmdsize))
    return out


def id_dylib_compat(path):
    d = thin_slice(open(path, "rb").read())
    for cmd, off in commands(d):
        if cmd == LC_ID_DYLIB:
            _name, comp = _dylib_name_and_compat(d, off, 0)
            return comp
    return None


def main():
    if len(sys.argv) != 3:
        raise SystemExit(__doc__)
    binary, overlay = sys.argv[1], sys.argv[2]
    want = load_commands(open(binary, "rb").read())

    print("binary:  %s" % binary)
    print("overlay: %s" % overlay)
    print("dependencies: %d\n" % len(want))

    bad = 0
    for name, need in sorted(want):
        if not name.startswith("/"):
            print("SKIP   %-72s not an absolute guest path" % name)
            bad += 1
            continue
        path = os.path.join(overlay, name.lstrip("/"))
        if not os.path.exists(path):
            print("MISSING %-72s not in the overlay" % name)
            bad += 1
            continue
        have = id_dylib_compat(path)
        if have is None:
            print("NOID   %-72s has no LC_ID_DYLIB to compare against" % name)
            bad += 1
            continue
        # dyld's rule, verbatim: refuse when the found version is lower.
        if have < need:
            print("OLD    %-72s needs %s, overlay has %s"
                  % (name, vstr(need), vstr(have)))
            print("       dyld would refuse this: 'found ... which has compat")
            print("       version (%s) which is less than required (%s)'"
                  % (vstr(have), vstr(need)))
            bad += 1
        else:
            note = "  <- 0xffffffff, printed as n/a by llvm-objdump" if have == 0xFFFFFFFF else ""
            print("ok     %-72s needs %-12s overlay has %s%s"
                  % (name, vstr(need), vstr(have), note))

    print("\n%s: %d problem(s)" % ("FAIL" if bad else "PASS", bad))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
