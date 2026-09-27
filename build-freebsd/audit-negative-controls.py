#!/usr/bin/env python3
"""Build one negative control per UNGUARDED read in trieWalk, and check that
--audit fails on each of them for the right reason.

This is the falsifiability gate for the audit. An audit that cannot fail proves
nothing, and a fixture that trips the WRONG check proves the checks are not
independent. So each control below aims at exactly one read, and the run is only
green if each one FAILS at its own line.

Note the FF FF FF 7F fixture from the earlier slice is deliberately NOT here.
That one is caught by 1848 -- dyld2 logs "terminalSize extends past end of
trie" and returns NULL -- so it is a GUARDED finding and the audit correctly
reports it without failing. Making it a FAIL would have meant calling a caught
and refused read a fault. These four are the reads that have no guard at all.

  1837  terminalSize = *p++            node offset == end. 1889 tests
                                        (&start[nodeOffset] > end) and
                                        &start[end] == end, so an offset of exactly
                                        `end` is ACCEPTED and 1837 then reads one
                                        byte past the end
  1853  childrenRemaining = *children++ children == end (1848 tests `>`, not `>=`)
  1862  the edge scan                   an edge with no NUL before end
  1876  child-uleb continuation         continuation bytes with the high bit set
                                         running to the last byte

Writes only into a scratch directory. Reads the overlay, never writes it.
"""
import os
import struct
import subprocess
import sys

_HERE = os.path.dirname(os.path.abspath(__file__))
EM = os.environ.get("EMULATOR", os.path.join(_HERE, "chrome-trie-emulator.py"))
OUT = sys.argv[1] if len(sys.argv) > 1 else os.path.join(
    os.environ.get("TMPDIR", "/tmp"), "audit-negative-controls")


def pick_source():
    """A thin 64-bit dylib from the overlay to corrupt.

    Discovered rather than hardcoded, so no machine's paths end up in the tree.
    A FAT file would do as well -- the export blob is per-slice and this only
    needs the trie -- but a thin one keeps the offsets obvious.
    """
    overlay = os.environ.get("DARLING_OVERLAY")
    if not overlay:
        raise SystemExit("set DARLING_OVERLAY to the overlay dir to pick a "
                         "source dylib from")
    for root in ("usr/lib", "usr/lib/system"):
        base = os.path.join(overlay, root)
        if not os.path.isdir(base):
            continue
        for name in sorted(os.listdir(base)):
            if not name.endswith(".dylib"):
                continue
            p = os.path.join(base, name)
            try:
                with open(p, "rb") as f:
                    if struct.unpack_from("<I", f.read(4), 0)[0] == 0xFEEDFACF:
                        return p
            except OSError:
                continue
    raise SystemExit("no thin MH_MAGIC_64 .dylib found under %s" % overlay)


SRC = None  # set in main(), once DARLING_OVERLAY has been consulted


def export_blob(d):
    """(off, size) of the export trie, on the x86_64 slice if fat."""
    if struct.unpack_from(">I", d, 0)[0] in (0xCAFEBABE, 0xCAFEBABF):
        n = struct.unpack_from(">I", d, 4)[0]
        for i in range(n):
            cput, _sub, off, size, _al = struct.unpack_from(">IIIII", d, 8 + i * 20)
            if cput == 0x01000007:
                return export_blob(d[off:off + size])
    n = struct.unpack_from("<I", d, 16)[0]
    o = 32
    for _ in range(n):
        cmd, cs = struct.unpack_from("<II", d, o)
        if cmd in (0x22, 0x80000022):
            f = struct.unpack_from("<10I", d, o + 8)
            return f[8], f[9]
        o += cs
    raise SystemExit("no export blob in %s" % SRC)


def uleb(v):
    out = bytearray()
    while True:
        b = v & 0x7F
        v >>= 7
        out.append(b | (0x80 if v else 0))
        if not v:
            return bytes(out)


def build(name, mutate):
    d = bytearray(open(SRC, "rb").read())
    off, size = export_blob(d)
    mutate(d, off, size)
    p = os.path.join(OUT, name)
    with open(p, "wb") as f:
        f.write(bytes(d))
    return p, size


# --- 1837: a child offset of exactly `end` -----------------------------------
def c1837(d, off, size):
    # root: terminalSize 0, one child whose edge is "_" and whose uleb is `size`
    # (== end). 1889 accepts it; 1837 then reads buf[end].
    d[off + 0] = 0                       # terminalSize
    d[off + 1] = 1                       # childrenRemaining
    d[off + 2] = ord("_")                # edge
    d[off + 3] = 0                       # edge terminator
    d[off + 4:off + 4 + len(uleb(size))] = uleb(size)


# --- 1853: children == end ----------------------------------------------------
def c1853(d, off, size):
    # root terminalSize is a 2-byte uleb chosen so p + terminalSize == end
    tsz = size - 2
    d[off:off + 2] = uleb(tsz)
    for i in range(2, size):
        d[off + i] = 0x41                # payload filler, no NUL


# --- 1862: an edge with no NUL before the end --------------------------------
def c1862(d, off, size):
    d[off + 0] = 0                       # terminalSize
    d[off + 1] = 1                       # childrenRemaining
    for i in range(2, size):
        d[off + i] = 0x41                # edge with no terminator anywhere


# --- 1876: continuation bytes running to the last byte ------------------------
def c1876(d, off, size):
    d[off + 0] = 0                       # terminalSize
    d[off + 1] = 1                       # childrenRemaining
    d[off + 2] = ord("_")                # edge
    d[off + 3] = 0                       # edge terminator
    d[off + 4:size] = b"\xff" * (size - 4)   # every byte a continuation byte


CASES = [
    ("1837-terminalsize-past-end.dylib", c1837, 1837),
    ("1853-children-at-end.dylib", c1853, 1853),
    ("1862-edge-scan-past-end.dylib", c1862, 1862),
    ("1876-uleb-skip-past-end.dylib", c1876, 1876),
]


def main():
    global SRC
    SRC = pick_source()
    print("source dylib: %s" % SRC)
    os.makedirs(OUT, exist_ok=True)
    bad = 0
    for name, fn, want_line in CASES:
        p, size = build(name, fn)
        r = subprocess.run([sys.executable, EM, "--audit", p],
                           capture_output=True, text=True)
        out = r.stdout + r.stderr
        verdict_fail = "VERDICT: FAIL" in out
        unchecked = [l for l in out.splitlines()
                     if l.strip().startswith("0x") and "ImageLoader.cpp:" in l
                     and "GUARDED" not in l]
        hit = [l for l in unchecked if "ImageLoader.cpp:%d" % want_line in l]
        ok = verdict_fail and r.returncode == 1 and hit
        print("%-34s size=0x%-4x rc=%d  FAIL=%-5s  finding at :%d -> %s"
              % (name, size, r.returncode, verdict_fail, want_line,
                 "OK" if ok else "WRONG"))
        if not ok:
            bad += 1
            print("    ---- audit output ----")
            for l in out.splitlines():
                print("    " + l)
    # and the untouched file must PASS, or "always FAIL" would be no better
    r = subprocess.run([sys.executable, EM, "--audit", SRC],
                       capture_output=True, text=True)
    clean = "VERDICT: PASS" in (r.stdout + r.stderr) and r.returncode == 0
    print("%-34s %s" % ("pristine " + os.path.basename(SRC),
                        "PASS" if clean else "NOT PASS"))
    if not clean:
        bad += 1
    print()
    print("negative controls: %s" % ("ALL OK" if bad == 0 else "%d PROBLEM(S)" % bad))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
