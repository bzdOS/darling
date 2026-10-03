#!/usr/bin/env python3
"""sym-wall-map.py — symbol-wall map for a chained-fixups Mach-O closure.

For one or more Mach-Os (the Chrome framework + its Libraries/*.dylib),
walks the LC_DYLD_CHAINED_FIXUPS imports per dylib ordinal, maps each
ordinal's dylib path to a file under the overlay root, diffs the imports
against that stub's exports (nm -gU), and reports:

  provider -> missing count -> symbol classes
  unstaged providers (no file under the overlay root) — listed separately,
  they are staging gaps, not symbol walls.

Writes the union of all missing symbols (one per line) to <out-missing>.

Usage:
  sym-wall-map.py <overlay-root> <out-missing> <macho> [<macho> ...]
"""
import collections
import subprocess
import sys

LC_LOAD_DYLIB = 0xC
LC_LOAD_WEAK = 0x80000018
LC_REEXPORT = 0x8000001F
LC_UPWARD = 0x80000023
LC_SEGMENT_64 = 0x19
LC_DYLD_CHAINED_FIXUPS = 0x80000034
DYLIB_LCS = (LC_LOAD_DYLIB, LC_LOAD_WEAK, LC_REEXPORT, LC_UPWARD)


def parse_macho(d):
    ncmds = int.from_bytes(d[16:20], "little")
    off = 32
    libs = []
    cf = None
    for _ in range(ncmds):
        cmd = int.from_bytes(d[off:off + 4], "little")
        cs = int.from_bytes(d[off + 4:off + 8], "little")
        if cmd in DYLIB_LCS:
            stroff = int.from_bytes(d[off + 8:off + 12], "little")
            name = d[off + stroff:off + cs].split(b"\0")[0].decode(errors="replace")
            libs.append(name)
        elif cmd == LC_DYLD_CHAINED_FIXUPS:
            cf = int.from_bytes(d[off + 8:off + 12], "little")
        off += cs
    if cf is None:
        return libs, None
    h = cf
    _v, _so, io, sy, cnt, ifmt, _sf = (
        int.from_bytes(d[h:h + 4], "little"),
        int.from_bytes(d[h + 4:h + 8], "little"),
        int.from_bytes(d[h + 8:h + 12], "little"),
        int.from_bytes(d[h + 12:h + 16], "little"),
        int.from_bytes(d[h + 16:h + 20], "little"),
        int.from_bytes(d[h + 20:h + 24], "little"),
        int.from_bytes(d[h + 24:h + 28], "little"),
    )
    assert ifmt == 1, f"unexpected imports_format {ifmt}"
    out = []
    for i in range(cnt):
        p = h + io + i * 4
        v = int.from_bytes(d[p:p + 4], "little")
        lo = v & 0xFF
        no = (v >> 9) & 0x7FFFFF
        s = h + sy + no
        e = d.find(b"\0", s)
        out.append((lo, d[s:e].decode(errors="replace")))
    return libs, out


def exports_of(path):
    if path.endswith("/usr/lib/libSystem.B.dylib"):
        # the umbrella re-exports /usr/lib/system/*; its own export table is
        # not the provider set (the 931 "missing" pthread/launch_data symbols
        # otherwise are reexport artifacts, not walls)
        paths = [path]
        import glob
        paths += glob.glob(path.rsplit("/usr/lib/", 1)[0] + "/usr/lib/system/*.dylib")
        have = set()
        for p in paths:
            have |= exports_of_file(p)
        return have
    return exports_of_file(path)


def exports_of_file(path):
    r = subprocess.run(["nm", "-gU", path], capture_output=True, text=True)
    have = set()
    for line in r.stdout.splitlines():
        parts = line.split()
        if len(parts) >= 3:
            have.add(parts[2])
    return have


def klass(sym):
    for p in ("_kCF", "_kCG", "_kCA", "_kCM", "_kCV", "_objc", "___OBJC",
              "_NS", "_CF", "_CG", "_IO", "_IOSurface", "_CMSample",
              "_CMFormat", "_AV", "_MTL", "_WK"):
        if sym.startswith(p):
            return p.lstrip("_").lstrip("_") or p.strip("_")
    return sym.lstrip("_")[:12]


def main():
    overlay = sys.argv[1]
    out_missing = sys.argv[2]
    binaries = sys.argv[3:]
    union = set()
    print(f"{'provider':60s} {'missing':>8s}  classes")
    unstaged = []
    for binpath in binaries:
        d = open(binpath, "rb").read()
        libs, imps = parse_macho(d)
        if imps is None:
            print(f"[{binpath.rsplit('/', 1)[-1]}] no LC_DYLD_CHAINED_FIXUPS — skipped")
            continue
        by_ord = collections.defaultdict(set)
        for lo, nm in imps:
            by_ord[lo].add(nm)
        for lo in sorted(by_ord):
            if lo <= 0 or lo > len(libs):
                continue
            prov = libs[lo - 1]
            ov_path = overlay + prov
            imports = by_ord[lo]
            try:
                have = exports_of(ov_path)
            except FileNotFoundError:
                unstaged.append((prov, len(imports), binpath.rsplit("/", 1)[-1]))
                continue
            missing = imports - have
            if not missing:
                continue
            union |= missing
            classes = collections.Counter(klass(s) for s in missing)
            top = ", ".join(f"{k}×{v}" for k, v in classes.most_common(3))
            print(f"{prov[-58:]:60s} {len(missing):>8d}  {top}")
    print()
    print(f"UNSTAGED providers (staging gaps, not symbol walls): {len(unstaged)}")
    for prov, n, src in unstaged:
        print(f"  {prov}  ({n} imports from {src})")
    print()
    print(f"union of missing symbols: {len(union)}")
    with open(out_missing, "w") as f:
        f.write("\n".join(sorted(union)) + "\n")
    print(f"written: {out_missing}")


if __name__ == "__main__":
    main()
