#!/usr/bin/env python3
"""build-extras-objc-classes.py — Control #62.

Rebuild the /usr/lib/*Extras.dylib wrappers so that _OBJC_CLASS_$_X and
_OBJC_METACLASS_$_X are exported as REAL ObjC class data (compiled root classes
with a valid isa/metaclass), not as code no-op stubs (`void X(void){}`).

Why: the Extras wrappers were generated with every missing symbol as a void
function, including ObjC class names. libobjc's readClass then read a stub's
code bytes as a class object and faulted (see Control #61). The bind is strong
(removing the names makes Chrome fail with "Symbol not found"), so the names must
be exported — as data.

Idempotent: a wrapper whose ObjC names are already data classes is rebuilt
unchanged. Reads each wrapper's own export list and re-export target.

Environment:
  DARLING_OVERLAY    — overlay dir (required)
  DARLING_BUILD_DIR  — scratch dir (required)
"""
import os
import re
import subprocess

OD = os.environ["DARLING_OVERLAY"]
BUILD = os.environ["DARLING_BUILD_DIR"] + "/extras-objc-classes"
EXTRAS = os.path.join(OD, "usr/lib")
OBJC = re.compile(r"_OBJC_(CLASS|METACLASS)_\$_(.+)")
TYPES = ("T", "S", "D", "t", "s", "d")


def run(cmd):
    return subprocess.run(cmd, capture_output=True, text=True)


def exports(path):
    out = run(["llvm-nm", "-gU", path]).stdout
    syms = []
    for line in out.splitlines():
        parts = line.split()
        if len(parts) >= 3 and parts[1] in TYPES:
            syms.append(parts[2])
    return syms


def reexport_target(path):
    out = run(["llvm-otool", "-l", path]).stdout
    m = re.search(r"LC_REEXPORT_DYLIB.*?name (\S+)", out, re.S)
    return m.group(1) if m else None


def build_empty_cache():
    src = os.path.join(BUILD, "empty_cache.s")
    obj = os.path.join(BUILD, "empty_cache.o")
    with open(src, "w") as f:
        f.write("\t.section\t__DATA,__data\n\t.globl\t__objc_empty_cache\n"
                "\t.p2align\t3\n__objc_empty_cache:\n\t.quad\t0\n\t.quad\t0\n")
    r = run(["clang", "-target", "x86_64-apple-macos10.12", "-c", src, "-o", obj])
    if r.returncode:
        raise SystemExit("clang failed for empty_cache.s: " + r.stderr[:300])
    return obj


def main():
    os.makedirs(BUILD, exist_ok=True)
    empty_cache = build_empty_cache()
    for fn in sorted(os.listdir(EXTRAS)):
        if not fn.endswith("Extras.dylib") or fn == "darling-extras.dylib":
            continue
        wrapper = os.path.join(EXTRAS, fn)
        syms = exports(wrapper)
        objc = [s for s in syms if OBJC.search(s)]
        if not objc:
            continue
        keep = [s for s in syms if not OBJC.search(s)]
        classes = sorted({OBJC.search(s).group(2) for s in objc})
        target = reexport_target(wrapper)
        real = OD + target if target else None
        if not real or not os.path.exists(real):
            print("SKIP", fn, real)
            continue
        cpath = os.path.join(BUILD, fn + ".c")
        mpath = os.path.join(BUILD, fn + ".m")
        with open(cpath, "w") as f:
            for s in keep:
                f.write(f"void {s[1:]}(void) {{}}\n")
        with open(mpath, "w") as f:
            for name in classes:
                f.write(f"__attribute__((objc_root_class))\n@interface {name}\n@end\n"
                        f"@implementation {name}\n@end\n")
        objs = []
        for src, out, extra in ((cpath, fn + ".c.o", []),
                                (mpath, fn + ".m.o", ["-fobjc-runtime=macosx-10.12"])):
            op = os.path.join(BUILD, out)
            r = run(["clang", "-target", "x86_64-apple-macos10.12"] + extra + ["-c", src, "-o", op])
            if r.returncode:
                print("CLANG FAIL", fn, r.stderr[:200])
                objs = None
                break
            objs.append(op)
        if objs is None:
            continue
        r = run(["ld64.lld", "-dylib", "-arch", "x86_64",
                 "-platform_version", "macos", "10.12", "10.12",
                 "-install_name", "/usr/lib/" + fn,
                 "-reexport_library", real,
                 "-o", wrapper] + objs + [empty_cache])
        if r.returncode:
            print("LD FAIL", fn, r.stderr[:300])
            continue
        print(f"rebuilt {fn}: {len(classes)} ObjC classes + {len(keep)} stub functions")


if __name__ == "__main__":
    main()
