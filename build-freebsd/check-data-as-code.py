#!/usr/bin/env python3
"""check-data-as-code.py -- fail if a generated stub dylib exports DATA as a code stub.

A stub generator that does not know a symbol is data emits `xorl %eax,%eax; ret`
for it. A consumer that binds the name through a non-lazy pointer then reads
code bytes as an object (a class, a block isa, a constant). This happened with
_vm_page_size, with the OpenDirectory classes, and with the ObjC class stubs of
the stub frameworks. This check catches the class of bug at build time.

A symbol exported from `__TEXT,__text` of a stub image is an offender when it is
really data, by any of these rules:
  a  ObjC metadata: _OBJC_CLASS_$_*, _OBJC_METACLASS_$_*, _OBJC_IVAR_$_*,
     _OBJC_EHTYPE_$_*
  b  known data names: _NSConcrete* / __NSConcrete* (block isa objects), the
     constant prefixes the generators treat as data (_kCG _kCM _kCA _kCV _kCI
     _kMT _kOD _kSec _kCF ...), _vm_page_*, __DefaultRuneLocale, _environ, ...
  c  consumer evidence: every consuming image (a Mach-O that loads the stub)
     binds the symbol only through non-lazy pointers (__got, __data, __const,
     __objc_* ... : no __stubs entry and no lazy pointer calls it)
  d  a provider dylib on disk (a library the stub re-exports or loads, plus
     the libSystem re-export closure) defines the same name in a __DATA section
Rules a and b are suppressed when a provider defines the name in __TEXT,__text
(then it is a real function with an unlucky name). Rule c is suppressed the same
way and by the allow list (--allow FILE, `symbol  reason` per line).

A consumer names its provider by load path (dyld ignores the install id), so a stub
is matched by its path under the overlay, or by the path after `=` in --stub FILE=PATH
(for a freshly built image that is not installed yet).

Stubs: every Mach-O dylib under <overlay>/usr/lib and
<overlay>/System/Library/{Frameworks,PrivateFrameworks} whose file is at most
256 KiB and whose exported code symbols average at most 48 bytes (stub shaped) or
whose name is a generated wrapper (*Extras.dylib, suppl.dylib, cg-supl.dylib),
plus every path given with --stub. Backups (*.bak, *.pre-*, *.baseline-*, ...)
are skipped. Consumers: every --consumer path, plus every overlay image of the
same scan that loads a stub.

Usage: check-data-as-code.py --overlay DIR [--consumer PATH ...] [--stub FILE[=LOADPATH] ...]
                             [--allow FILE] [--list-stubs]
Exit 1 and the list of offenders on any violation.
"""
import argparse
import bisect
import mmap
import os
import re
import struct
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from stub_data_rules import OBJC, BLOCKS, DATA_NAMES  # noqa: E402

MH_MAGIC_64 = 0xFEEDFACF
LC_SEGMENT_64 = 0x19
LC_ID_DYLIB = 0xD
LOAD_CMDS = {0xC: "load", 0x80000018: "weak", 0x8000001F: "reexport", 0x80000023: "upward"}
LC_DYLD_CHAINED_FIXUPS = 0x80000034

SKIP_NAME = re.compile(r"\.(bak|orig|old|save)$|\.bak|\.pre-|\.baseline-|\.objc\.|~$")
NONLAZY_GOT = ("__got", "__auth_got", "__la_symbol_ptr", "__nl_symbol_ptr", "__auth_ptr")


def run(*a):
    r = subprocess.run(a, capture_output=True, text=True)
    return r.stdout


def is_macho(path):
    try:
        with open(path, "rb") as f:
            return struct.unpack("<I", f.read(4))[0] == MH_MAGIC_64
    except (OSError, struct.error):
        return False


def header(path):
    """(install_id, [(kind, name)], [segments], [sections]) from the load commands."""
    with open(path, "rb") as f:
        d = f.read(1 << 20)
    ncmds = struct.unpack_from("<I", d, 16)[0]
    ident, libs, segs, sects = None, [], [], []
    off = 32
    for _ in range(ncmds):
        cmd, cs = struct.unpack_from("<II", d, off)
        if cmd in LOAD_CMDS or cmd == LC_ID_DYLIB:
            so = struct.unpack_from("<I", d, off + 8)[0]
            nm = d[off + so:off + cs].split(b"\0")[0].decode(errors="replace")
            if cmd == LC_ID_DYLIB:
                ident = nm
            else:
                libs.append((LOAD_CMDS[cmd], nm))
        off += cs
    return ident, libs


def macho_layout(mm):
    """Segments and sections of a mapped Mach-O: (segments, sections)."""
    ncmds = struct.unpack_from("<I", mm, 16)[0]
    off = 32
    segs, sects, chained = [], [], None
    for _ in range(ncmds):
        cmd, cs = struct.unpack_from("<II", mm, off)
        if cmd == LC_SEGMENT_64:
            name = mm[off + 8:off + 24].split(b"\0")[0].decode()
            vmaddr, vmsize, fileoff, filesize, _mp, _ip, nsects, _fl = struct.unpack_from(
                "<QQQQiiII", mm, off + 24)
            segs.append((name, vmaddr, vmsize, fileoff, filesize))
            so = off + 72
            for _s in range(nsects):
                sn = mm[so:so + 16].split(b"\0")[0].decode()
                sg = mm[so + 16:so + 32].split(b"\0")[0].decode()
                addr, size = struct.unpack_from("<QQ", mm, so + 32)
                sects.append((addr, addr + size, sg, sn))
                so += 80
        elif cmd == LC_DYLD_CHAINED_FIXUPS:
            chained = struct.unpack_from("<II", mm, off + 8)
        off += cs
    sects.sort()
    return segs, sects, chained


class Section:
    def __init__(self, sects):
        self.sects = sects
        self.starts = [s[0] for s in sects]

    def at(self, addr):
        i = bisect.bisect_right(self.starts, addr) - 1
        if i >= 0 and addr < self.sects[i][1]:
            return self.sects[i][2], self.sects[i][3]
        return None, None


def nm_defined(path):
    """name -> (segment, section) of every exported defined symbol."""
    out = {}
    for l in run("llvm-nm", "-m", "--defined-only", "-U", path).splitlines():
        m = re.match(r"^[0-9a-f]+ \((\S+),(\S+)\) (external|non-external) (\S.*)$", l)
        if m and m.group(3) == "external":
            out[m.group(4).split(" ")[0]] = (m.group(1), m.group(2))
    return out


def reexports_of(path):
    return [n for k, n in header(path)[1] if k == "reexport"]


def text_size_per_symbol(path, syms):
    n = sum(1 for v in syms.values() if v == ("__TEXT", "__text"))
    if not n:
        return None
    for l in run("llvm-objdump", "--macho", "--section-headers", path).splitlines():
        m = re.match(r"\s*\d+\s+__text\s+([0-9a-f]+)", l)
        if m:
            return int(m.group(1), 16) / n
    return None


def discover_stubs(overlay):
    roots = [os.path.join(overlay, "usr/lib"),
             os.path.join(overlay, "System/Library/Frameworks"),
             os.path.join(overlay, "System/Library/PrivateFrameworks")]
    images, stubs = [], {}
    for root in roots:
        for dp, dn, fn in os.walk(root):
            for f in fn:
                p = os.path.join(dp, f)
                if os.path.islink(p) or not os.path.isfile(p) or SKIP_NAME.search(f):
                    continue
                if os.path.getsize(p) > (256 << 10) or not is_macho(p):
                    continue
                images.append(p)
    for p in sorted(images):
        syms = nm_defined(p)
        avg = text_size_per_symbol(p, syms)
        # the generated wrappers are stubs by construction even when they carry real
        # (root class) methods that raise the average above the stub-shaped threshold
        generated = re.search(r"(Extras|suppl|cg-supl)\.dylib$", p)
        if generated or (avg is not None and avg <= 48):
            stubs[p] = syms
    return stubs


def all_macho(overlay):
    out = []
    for root in ("usr/lib", "System/Library/Frameworks", "System/Library/PrivateFrameworks"):
        for dp, dn, fn in os.walk(os.path.join(overlay, root)):
            for f in fn:
                p = os.path.join(dp, f)
                if os.path.islink(p) or not os.path.isfile(p) or SKIP_NAME.search(f):
                    continue
                if is_macho(p):
                    out.append(p)
    return out


# ---------------------------------------------------------------- consumer binds

def chained_binds(path):
    """yield (ordinal, symbol, kind) for every chained bind. kind: stub|got|data"""
    with open(path, "rb") as f:
        mm = mmap.mmap(f.fileno(), 0, access=mmap.ACCESS_READ)
    segs, sects, chained = macho_layout(mm)
    sec = Section(sects)
    dylibs = [n for k, n in header(path)[1]]
    # slots reached by a __stubs entry: `jmp *rel32(%rip)` (ff 25)
    stub_slots = set()
    for a, b, sg, sn in sects:
        if sn == "__stubs":
            for tseg in segs:
                if tseg[1] <= a < tseg[1] + tseg[2]:
                    base = tseg[3] + (a - tseg[1])
                    for o in range(0, b - a, 6):
                        if mm[base + o] == 0xFF and mm[base + o + 1] == 0x25:
                            rel = struct.unpack_from("<i", mm, base + o + 2)[0]
                            stub_slots.add(a + o + 6 + rel)
    dataoff, _size = chained
    (_v, starts_off, imports_off, symbols_off, imports_count, imports_fmt,
     _sf) = struct.unpack_from("<IIIIIII", mm, dataoff)
    imports = []
    for i in range(imports_count):
        if imports_fmt == 1:
            v = struct.unpack_from("<I", mm, dataoff + imports_off + 4 * i)[0]
            ordn, noff = v & 0xFF, v >> 9
            if ordn >= 0xF0:
                ordn -= 256
        elif imports_fmt == 2:
            v = struct.unpack_from("<I", mm, dataoff + imports_off + 8 * i)[0]
            ordn, noff = v & 0xFF, v >> 9
            if ordn >= 0xF0:
                ordn -= 256
        else:
            v = struct.unpack_from("<Q", mm, dataoff + imports_off + 8 * i)[0]
            ordn, noff = v & 0xFFFF, v >> 32
            if ordn >= 0xFFF0:
                ordn -= 65536
        e = mm.find(b"\0", dataoff + symbols_off + noff)
        imports.append((ordn, bytes(mm[dataoff + symbols_off + noff:e]).decode(errors="replace")))
    seg_count = struct.unpack_from("<I", mm, dataoff + starts_off)[0]
    base_vm = segs[0][1]
    res = {}  # (ordinal-name, symbol) -> set(kinds)
    for si in range(seg_count):
        so = struct.unpack_from("<I", mm, dataoff + starts_off + 4 + 4 * si)[0]
        if not so:
            continue
        sb = dataoff + starts_off + so
        _sz, page_size, pfmt, seg_off, _mx, page_count = struct.unpack_from("<IHHQIH", mm, sb)
        if pfmt not in (2, 6):
            raise SystemExit(f"{path}: chained pointer format {pfmt} not handled")
        for pi in range(page_count):
            ps = struct.unpack_from("<H", mm, sb + 22 + 2 * pi)[0]
            if ps == 0xFFFF:
                continue
            addr = base_vm + seg_off + pi * page_size + ps
            # file offset: the segment containing addr
            for sname, vm, vsz, fo, fsz in segs:
                if vm <= addr < vm + vsz:
                    foff = fo + (addr - vm)
                    seg_delta = fo - vm
                    break
            else:
                continue
            while True:
                v = struct.unpack_from("<Q", mm, foff)[0]
                if v >> 63:
                    idx = v & 0xFFFFFF
                    ordn, sym = imports[idx]
                    sg, sn = sec.at(addr)
                    if sn in NONLAZY_GOT:
                        kind = "stub" if addr in stub_slots else "got"
                    else:
                        kind = "data"
                    lib = dylibs[ordn - 1] if 1 <= ordn <= len(dylibs) else None
                    res.setdefault((lib, sym), set()).add((kind, sn))
                nxt = (v >> 51) & 0xFFF
                if not nxt:
                    break
                foff += nxt * 4
                addr += nxt * 4
    mm.close()
    return res


def opcode_binds(path):
    res = {}
    for flag, lazy in (("--bind", False), ("--lazy-bind", True)):
        out = run("llvm-objdump", "--macho", flag, path)
        for l in out.splitlines():
            f = l.split()
            # segment section address type addend dylib symbol
            if len(f) >= 7 and f[2].startswith("0x"):
                sn, lib, sym = f[1], f[5], f[6]
                kind = "stub" if lazy else ("got" if sn in NONLAZY_GOT else "data")
                res.setdefault((lib, sym), set()).add((kind, sn))
    return res


def consumer_binds(path):
    with open(path, "rb") as f:
        mm = mmap.mmap(f.fileno(), 0, access=mmap.ACCESS_READ)
    chained = macho_layout(mm)[2]
    mm.close()
    return chained_binds(path) if chained else opcode_binds(path)


# ---------------------------------------------------------------------- main

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--overlay", default=os.environ.get("DARLING_OVERLAY"))
    ap.add_argument("--consumer", action="append", default=[])
    ap.add_argument("--stub", action="append", default=[])
    ap.add_argument("--allow")
    ap.add_argument("--list-stubs", action="store_true")
    ap.add_argument("--emit-data", help="write every offending symbol name to FILE (generators read it)")
    ap.add_argument("--no-discover", action="store_true")
    a = ap.parse_args()
    if not a.overlay:
        sys.exit("check-data-as-code: --overlay (or DARLING_OVERLAY) required")
    ov = a.overlay.rstrip("/")
    allow = {}
    here = os.path.dirname(os.path.abspath(__file__))
    if not a.allow and os.path.exists(os.path.join(here, "data-as-code-allow.txt")):
        a.allow = os.path.join(here, "data-as-code-allow.txt")
    app = os.environ.get("CHROME_APP")
    if app and not a.consumer:
        import glob
        a.consumer += glob.glob(os.path.join(
            app, "Contents/Frameworks/*.framework/Versions/Current/*Framework"))
        a.consumer += glob.glob(os.path.join(app, "Contents/MacOS/*"))
        src = os.environ.get("DARLING_SRC_DIR")
        if src and os.path.exists(os.path.join(src, "tests/chrome-macho")):
            a.consumer.append(os.path.join(src, "tests/chrome-macho"))
        a.consumer = [p for p in a.consumer if is_macho(p)]
    if a.allow:
        for l in open(a.allow):
            f = l.split(None, 1)
            if f and not f[0].startswith("#"):
                allow[f[0]] = f[1].strip() if len(f) > 1 else ""

    stubs = {} if a.no_discover else discover_stubs(ov)
    loadpath = {}   # stub file -> the path consumers load it by (dyld uses the path, not the id)
    for spec in a.stub:
        p, _, lp = spec.partition("=")
        stubs[p] = nm_defined(p)
        if lp:
            loadpath[p] = lp
    if a.list_stubs:
        for p in sorted(stubs):
            print(p)
        return 0

    # names by which a consumer's load command refers to each stub
    names = {}
    for p in stubs:
        rel = loadpath.get(p) or "/" + os.path.relpath(p, ov)
        names.setdefault(rel, set()).add(p)
        names.setdefault("@base:" + os.path.basename(rel).split(".")[0], set()).add(p)

    # consumers: explicit plus every overlay image that loads a stub
    consumers = list(a.consumer)
    for p in all_macho(ov):
        if p in stubs or p in consumers:
            continue
        if any(n in names for k, n in header(p)[1]):
            consumers.append(p)
    # provider cache
    prov_cache = {}

    def provider_syms(path):
        if path not in prov_cache:
            prov_cache[path] = nm_defined(path) if os.path.exists(path) else {}
        return prov_cache[path]

    libsys = os.path.join(ov, "usr/lib/libSystem.B.dylib")
    closure = [ov + n for n in reexports_of(libsys)] if os.path.exists(libsys) else []

    def providers_for(stub):
        out = list(closure)
        for k, n in header(stub)[1]:
            cand = ov + n
            if os.path.exists(cand) and cand not in stubs:
                out.append(cand)
        return out

    # consumer evidence: (stub, symbol) -> {consumer: kinds}
    evidence = {}
    for c in consumers:
        try:
            binds = consumer_binds(c)
        except SystemExit:
            raise
        for (lib, sym), kinds in binds.items():
            if lib is None:
                continue
            tgt = names.get(lib)
            if tgt is None:
                tgt = names.get("@base:" + os.path.basename(lib).split(".")[0]) if "/" not in lib else None
            for s in tgt or ():
                evidence.setdefault((s, sym), {})[c] = {k for k, _ in kinds}

    offenders = []
    for stub, syms in sorted(stubs.items()):
        provs = providers_for(stub)
        for sym, (sg, sn) in sorted(syms.items()):
            if (sg, sn) != ("__TEXT", "__text"):
                continue
            pdefs = [(p, provider_syms(p).get(sym)) for p in provs]
            pdefs = [(p, v) for p, v in pdefs if v]
            if any(v == ("__TEXT", "__text") for p, v in pdefs):
                continue  # a real function with that name
            why = []
            if OBJC.match(sym):
                why.append("a: ObjC class/ivar/ehtype symbol")
            if BLOCKS.match(sym) or DATA_NAMES.match(sym):
                if not OBJC.match(sym):
                    why.append("b: known data name")
            ev = evidence.get((stub, sym))
            if ev and sym not in allow:
                kinds = set().union(*ev.values())
                if "stub" not in kinds and kinds & {"got", "data"}:
                    why.append("c: bound only non-lazily (%s) by %s" % (
                        "/".join(sorted(kinds)), ", ".join(sorted(os.path.basename(x) for x in ev))))
            for p, v in pdefs:
                if v[0].startswith("__DATA") or v[1] in ("__data", "__const", "__common", "__bss"):
                    why.append("d: provider %s has it in %s,%s" % (os.path.basename(p), v[0], v[1]))
                    break
            if why:
                offenders.append((stub, sym, why))

    print("check-data-as-code: %d stub images, %d consumer images, overlay %s" % (
        len(stubs), len(consumers), "$DARLING_OVERLAY"))
    cur = None
    for stub, sym, why in offenders:
        if stub != cur:
            cur = stub
            print("  " + os.path.relpath(stub, ov))
        print("    OFFENDER %s  [%s]" % (sym, "; ".join(why)))
    if a.emit_data:
        with open(a.emit_data, "w") as f:
            f.write("".join(sym + "\n" for sym in sorted({s for _, s, _ in offenders})))
    nstub = len({s for s, _, _ in offenders})
    print("check-data-as-code: %s (%d offenders in %d stub images)" % (
        "FAIL" if offenders else "OK", len(offenders), nstub))
    return 1 if offenders else 0


if __name__ == "__main__":
    sys.exit(main())
