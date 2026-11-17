#!/usr/bin/env python3
"""rebuild-stub-data.py -- re-emit stub dylibs so that data is exported as data.

A stub image whose exports are all `__TEXT,__text` one-instruction functions answers
a data symbol (an ObjC class, a CFString constant, a block isa) with code. This tool
rebuilds such an image from its own export list and load commands:
  * names that stub_data_rules.is_known_data() says are data, and names in the
    --data-list file (check-data-as-code.py --emit-data: consumer evidence), become
    zero-filled exported objects (DATA_SIZE bytes);
  * _OBJC_CLASS_$_X / _OBJC_METACLASS_$_X become real root classes (compiled from
    stub_data_rules.classes_m), the way the OpenDirectory stand-in does it;
  * names a provider on disk (a library the image re-exports or loads, plus the
    libSystem re-export closure) defines as DATA are not emitted at all: the
    provider is reached through a re-export, nothing is shadowed
    (_NSConcreteStackBlock and friends come from libsystem_blocks);
  * an INJECTED image (darling-extras.dylib: DYLD_INSERT_LIBRARIES puts it first in the flat lookup order, so a
    dlsym(RTLD_DEFAULT) or flat bind finds it before the framework) exports no name that a hand written behaviour
    (stub-frameworks/behaviors/<image>/*.c) of a SHADOW_FROM image defines: the single implementation lives in the behaviour;
  * everything else stays a function (`xorl %eax,%eax; ret`).
Install name, versions, re-exports and dependencies are kept. An image with nothing
to fix is left alone. With --install the old file is copied to
<backup-dir>/<image file name>/pre-dataascode (one per image, never overwritten, nothing is written
into the overlay besides the image itself; backup-dir defaults to ../backup next to the overlay) and
replaced by the rebuilt one.

Usage: rebuild-stub-data.py --overlay DIR --work DIR --data-list FILE [--install] IMAGE...
"""
import argparse
import hashlib
import os
import re
import shutil
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import stub_data_rules as R  # noqa: E402


def run(*a, check=False):
    r = subprocess.run(a, capture_output=True, text=True)
    if check and r.returncode:
        sys.exit("FATAL %s: %s" % (" ".join(a[:3]), r.stderr[:400]))
    return r.stdout


def exports(path):
    out = {}
    for l in run("llvm-nm", "-m", "--defined-only", "-U", path).splitlines():
        m = re.match(r"^[0-9a-f]+ \((\S+),(\S+)\) external (\S+)", l)
        if m:
            out[m.group(3)] = (m.group(1), m.group(2))
    return out


def load_info(path):
    """install name, versions, [(kind, name)] of the dependencies"""
    txt = run("llvm-otool", "-l", path)
    info = {"deps": []}
    blocks = re.split(r"\n(?=Load command )", txt)
    for b in blocks:
        cmd = re.search(r"cmd (\S+)", b)
        if not cmd:
            continue
        nm = re.search(r"name (\S+) \(offset", b)
        if cmd.group(1) == "LC_ID_DYLIB":
            info["id"] = nm.group(1)
            # llvm-otool prints "n/a" for 0xffffffff (65535.255.255), the "any version"
            # the wrapper images carry: keep it, a consumer that asks for 150.0.0 refuses 0.0.0
            def ver(k):
                m = re.search(k + r" version (\S+)", b)
                if m and re.match(r"^\d+(\.\d+){0,2}$", m.group(1)):
                    return m.group(1)
                return "65535.255.255" if m else "0.0.0"
            info["cur"] = ver("current")
            info["compat"] = ver("compatibility")
        elif cmd.group(1) in ("LC_LOAD_DYLIB", "LC_REEXPORT_DYLIB", "LC_LOAD_WEAK_DYLIB"):
            info["deps"].append((cmd.group(1), nm.group(1)))
    return info


INJECTED = ("darling-extras.dylib",)
# behaviour directories whose names an injected image must not shadow. Widen the list one image at a time, once the framework
# that carries the behaviour is known to be loaded wherever the injected image is (a name left out of both has no provider).
SHADOW_FROM = ("CoreText", "CoreTextExtras.dylib")
BEHAVIORS = os.path.join(os.path.dirname(os.path.abspath(__file__)), "stub-frameworks", "behaviors")


def compile_behaviors(img_name, work):
    """compile stub-frameworks/behaviors/<img_name>/*.c; returns the object files"""
    objs = []
    bdir = os.path.join(BEHAVIORS, img_name)
    if os.path.isdir(bdir):
        for fn in sorted(os.listdir(bdir)):
            if fn.endswith(".c"):
                bo = os.path.join(work, img_name + "." + fn + ".o")
                run("clang", "-target", "x86_64-apple-macos10.12", "-O1", "-fno-stack-protector",
                    "-c", os.path.join(bdir, fn), "-o", bo, check=True)
                objs.append(bo)
    return objs


def behavior_names(work, skip=None):
    """names exported by the behaviours of the SHADOW_FROM images (except `skip`)"""
    names = set()
    if os.path.isdir(BEHAVIORS):
        for d in sorted(os.listdir(BEHAVIORS)):
            if d != skip and d in SHADOW_FROM:
                for bo in compile_behaviors(d, work):
                    names |= set(exports(bo))
    return names


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--overlay", required=True)
    ap.add_argument("--work", required=True)
    ap.add_argument("--data-list")
    ap.add_argument("--install", action="store_true")
    ap.add_argument("--backup-dir", help="where --install keeps the original of each image "
                    "(default: DARLING_BACKUP_DIR, else 'backup' next to the overlay directory)")
    ap.add_argument("--chrome-providers", metavar="CHROME_ROOT",
                    help="also rebuild the stub images that provide a string constant the Chrome images under "
                         "CHROME_ROOT import as a zero object (data-only stubs are not in the stub list)")
    ap.add_argument("images", nargs="+")
    a = ap.parse_args()
    ov = a.overlay.rstrip("/")
    backup_root = a.backup_dir or os.environ.get("DARLING_BACKUP_DIR") or os.path.join(os.path.dirname(ov), "backup")
    if a.chrome_providers:
        import importlib.util
        here = os.path.dirname(os.path.abspath(__file__))
        sp = importlib.util.spec_from_file_location("dsc", os.path.join(here, "data-shadow-census.py"))
        dsc = importlib.util.module_from_spec(sp)
        sp.loader.exec_module(dsc)
        have = {os.path.realpath(i if os.path.isabs(i) else os.path.join(ov, i)) for i in a.images}
        for sym, prov, typ, who in dsc.chrome_imports(ov, a.chrome_providers, None):
            p = os.path.join(ov, prov)
            # stub sized images only: a real framework is never rewritten
            if typ == "string" and os.path.realpath(p) not in have and os.path.getsize(p) <= 256 * 1024:
                have.add(os.path.realpath(p))
                a.images.append(p)
    datalist = set()
    if a.data_list and os.path.exists(a.data_list):
        datalist = {l.strip() for l in open(a.data_list) if l.strip()}
    os.makedirs(a.work, exist_ok=True)
    libsys = os.path.join(ov, "usr/lib/libSystem.B.dylib")
    closure = []
    for l in run("llvm-objdump", "--macho", "--dylibs-used", libsys).splitlines():
        l = l.strip()
        if l.endswith("reexport)") and l.startswith("/"):
            closure.append(ov + l.split(" (")[0])
    pcache = {}
    rebuilt = 0
    for img in a.images:
        path = img if os.path.isabs(img) else os.path.join(ov, img)
        if R.is_excluded_image(path):
            continue
        ex = exports(path)
        info = load_info(path)
        provs = list(closure)
        # the image's dependencies and theirs (a constant defined two hops away, e.g. in
        # CoreFoundation behind Foundation, must not be shadowed either)
        todo = [n for k, n in info["deps"]]
        seen = set()
        while todo and len(seen) < 256:
            n = todo.pop(0)
            if n in seen or not os.path.exists(ov + n):
                continue
            seen.add(n)
            if ov + n not in provs and ov + n != path:
                provs.append(ov + n)
            todo += [m for k, m in load_info(ov + n)["deps"]]
        for p in provs:
            if p not in pcache:
                pcache[p] = exports(p)
        funcs, datas, drop = [], [], []
        for n, (sg, sn) in sorted(ex.items()):
            if R.OBJC.match(n) and re.match(r"_OBJC_(CLASS|METACLASS)_", n):
                continue  # classes are emitted from class_names()
            if n == "__objc_empty_cache":
                continue  # re-emitted (hidden) with the classes
            text = (sg, sn) == ("__TEXT", "__text")
            if any(n in pcache[p] and pcache[p][n][0].startswith("__DATA") for p in provs):
                drop.append(n)   # a real provider has the object: do not shadow it
            elif not text:
                datas.append(n)
            elif R.is_known_data(n) or n in datalist:
                datas.append(n)
            else:
                funcs.append(n)
        # hand written behaviour (stub-frameworks/behaviors/<image file name>/*.c) replaces the
        # generic no-op for the names it defines; it is compiled and linked into the image
        behaviors = compile_behaviors(os.path.basename(path), a.work)
        shadow = []
        if os.path.basename(path) in INJECTED:
            real = behavior_names(a.work, skip=os.path.basename(path))
            shadow = sorted(n for n in funcs + datas if n in real)
            funcs = [n for n in funcs if n not in real]
            datas = [n for n in datas if n not in real]
            # data names another stand-in owns (stub_data_rules.INJECTED_DATA_SHADOW)
            dshadow = sorted(n for n in datas if R.is_injected_data_shadow(n))
            datas = [n for n in datas if n not in dshadow]
            shadow = sorted(set(shadow) | set(dshadow))
        if behaviors:
            defined = set()
            for bo in behaviors:
                defined |= set(exports(bo))
            funcs = [n for n in funcs if n not in defined]
            datas = [n for n in datas if n not in defined]
        classes = R.class_names(ex)
        fix = [n for n in ex if n in drop or (ex[n] == ("__TEXT", "__text") and
               (R.is_known_data(n) or n in datalist))]
        # a string constant exported as a zero filled object is rebuilt as a pointer to a CFString
        fix += [n for n in datas if R.is_string_const(n) and n not in fix and
                ex[n][0].startswith("__DATA") and n not in drop]
        if not fix and not behaviors and not shadow:
            print("unchanged %s" % os.path.relpath(path, ov))
            continue
        base = re.sub(r"[^A-Za-z0-9_]", "_", os.path.basename(path))
        s = os.path.join(a.work, base + ".s")
        with open(s, "w") as f:
            f.write("# generated by rebuild-stub-data.py\n.text\n")
            for n in funcs:
                f.write(".globl %s\n.p2align 4\n%s:\n    xorl %%eax, %%eax\n    retq\n" % (n, n))
            for n in datas:
                f.write("\n".join(R.data_asm(n, strings=True)) + "\n")
            if classes:
                f.write(R.EMPTY_CACHE_S)
        objs = [os.path.join(a.work, base + ".s.o")] + behaviors
        run("clang", "-target", "x86_64-apple-macos10.12", "-c", s, "-o", objs[0], check=True)
        if classes:
            m = os.path.join(a.work, base + ".m")
            open(m, "w").write(R.classes_m(classes))
            objs.append(m + ".o")
            run("clang", "-target", "x86_64-apple-macos10.12", "-fobjc-runtime=macosx-10.12",
                "-c", m, "-o", m + ".o", check=True)
        out = os.path.join(a.work, base)
        cmd = ["ld64.lld", "-dylib", "-arch", "x86_64", "-platform_version", "macos", "10.12", "10.12",
               "-install_name", info["id"], "-current_version", info["cur"],
               "-compatibility_version", info["compat"]]
        for k, n in info["deps"]:
            if os.path.exists(ov + n):
                cmd += ["-reexport_library" if k == "LC_REEXPORT_DYLIB" else "-needed_library", ov + n]
        if behaviors:
            cmd += ["-undefined", "dynamic_lookup", "-bind_at_load"]
        if any(R.is_string_const(n) for n in datas):
            # the CFString isa is bound two-level to CoreFoundation itself, never by flat lookup
            # CoreFoundation goes FIRST: the linker binds the isa to the first input that defines it,
            # and a framework this image re-exports (CoreText, Foundation, ...) would otherwise be
            # recorded as the image the dynamic linker must find it in.
            cfp = ov + R.CF_INSTALL
            if os.path.realpath(cfp) != os.path.realpath(path):
                # re-exported when the image already re-exports it or it provides a dropped name
                # (the linker keeps only the first of two inputs naming the same dylib)
                rex = any(k == "LC_REEXPORT_DYLIB" and ov + n == cfp for k, n in info["deps"]) or \
                    (drop and cfp in provs and any(n in pcache[cfp] for n in drop))
                cmd[1:1] = ["-reexport_library" if rex else "-needed_library", cfp]
            cmd += ["-syslibroot", ov]   # CF re-exports libSystem by install name
        # a name dropped as shadowing needs its provider reachable from this image
        if drop:
            for p in provs:
                if any(n in pcache[p] for n in drop):
                    cmd += ["-reexport_library", p]
        cmd += ["-o", out] + objs
        run(*cmd, check=True)
        print("rebuilt %s: %d functions, %d data objects, %d ObjC classes, %d dropped (provider has them), %d retyped" % (
            os.path.relpath(path, ov), len(funcs), len(datas), len(classes), len(drop), len(fix)))
        if drop:
            print("  dropped: " + " ".join(drop))
        if shadow:
            print("  not exported (a behaviour of another image implements them): %d: %s" % (len(shadow), " ".join(shadow)))
        rebuilt += 1
        if a.install:
            bdir = os.path.join(backup_root, os.path.basename(path))
            bak = os.path.join(bdir, "pre-dataascode")
            if not os.path.exists(bak):
                os.makedirs(bdir, exist_ok=True)
                shutil.copy2(path, bak)
            shutil.copyfile(out, path)
            try:
                os.chmod(path, 0o755)
            except PermissionError:   # replaced in place, mode kept (file owned by another user)
                pass
    print("rebuild-stub-data: %d images rebuilt" % rebuilt)


if __name__ == "__main__":
    main()
