#!/usr/bin/env python3
"""data-shadow-census.py -- data symbols (constants) that an injected image exports zero filled
on top of a real definition in another overlay image, and the value check behind CATX-KEYS.

The injected image (darling-extras.dylib) is first in the flat lookup order, so a dlsym(RTLD_DEFAULT)
or a flat bind finds its zero object before the framework's real one.

Usage:
  data-shadow-census.py census [--overlay DIR] [--image usr/lib/darling-extras.dylib]
      one line per symbol: <symbol> <image holding the real definition> [<more images>]
      only data symbols whose object in the injected image is zero (no bytes, no rebase, no bind)
      and whose provider holds a non-zero object (bytes, rebase or bind).
  data-shadow-census.py value IMAGE SYMBOL
      prints "non-null" or "null" for the first pointer of the exported object (a rebase or a bind
      at that address counts as non-null), "absent" if IMAGE does not export SYMBOL.
  data-shadow-census.py chrome-imports [--overlay DIR] [--chrome-root DIR] [--sdk DIR] [--list]
      the zero exported data objects that the Chrome framework, its helpers and its libraries import:
      every chained-fixups import (and opcode bind) is resolved the way dyld does: two-level through the
      load command ordinal, following re-exports to the image that defines the symbol; a flat lookup
      goes through the images in overlay order. A line per symbol the provider holds as a zero object:
      <symbol> <provider image> <string|number|struct|other> <importing images>
      last line: NULL-CONST-COUNT strings=<n> other=<n>. Exit 0 always (the gate reads the count).
Needs llvm-nm and llvm-objdump. Reads images only through those tools and seek-sized reads.
"""
import argparse
import os
import re
import struct
import subprocess
import sys

DEFAULT_OV = os.environ.get("DARLING_OVERLAY", "/opt/darling/overlay")
TREES = ("System/Library/Frameworks", "System/Library/PrivateFrameworks", "usr/lib")
MAX_SIZE = 200 * 1024 * 1024


def run(*a):
    return subprocess.run(a, capture_output=True, text=True).stdout


def is_macho64(path):
    try:
        with open(path, "rb") as f:
            m = f.read(4)
        return m == b"\xcf\xfa\xed\xfe"
    except OSError:
        return False


def data_exports(path):
    """{name: vmaddr} of the externally defined symbols living in a __DATA* segment"""
    out = {}
    for l in run("llvm-nm", "-m", "--defined-only", "-U", path).splitlines():
        m = re.match(r"^([0-9a-f]+) \((__DATA\w*),(\S+)\) external (\S+)", l)
        if m:
            out[m.group(4)] = int(m.group(1), 16)
    return out


def sections(path):
    """[(vmaddr, size, fileoff)] of the 64 bit Mach-O sections"""
    with open(path, "rb") as f:
        hdr = f.read(32)
        ncmds = struct.unpack_from("<I", hdr, 16)[0]
        cmds = f.read(struct.unpack_from("<I", hdr, 20)[0])
    out, off = [], 0
    for _ in range(ncmds):
        cmd, size = struct.unpack_from("<II", cmds, off)
        if cmd == 0x19:
            nsects = struct.unpack_from("<I", cmds, off + 64)[0]
            for i in range(nsects):
                s = off + 72 + i * 80
                addr, sz, foff = struct.unpack_from("<QQI", cmds, s + 32)
                out.append((addr, sz, foff))
        off += size
    return out


def fixup_addrs(path):
    """addresses covered by a rebase or a bind (opcode based images)"""
    addrs = set()
    for flag in ("--rebase", "--bind", "--weak-bind"):
        for l in run("llvm-objdump", "--macho", flag, path).splitlines():
            m = re.search(r"\b0x([0-9A-Fa-f]{6,})\b", l)
            if m:
                addrs.add(int(m.group(1), 16))
    return addrs


def is_null(path, addr, secs, fixups):
    if addr in fixups:
        return False
    for a, sz, foff in secs:
        if a <= addr < a + sz:
            with open(path, "rb") as f:
                f.seek(foff + addr - a)
                return f.read(8).strip(b"\0") == b""
    return True


def _magic(path):
    try:
        with open(path, "rb") as f:
            return f.read(4)
    except OSError:
        return b""


def images(ov, fat=False):
    for t in TREES:
        for d, _, fs in os.walk(os.path.join(ov, t)):
            if d[len(ov):].count("/") > 9:
                continue
            for fn in fs:
                p = os.path.join(d, fn)
                if os.path.islink(p) or ".pre-" in fn or ".bak" in fn or fn.endswith((".objc", ".py", ".sh", ".a")):
                    continue
                try:
                    if os.path.getsize(p) > MAX_SIZE:
                        continue
                except OSError:
                    continue
                if is_macho64(p) or (fat and _magic(p) == b"\xca\xfe\xba\xbe"):
                    yield p


# ---- chrome-imports: zero constants that Chrome (framework, helpers, libraries) binds to ----
LC_DYLIBS = {0xC: "dylib", 0x80000018: "weak", 0x8000001F: "reexport", 0x80000023: "upward"}
LC_SEGMENT_64 = 0x19
LC_DYLD_CHAINED_FIXUPS = 0x80000034
_nm_cache = {}


def cached_exports(path):
    if path not in _nm_cache:
        _nm_cache[path] = data_exports(path)
    return _nm_cache[path]


_all_cache = {}


def all_exports(path):
    """every externally defined symbol {name: (segment, section)} (code stubs included)"""
    if path not in _all_cache:
        out = {}
        for l in run("llvm-nm", "-m", "--defined-only", "-U", path).splitlines():
            m = re.match(r"^([0-9a-f]+) \((\w+),(\S+)\) external (\S+)", l)
            if m:
                out[m.group(4)] = (m.group(2), int(m.group(1), 16))
        _all_cache[path] = out
    return _all_cache[path]


def macho_deps(path):
    """[(kind, install name)] in load command order (ordinal = index + 1)"""
    with open(path, "rb") as f:
        hdr = f.read(32)
        ncmds = struct.unpack_from("<I", hdr, 16)[0]
        cmds = f.read(struct.unpack_from("<I", hdr, 20)[0])
    out, off = [], 0
    for _ in range(ncmds):
        cmd, size = struct.unpack_from("<II", cmds, off)
        if cmd in LC_DYLIBS:
            so = struct.unpack_from("<I", cmds, off + 8)[0]
            out.append((LC_DYLIBS[cmd], cmds[off + so:off + size].split(b"\0")[0].decode(errors="replace")))
        off += size
    return out


def macho_imports(path):
    """[(ordinal, symbol)] of the chained-fixups imports; ordinal is signed for the special ones"""
    with open(path, "rb") as f:
        d = f.read()
    ncmds = struct.unpack_from("<I", d, 16)[0]
    off = 32
    for _ in range(ncmds):
        cmd, size = struct.unpack_from("<II", d, off)
        if cmd == LC_DYLD_CHAINED_FIXUPS:
            h = struct.unpack_from("<I", d, off + 8)[0]
            _v, _so, io, so, count, fmt, _sf = struct.unpack_from("<IIIIIII", d, h)
            out = []
            for i in range(count):
                if fmt == 1:
                    v = struct.unpack_from("<I", d, h + io + i * 4)[0]
                    lo, no = v & 0xFF, v >> 9
                    lo = lo - 256 if lo > 0xF0 else lo
                elif fmt == 2:
                    v = struct.unpack_from("<Q", d, h + io + i * 8)[0]
                    lo, no = v & 0xFFFF, (v >> 32) & 0xFFFFFFFF
                    lo = lo - 65536 if lo > 0xFFF0 else lo
                else:
                    v = struct.unpack_from("<Q", d, h + io + i * 8)[0]
                    lo, no = v & 0xFFFF, (v >> 32) & 0xFFFFFFFF
                    lo = lo - 65536 if lo > 0xFFF0 else lo
                s = h + so + no
                out.append((lo, d[s:d.find(b"\0", s)].decode(errors="replace")))
            return out
        off += size
    return []


def opcode_imports(path):
    """[(ordinal, symbol)] from a classic bind table (images without chained fixups)"""
    names = macho_deps(path)
    out = []
    for flag in ("--bind", "--lazy-bind"):
        for l in run("llvm-objdump", "--macho", flag, path).splitlines():
            m = re.match(r"^\S+\s+\S+\s+0x[0-9A-Fa-f]+\s+\S+\s+(?:flat-namespace|main-executable|(\S+))\s+(\S+)", l)
            if not m:
                continue
            lib = m.group(1)
            ordv = -1
            if lib:
                for i, (k, n) in enumerate(names, 1):
                    if os.path.basename(n) == lib or n.rsplit("/", 1)[-1] == lib:
                        ordv = i
                        break
            out.append((ordv, m.group(2)))
    return out


_thin = {}
_thin_dir = []


def thin(path):
    """path of a thin x86_64 copy for a fat image (offsets in the helpers above need a thin file)"""
    try:
        with open(path, "rb") as f:
            m = f.read(8)
            if m[:4] != b"\xca\xfe\xba\xbe":
                return path
            n = struct.unpack(">I", m[4:])[0]
            for i in range(min(n, 8)):
                f.seek(8 + 20 * i)
                ct, _sub, off, size, _al = struct.unpack(">5I", f.read(20))
                if ct == 0x01000007:
                    if not _thin_dir:
                        import tempfile
                        _thin_dir.append(tempfile.mkdtemp(prefix="census-thin-"))
                    out = os.path.join(_thin_dir[0], "%d" % len(_thin))
                    f.seek(off)
                    with open(out, "wb") as g:
                        g.write(f.read(size))
                    _thin[out] = path
                    return out
    except OSError:
        pass
    return path


_thin_by_path = {}


def thin_cached(path):
    if path not in _thin_by_path:
        _thin_by_path[path] = thin(path)
    return _thin_by_path[path]


def resolve_install(ov, name, importer):
    cands = []
    if name.startswith("/"):
        cands.append(ov + name)
    elif name.startswith(("@loader_path/", "@executable_path/")):
        cands.append(os.path.join(os.path.dirname(importer), name.split("/", 1)[1]))
    for c in cands:
        if os.path.exists(c):
            return thin_cached(os.path.realpath(c))
    return None


def find_definer(ov, img, sym, seen=None, depth=0):
    """the image that defines `sym`, looking in `img` and then in the images it re-exports"""
    seen = seen if seen is not None else set()
    if img in seen or depth > 12:
        return None
    seen.add(img)
    if sym in all_exports(img):
        return img
    for kind, n in macho_deps(img):
        if kind == "reexport":
            p = resolve_install(ov, n, img)
            if p:
                r = find_definer(ov, p, sym, seen, depth + 1)
                if r:
                    return r
    return None


# zero in the real image too (filled in at run time or a plain int 0): not a missing constant
RUNTIME_ZERO = {"___stack_chk_guard", "_daylight", "_optarg", "_optind", "_opterr", "_optopt", "_optreset",
                "_timezone", "_signgam", "_kIOMainPortDefault"}


def behavior_set_names():
    """constants a behaviour source (stub-frameworks/behaviors/*/*.c with a constructor) fills in at image load:
    zero in the file, a real value at run time"""
    out = set()
    bdir = os.path.join(os.path.dirname(os.path.abspath(__file__)), "stub-frameworks", "behaviors")
    for d, _, fs in os.walk(bdir):
        for fn in fs:
            if not fn.endswith(".c"):
                continue
            try:
                txt = open(os.path.join(d, fn), errors="replace").read()
            except OSError:
                continue
            if "constructor" in txt:
                out |= {"_" + m for m in re.findall(r"^(?:CFStringRef|NSString \*|id|void \*)\s*\*?(\w+);", txt, re.M)}
    return out


def object_is_zero(path, sym):
    """True when the object (up to the next symbol, 64 bytes at most) holds no byte, rebase or bind"""
    seg, addr = all_exports(path)[sym]
    if not seg.startswith("__DATA"):
        return None   # code or read-only: not a zero data object
    if path not in _sec_cache:
        _sec_cache[path] = (sections(path), fixup_addrs(path), sorted(a for _, a in all_exports(path).values()))
    secs, fx, addrs = _sec_cache[path]
    import bisect
    i = bisect.bisect_right(addrs, addr)
    ext = 64
    if i < len(addrs):
        ext = max(8, min(64, addrs[i] - addr))
    if any(addr <= f < addr + ext for f in fx):
        return False
    for a, sz, foff in secs:
        if a <= addr < a + sz:
            with open(path, "rb") as f:
                f.seek(foff + addr - a)
                return not f.read(min(ext, a + sz - addr)).strip(b"\0")
    return True


_sec_cache = {}


def chrome_images(root):
    out = []
    for d, _, fs in os.walk(root):
        if d[len(root):].count("/") > 9:
            continue
        for fn in sorted(fs):
            p = os.path.join(d, fn)
            if os.path.islink(p) or not is_macho64(p):
                continue
            out.append(p)
    return sorted(out)


def overlay_order(ov):
    first = os.path.join(ov, "usr/lib/darling-extras.dylib")
    return [thin_cached(p) for p in ([first] if os.path.exists(first) else []) + [p for p in images(ov, True) if p != first]]


NUMBER_NAMES = re.compile(r"^_(kCLLocationAccuracy|GCHapticDurationInfinite|NSURLSessionTaskPriority|AVSpeechUtterance[A-Za-z]*Rate|kIOMainPortDefault)")
STRUCT_NAMES = re.compile(r"CallBacks$|^_kCMTime|^_CATransform3D|^___NS\w+0__struct$|^___kCFBoolean|^__os_log_default$")


def sdk_type(sym, sdk=None):
    """string | number | struct | other. The SDK headers of the framework are consulted first when they
    declare the symbol (NSString * / CFStringRef ...); the rest is decided by name, from the declarations
    of the public headers: string constants are NSString * / CFStringRef, numbers CGFloat / double / int,
    structs CMTime / CATransform3D / callbacks; other is a non-string object (UTType *, blocks, os_log_t)."""
    nm = sym[1:]
    if sdk:
        out = subprocess.run(["grep", "-rhwE", "--include=*.h", r"%s *(;|[A-Z_]+ *;|__OSX|API_|NS_|CF_|CA_|CV_)" % re.escape(nm), sdk],
                             capture_output=True, text=True).stdout
        for l in out.splitlines():
            pre = l.split(nm)[0]
            if "(" in pre:
                continue
            if re.search(r"NSString|CFStringRef|CFString\b|NSNotificationName", pre):
                return "string"
            if re.search(r"\b(CGFloat|double|float|int|NSInteger|NSUInteger|CFTimeInterval|NSTimeInterval)\b", pre) and "*" not in pre:
                return "number"
    if NUMBER_NAMES.match(sym):
        return "number"
    if STRUCT_NAMES.search(sym):
        return "struct"
    return "string" if is_string_const(sym) else "other"


def chrome_imports(ov, root, sdk):
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    global is_string_const
    from stub_data_rules import is_string_const
    order = overlay_order(ov)
    runtime_set = behavior_set_names()
    found = {}   # symbol -> (provider, set of importers)
    for img in chrome_images(root):
        deps = macho_deps(img)
        imps = macho_imports(img) or opcode_imports(img)
        rel = os.path.basename(img)
        for lo, sym in set(imps):
            prov = None
            if lo >= 1 and lo <= len(deps):
                p = resolve_install(ov, deps[lo - 1][1], img)
                if p:
                    prov = find_definer(ov, p, sym)
            elif lo in (-1, -3):
                for p in order:
                    if sym in all_exports(p):
                        prov = p
                        break
            if not prov:
                continue
            try:
                z = object_is_zero(prov, sym)
            except KeyError:
                continue
            if z and sym not in RUNTIME_ZERO and sym not in runtime_set:
                found.setdefault(sym, (prov, set()))[1].add(rel)
    rows = []
    for sym, (prov, who) in sorted(found.items()):
        rows.append((sym, os.path.relpath(_thin.get(prov, prov), ov), sdk_type(sym, sdk), sorted(who)))
    return rows



def main():
    ap = argparse.ArgumentParser()
    sub = ap.add_subparsers(dest="cmd", required=True)
    c = sub.add_parser("census")
    c.add_argument("--overlay", default=DEFAULT_OV)
    c.add_argument("--image", default="usr/lib/darling-extras.dylib")
    v = sub.add_parser("value")
    v.add_argument("image")
    v.add_argument("symbol")
    ci = sub.add_parser("chrome-imports")
    ci.add_argument("--overlay", default=DEFAULT_OV)
    ci.add_argument("--chrome-root", default=None)
    ci.add_argument("--sdk", default=os.environ.get("DARLING_SDK", ""))
    a = ap.parse_args()
    if a.cmd == "chrome-imports":
        ov = a.overlay.rstrip("/")
        root = a.chrome_root or os.path.join(ov, "Frameworks")
        sdk = a.sdk if a.sdk and os.path.isdir(a.sdk) else None
        rows = chrome_imports(ov, root, sdk)
        for sym, prov, typ, who in rows:
            print("%s %s %s %s" % (sym, prov, typ, ",".join(who)))
        from stub_data_rules import string_source
        srcs = [string_source(r[0]) for r in rows if r[2] == "string"]
        ns = len(srcs)
        print("NULL-CONST-COUNT strings=%d other=%d from_memory=%d name_fallback=%d cocotron=%d" % (
            ns, len(rows) - ns, srcs.count("sdk-doc-from-memory"), srcs.count("symbol-name"),
            srcs.count("cocotron-code")))
        return 0
    if a.cmd == "value":
        ex = data_exports(a.image)
        if a.symbol not in ex:
            print("absent")
            return 2
        print("null" if is_null(a.image, ex[a.symbol], sections(a.image), fixup_addrs(a.image)) else "non-null")
        return 0
    ov = a.overlay.rstrip("/")
    inj = os.path.join(ov, a.image)
    iex = data_exports(inj)
    secs, fx = sections(inj), fixup_addrs(inj)
    zero = {n for n, ad in iex.items() if is_null(inj, ad, secs, fx)}
    real = {}
    for p in images(ov):
        if os.path.realpath(p) == os.path.realpath(inj):
            continue
        pex = data_exports(p)
        hit = [n for n in pex if n in zero]
        if not hit:
            continue
        # a provider counts when its own object is not zero (a zero stub is no real definition)
        psecs, pfx = sections(p), fixup_addrs(p)
        for n in hit:
            if not is_null(p, pex[n], psecs, pfx):
                real.setdefault(n, []).append(os.path.relpath(p, ov))
    for n in sorted(real):
        print("%s %s" % (n, " ".join(real[n])))
    print("# %d zero data exports in %s, %d with a real definition elsewhere" % (len(zero), a.image, len(real)),
          file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())
