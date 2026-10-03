#!/usr/bin/env python3
"""chrome-template-patch.py — rewrite the launcher's framework-path template.

The launcher holds the cstring
  "../Frameworks/Google Chrome for Testing Framework.framework/Versions/<v>/..."
which its code joins as dirname + "/" + template; with the guest binary at
"/chrome-macho" dirname = "/" and the constructed dlopen path becomes
"//../Frameworks/..." — the spelling that dies inside dyld. Dropping the
leading "../" makes the constructed path "//Frameworks/..." (kernel-normalized
"/Frameworks/..."), the spelling that opens.

Exact-length: the new string is written into the old string's own slot
(including its NUL); the remainder of the slot is zero-filled. No bytes move.
Load commands are re-parsed after the write and printed for verification.

Usage: chrome-template-patch.py <src> <dst>
"""
import struct
import sys

OLD = b"../Frameworks/Google Chrome for Testing Framework.framework/Versions/154.0.8029.0/Google Chrome for Testing Framework\x00"
NEW = b"Frameworks/Google Chrome for Testing Framework.framework/Versions/154.0.8029.0/Google Chrome for Testing Framework\x00"


def parse_lcs(d, label):
    magic = struct.unpack_from("<I", d, 0)[0]
    assert magic == 0xfeedfacf, f"{label}: not thin MH_MAGIC_64"
    ncmds = struct.unpack_from("<I", d, 16)[0]
    o = 32
    out = []
    for _ in range(ncmds):
        cmd, cmdsize = struct.unpack_from("<II", d, o)
        if cmd in (0xc, 0xd):
            stroff = struct.unpack_from("<I", d, o + 8)[0]
            ts, cur, compat = struct.unpack_from("<III", d, o + 12)
            name = d[o + stroff:o + cmdsize].split(b"\x00")[0].decode(errors="replace")
            kind = "LC_LOAD_DYLIB" if cmd == 0xc else "LC_ID_DYLIB"
            out.append(f"{kind} cmdsize={cmdsize} cur={cur:#010x} compat={compat:#010x} {name}")
        elif cmd == 0xe:
            stroff = struct.unpack_from("<I", d, o + 8)[0]
            name = d[o + stroff:o + cmdsize].split(b"\x00")[0].decode(errors="replace")
            out.append(f"LC_LOAD_DYLINKER cmdsize={cmdsize} {name}")
        o += cmdsize
    return out


def main():
    src, dst = sys.argv[1], sys.argv[2]
    data = bytearray(open(src, "rb").read())
    i = data.find(OLD)
    assert i != -1, "template cstring not found"
    assert data[i - 1:i] == b"\x00", "template not at a string boundary"
    slot = len(OLD)
    assert len(NEW) <= slot
    data[i:i + slot] = NEW + b"\x00" * (slot - len(NEW))
    open(dst, "wb").write(data)
    print(f"template @ {i}: slot={slot} new={len(NEW)} padded={slot - len(NEW)}")
    d2 = open(dst, "rb").read()
    j = d2.find(NEW)
    print(f"read-back: NEW at {j}, following bytes: {d2[j + len(NEW):j + len(NEW) + 8].hex(' ')}")
    print("LCs BEFORE:")
    for l in parse_lcs(open(src, "rb").read(), "src"):
        print(" ", l)
    print("LCs AFTER:")
    for l in parse_lcs(d2, "dst"):
        print(" ", l)


if __name__ == "__main__":
    main()
