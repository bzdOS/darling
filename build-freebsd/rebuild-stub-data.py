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
  * everything else stays a function (`xorl %eax,%eax; ret`).
Install name, versions, re-exports and dependencies are kept. An image with nothing
to fix is left alone. With --install the old file is copied to <file>.pre-dataascode
(never overwritten) and replaced by the rebuilt one.

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


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--overlay", required=True)
    ap.add_argument("--work", required=True)
    ap.add_argument("--data-list")
    ap.add_argument("--install", action="store_true")
    ap.add_argument("images", nargs="+")
    a = ap.parse_args()
    ov = a.overlay.rstrip("/")
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
        behaviors = []
        bdir = os.path.join(os.path.dirname(os.path.abspath(__file__)), "stub-frameworks",
                            "behaviors", os.path.basename(path))
        if os.path.isdir(bdir):
            cf = ov + "/System/Library/Frameworks/CoreFoundation.framework/Versions/A/CoreFoundation"
            for fn in sorted(os.listdir(bdir)):
                if fn.endswith(".c"):
                    bo = os.path.join(a.work, os.path.basename(path) + "." + fn + ".o")
                    run("clang", "-target", "x86_64-apple-macos10.12", "-O1", "-fno-stack-protector",
                        "-c", os.path.join(bdir, fn), "-o", bo, check=True)
                    behaviors.append(bo)
            defined = set()
            for bo in behaviors:
                defined |= set(exports(bo))
            funcs = [n for n in funcs if n not in defined]
            datas = [n for n in datas if n not in defined]
        classes = R.class_names(ex)
        fix = [n for n in ex if n in drop or (ex[n] == ("__TEXT", "__text") and
               (R.is_known_data(n) or n in datalist))]
        if not fix and not behaviors:
            print("unchanged %s" % os.path.relpath(path, ov))
            continue
        base = re.sub(r"[^A-Za-z0-9_]", "_", os.path.basename(path))
        s = os.path.join(a.work, base + ".s")
        with open(s, "w") as f:
            f.write("# generated by rebuild-stub-data.py\n.text\n")
            for n in funcs:
                f.write(".globl %s\n.p2align 4\n%s:\n    xorl %%eax, %%eax\n    retq\n" % (n, n))
            for n in datas:
                f.write("\n".join(R.data_asm(n)) + "\n")
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
        rebuilt += 1
        if a.install:
            bak = path + ".pre-dataascode"
            if not os.path.exists(bak):
                shutil.copy2(path, bak)
            shutil.copyfile(out, path)
            try:
                os.chmod(path, 0o755)
            except PermissionError:   # replaced in place, mode kept (file owned by another user)
                pass
    print("rebuild-stub-data: %d images rebuilt" % rebuilt)


if __name__ == "__main__":
    main()
