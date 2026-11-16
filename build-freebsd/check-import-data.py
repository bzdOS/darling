#!/usr/bin/env python3
"""check-import-data.py -- every DATA symbol a Chrome image imports must resolve in the overlay,
and every pointer constant (CFStringRef / NSString *) must hold a non-NULL pointer.

For each Mach-O given (default: the Chrome framework, its helpers and libraries under CHROME_APP)
the bind table (llvm-objdump --macho --dyld-info) names a symbol and the dylib that must
define it. The symbol is looked up in that image and its re-export closure in the overlay. Only
names stub_data_rules.is_known_data() or the pointer-constant rule call data are examined; an
unresolved one, or a pointer constant whose 8 bytes are zero in the image, is an offender.
One-line verdict:  IMPORT-DATA-VERDICT pass|FAIL imports=N data=N null=[..] unresolved=[..]
Exit status 0 on pass.
A second verdict covers the overlay images themselves (OVERLAY-IMAGES-VERDICT): no image may bind a
symbol by flat namespace that nothing in the overlay exports, and an image the stub generators rebuilt
(it has a <image>.pre-dataascode backup) must bind ___CFConstantStringClassReference two-level, never
flat; every CFString constant in an image (bind of the isa) must have flags 0x7c8, a rebased pointer to a
valid NUL terminated ASCII C string whose length matches, and every exported string constant that is
not zero must point at such a CFString whose text is the symbol name. Images that predate the
generators and still bind the isa flat are only counted (legacy=N).
Usage: check-import-data.py --overlay DIR [--app CHROME_APP] [--selftest] [IMAGE...]
"""
import argparse, os, re, struct, subprocess, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import stub_data_rules as R


def run(*a):
    return subprocess.run(a, capture_output=True, text=True).stdout


class Img:
    def __init__(self, path):
        self.path = path
        self.exp = {}
        for l in run("llvm-nm", "-m", "--defined-only", "-U", path).splitlines():
            m = re.match(r"^([0-9a-f]+) \((\S+),(\S+)\) external (\S+)", l)
            if m:
                self.exp[m.group(4)] = (int(m.group(1), 16), m.group(2))
        self.reexp = []
        for l in run("llvm-objdump", "--macho", "--dylibs-used", path).splitlines():
            l = l.strip()
            if l.startswith("/") and l.endswith("reexport)"):
                self.reexp.append(l.split(" (")[0])
        self.segs = []
        try:
            d = open(path, "rb").read(1 << 16)
            if struct.unpack_from("<I", d, 0)[0] == 0xfeedfacf:
                ncmds = struct.unpack_from("<I", d, 16)[0]
                off = 32
                for _ in range(ncmds):
                    cmd, sz = struct.unpack_from("<II", d, off)
                    if cmd == 0x19:
                        vm, vs, fo, fs = struct.unpack_from("<QQQQ", d, off + 24)
                        self.segs.append((vm, vs, fo))
                    off += sz
        except OSError:
            pass

    def readb(self, addr, n):
        for vm, vs, fo in self.segs:
            if vm <= addr < vm + vs:
                with open(self.path, "rb") as f:
                    f.seek(fo + addr - vm)
                    return f.read(n)
        return b""

    def read8(self, addr):
        for vm, vs, fo in self.segs:
            if vm <= addr < vm + vs:
                with open(self.path, "rb") as f:
                    f.seek(fo + addr - vm)
                    b = f.read(8)
                return struct.unpack("<Q", b)[0] if len(b) == 8 else None
        return None


CFISA = "___CFConstantStringClassReference"
# images whose string constants are hand written (real values, text differs from the symbol name)
HAND_WRITTEN = {"SystemConfigurationExtras.dylib"}


def macho_files(root):
    out = []
    for r, ds, fs in os.walk(root):
        ds[:] = [d for d in ds if d not in ("proc", "dev")]
        for fn in fs:
            if re.search(r"\.(bak|pre-[\w-]+)$", fn):
                continue
            p = os.path.join(r, fn)
            if os.path.islink(p) or "/System/Library/" not in p and "/usr/lib/" not in p:
                continue
            try:
                if os.path.getsize(p) < 4096:
                    continue
                with open(p, "rb") as f:
                    if f.read(4) == b"\xcf\xfa\xed\xfe":
                        out.append(p)
            except OSError:
                pass
    return sorted(out)


def binds_of(path):
    """[(address, dylib, symbol)] from the bind table"""
    out = []
    for l in run("llvm-objdump", "--macho", "--bind", path).splitlines():
        m = re.match(r"^__\w+\s+\S+\s+0x([0-9a-fA-F]+)\s+\w+\s+-?\d+\s+(\S+)\s+(\S+)$", l)
        if m:
            out.append((int(m.group(1), 16), m.group(2), m.group(3)))
    return out


def check_overlay_images(ov):
    """OVERLAY-IMAGES-VERDICT: flat-namespace binds and CFString constants of the overlay images"""
    files = macho_files(ov)
    exports_all = None
    flat_bad, cfstr_bad, legacy, nflat, ncf = [], [], 0, 0, 0
    for p in files:
        rel = os.path.relpath(p, ov)
        gen = os.path.exists(p + ".pre-dataascode")
        bl = binds_of(p)
        flat = sorted({sy for _, dy, sy in bl if dy == "flat-namespace"})
        nflat += len(flat)
        cf_flat = CFISA in flat
        if cf_flat:
            if gen or rel.endswith("darling-extras.dylib"):
                flat_bad.append("%s:%s" % (rel, CFISA))
            else:
                legacy += 1
        rest = [x for x in flat if x != CFISA]
        if rest:
            if exports_all is None:
                exports_all = set()
                for q in files:
                    for l in run("llvm-nm", "-m", "--defined-only", "-U", q).splitlines():
                        m = re.match(r"^[0-9a-f]+ \((\S+),(\S+)\) external (\S+)", l)
                        if m:
                            exports_all.add(m.group(3))
            miss = [x for x in rest if x not in exports_all and not x.startswith("_dyld")
                    and not x.startswith("dyld")]
            if gen and miss:
                flat_bad.append("%s:%s" % (rel, ",".join(miss[:6])))
        isas = [(a, dy) for a, dy, sy in bl if sy == CFISA]
        # the isa must be expected in the image that defines it, not in a framework that merely lists it
        wrong = sorted({dy for _, dy in isas if dy not in ("flat-namespace", "CoreFoundation")})
        if wrong and (gen or "Extras" in rel or "suppl" in rel):
            flat_bad.append("%s:%s-bound-to-%s" % (rel, CFISA, ",".join(wrong)))
        if not isas:
            continue
        img = Img(p)
        starts = set()
        for addr, dy in isas:
            ncf += 1
            starts.add(addr)
            hdr = img.readb(addr + 8, 24)
            if len(hdr) < 24:
                cfstr_bad.append("%s@%x:short" % (rel, addr)); continue
            flags, _, cptr, ln = struct.unpack("<IIQQ", hdr)
            if flags == 0x7d0:
                continue   # UTF-16 constant (len is in characters): not a C string
            cs = img.readb(cptr, ln + 1) if cptr else b""
            if flags != 0x7c8 or len(cs) != ln + 1 or cs[-1] != 0 or \
               b"\0" in cs[:-1]:
                cfstr_bad.append("%s@%x:bad(flags=%x len=%d)" % (rel, addr, flags, ln))
        if gen:
            for name, (addr, seg) in img.exp.items():
                if R.is_string_const(name) and seg.startswith("__DATA"):
                    v = img.read8(addr)
                    if not v:
                        continue
                    if v not in starts:
                        cfstr_bad.append("%s:%s:not-a-CFString" % (rel, name)); continue
                    hdr = img.readb(v + 8, 24)
                    _, _, cptr, ln = struct.unpack("<IIQQ", hdr)
                    if os.path.basename(p) not in HAND_WRITTEN and img.readb(cptr, ln) != name[1:].encode():
                        cfstr_bad.append("%s:%s:text-mismatch" % (rel, name))
    bad = flat_bad or cfstr_bad
    print("OVERLAY-IMAGES-VERDICT %s images=%d flat_syms=%d cfstrings=%d legacy_flat_isa=%d flat_bad=%d [%s] cfstring_bad=%d [%s]" % (
        "FAIL" if bad else "pass", len(files), nflat, ncf, legacy, len(flat_bad), " ".join(flat_bad),
        len(cfstr_bad), " ".join(cfstr_bad)))
    return not bad


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--overlay", required=True)
    ap.add_argument("--app", default=os.environ.get("CHROME_APP"))
    ap.add_argument("--list", action="store_true", help="print one offender per line")
    ap.add_argument("images", nargs="*")
    a = ap.parse_args()
    ov = a.overlay.rstrip("/")
    imgs = list(a.images)
    if not imgs and a.app:
        for root, _, fs in os.walk(os.path.join(a.app, "Contents")):
            for fn in fs:
                p = os.path.join(root, fn)
                if "crashpad_handler" in fn:
                    continue   # not started by the series; binds kIOMainPortDefault from IOKit (the shim lacks it)
                if os.path.islink(p) or os.path.getsize(p) < 20000 or "/Resources/" in p:
                    continue
                with open(p, "rb") as f:
                    if f.read(4) == b"\xcf\xfa\xed\xfe":
                        imgs.append(p)
    # index of overlay images by file basename
    index = {}
    for root, _, fs in os.walk(ov):
        if "/proc" in root or "/dev" in root:
            continue
        for fn in fs:
            base = re.sub(r"(\.B)?\.dylib$", "", fn)
            index.setdefault(base, []).append(os.path.join(root, fn))
    cache = {}

    def image(p):
        if p not in cache:
            cache[p] = Img(p)
        return cache[p]

    def lookup(img, name, depth=0):
        if name in img.exp:
            return img, img.exp[name]
        if depth < 6:
            for r in img.reexp:
                rp = ov + r
                if os.path.exists(rp) and rp != img.path:
                    got = lookup(image(rp), name, depth + 1)
                    if got:
                        return got
        return None

    def candidates(dy):
        c = [p for p in index.get(dy, []) if "/Versions/A/" in p or p.endswith(".dylib")]
        return [p for p in c if not re.search(r"\.(bak|pre-\w+)$", p)]

    # constants a hand written behaviour sets from a constructor (stub-frameworks/behaviors/*/*.c:
    # `CFStringRef kX;`) are zero in the file and a real CFString after load
    ctor = set()
    bdir = os.path.join(os.path.dirname(os.path.abspath(__file__)), "stub-frameworks", "behaviors")
    for root, _, fs in os.walk(bdir):
        for fn in fs:
            if fn.endswith(".c"):
                ctor |= {"_" + n for n in re.findall(r"^CFStringRef\s+(\w+);", open(os.path.join(root, fn)).read(), re.M)}
    seen = {}
    total = 0
    for im in imgs:
        for l in run("llvm-objdump", "--macho", "--dyld-info", im).splitlines():
            m = re.search(r"\sbind\s+0x[0-9a-fA-F]+\s+(.*?)\s*(_\S+)$", l)
            if not m:
                continue
            total += 1
            seen[(m.group(1).strip(), m.group(2))] = 1
    nulls, unres, ndata = [], [], 0
    for (dy, name) in sorted(seen):
        if not (R.is_known_data(name) or R.is_string_const(name)) or R.is_objc_symbol(name):
            continue
        ndata += 1
        got = None
        for p in candidates(dy) if dy and not dy.startswith("(") else []:
            got = lookup(image(p), name)
            if got:
                break
        if not got:
            unres.append("%s@%s" % (name, dy))
            continue
        owner, (addr, seg) = got
        if R.is_string_const(name) and seg.startswith("__DATA"):
            v = owner.read8(addr)
            if v == 0 and name not in ctor:
                nulls.append("%s@%s" % (name, dy))
    bad = nulls or unres
    ok_images = check_overlay_images(ov)
    print("IMPORT-DATA-VERDICT %s binds=%d data=%d null=%d [%s] unresolved=%d [%s]" % (
        "FAIL" if bad else "pass", total, ndata, len(nulls), " ".join(nulls),
        len(unres), " ".join(unres)))
    sys.exit(1 if bad or not ok_images else 0)


if __name__ == "__main__":
    main()
