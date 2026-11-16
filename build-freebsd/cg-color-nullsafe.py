#!/usr/bin/env python3
"""cg-color-nullsafe.py - make the CGColor accessors of the installed CoreGraphics answer 0 for a NULL colour.

The installed CoreGraphics exports each of CGColorGetColorSpace/GetNumberOfComponents/GetComponents/GetAlpha/GetPattern
and CGColorRetain/Release as the same 27 byte forwarder to the Onyx2D function (frame, store the argument, call,
unframe), and the Onyx2D function reads a field of its argument: a NULL colour crashed at address 0x10. Apple's CoreGraphics
answers 0 (0.0 for the alpha) for NULL. Each forwarder is rewritten in place, inside its own 32 byte slot:

    test %rdi,%rdi ; je zero ; jmp <the callee> ; zero: xor %eax,%eax ; pxor %xmm0,%xmm0 ; ret

The patch is exact and verified: only a forwarder that still has the stock bytes is rewritten, one already rewritten is
left alone, any other body stops the run. The image is kept as <image>.pre-cgcolor the first time.

Usage: cg-color-nullsafe.py <CoreGraphics image>          Output: CG-COLOR-NULLSAFE patched=<n> already=<n> names=<list>
       cg-color-nullsafe.py --check <CoreGraphics image>  read-only; one line CG-COLOR-NULLSAFE-CHECK ok|FAIL, rc 1 on FAIL"""
import os
import shutil
import struct
import subprocess
import sys

NAMES = ["GetColorSpace", "GetNumberOfComponents", "GetComponents", "GetAlpha", "GetPattern", "Retain", "Release"]
HEAD = bytes.fromhex("554889e54883ec104889 7df8488b7df8e8".replace(" ", ""))
TAIL = bytes.fromhex("4883c4105dc3")
NEW_HEAD = bytes.fromhex("4885ff7405e9")          # test rdi,rdi ; je +5 ; jmp rel32
ZERO = bytes.fromhex("31c0660fefc0c3")           # xor eax,eax ; pxor xmm0,xmm0 ; ret


def symbols(path):
    out = subprocess.run(["llvm-nm", path], check=True, capture_output=True, text=True).stdout
    sy = {}
    for ln in out.splitlines():
        p = ln.split()
        if len(p) == 3 and p[1] == "T":
            sy[p[2]] = int(p[0], 16)
    return sy


def main():
    check = len(sys.argv) == 3 and sys.argv[1] == "--check"
    if len(sys.argv) != 2 and not check:
        print(__doc__)
        return 2
    path = sys.argv[-1]
    if check:
        try:
            sy = symbols(path)
            d = open(path, "rb").read()
            bad = [n for n in NAMES if sy.get("_CGColor" + n) is None
                   or d[sy["_CGColor" + n]:sy["_CGColor" + n] + len(NEW_HEAD)] != NEW_HEAD]
        except Exception as e:
            print("CG-COLOR-NULLSAFE-CHECK FAIL %s" % e)
            return 1
        if bad:
            print("CG-COLOR-NULLSAFE-CHECK FAIL not null-safe: %s" % ",".join(bad))
            return 1
        print("CG-COLOR-NULLSAFE-CHECK ok names=%d" % len(NAMES))
        return 0
    sy = symbols(path)
    d = bytearray(open(path, "rb").read())
    patched, already = [], []
    for n in NAMES:
        a = sy.get("_CGColor" + n)
        if a is None:
            print("CG-COLOR-NULLSAFE FAIL no _CGColor%s" % n)
            return 1
        # the __text section address equals its file offset in this image (checked by the stock bytes below)
        cur = bytes(d[a:a + 27])
        if cur[:len(NEW_HEAD)] == NEW_HEAD:
            already.append(n)
            continue
        if cur[:len(HEAD)] != HEAD or cur[len(HEAD) + 4:] != TAIL:
            print("CG-COLOR-NULLSAFE FAIL _CGColor%s has an unexpected body" % n)
            return 1
        rel = struct.unpack_from("<i", cur, len(HEAD))[0]
        target = a + len(HEAD) - 1 + 5 + rel          # the callq at a+16, its end at a+21
        new = NEW_HEAD
        jmp_end = a + len(NEW_HEAD) + 4
        new += struct.pack("<i", target - jmp_end) + ZERO
        d[a:a + len(new)] = new
        patched.append(n)
    if patched:
        bak = path + ".pre-cgcolor"
        if not os.path.exists(bak):
            shutil.copy2(path, bak)
        open(path, "wb").write(d)
    print("CG-COLOR-NULLSAFE patched=%d already=%d names=%s" % (len(patched), len(already), ",".join(patched + already)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
