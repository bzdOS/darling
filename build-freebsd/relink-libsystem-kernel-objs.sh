#!/bin/sh
# relink-libsystem-kernel-objs.sh -- rebuild libsystem_kernel.dylib after a change to a few
# objects, without a tree build. Generalises relink-libsystem-kernel-sysctl.sh (read its header
# for the method and its limits): every object named on the command line is recompiled for
# x86_64 with its exact ninja command into <outdir>, and the thin x86_64 dylib is relinked with
# those objects substituted. Nothing is written into the build tree or the overlay.
#
# Usage: DARLING_SRC_DIR=... DARLING_BUILD_DIR=... DARLING_OVERLAY=... \
#        sh build-freebsd/relink-libsystem-kernel-objs.sh <outdir> <object path in the build tree>...
# e.g. objects: src/external/xnu/darling/src/libsystem_kernel/libsyscall/CMakeFiles/libsyscall.dir/mach/mach_init.c.o
set -e
SRC="${DARLING_SRC_DIR:?set DARLING_SRC_DIR}"
BD="${DARLING_BUILD_DIR:?set DARLING_BUILD_DIR}"
OVERLAY="${DARLING_OVERLAY:?set DARLING_OVERLAY (libdyld input)}"
OUT="${1:?usage: relink-libsystem-kernel-objs.sh <outdir> <object>...}"
shift
mkdir -p "$OUT"
cd "$BD"
export PATH="${BD}/src/external/cctools-port/cctools/misc:${PATH}"
TARGET=src/external/xnu/darling/src/libsystem_kernel/libsystem_kernel.dylib
LIBDYLD="${OVERLAY}/usr/lib/system/libdyld.dylib"
ninja -t commands "$TARGET" > "$OUT/commands.txt"

DARLING_SRC_DIR="$SRC" python3 - "$OUT" "$TARGET" "$LIBDYLD" "$SRC" "$@" <<'PY'
import sys, shlex, os
out, target, libdyld, src = sys.argv[1:5]
objs = sys.argv[5:]
lines = open(out + "/commands.txt").read().split("\n")
cc = []
repl = {}
for n, obj in enumerate(objs):
    cmd = [l for l in lines if "-o " + obj + " " in l][0]
    t = shlex.split(cmd); o = []; i = 0
    while i < len(t):
        x = t[i]
        if x == "-arch": i += 2; continue
        if x in ("-MT", "-MF", "-o"): i += 2; continue
        if x == "-MD": i += 1; continue
        o.append(x); i += 1
    new = "%s/obj%d.o" % (out, n)
    j = o.index("-c")
    o[j:j] = ["-arch", "x86_64", "-o", new]
    cc.append(" ".join(shlex.quote(x) for x in o))
    repl[obj] = new
open(out + "/cc.sh", "w").write("\n".join(cc) + "\n")
link = [l for l in lines if "-o " + target + " " in l and "-shared" in l][-1]
link = link.replace("-arch x86_64 -arch i386", "-arch x86_64").replace(" -arch i386 -arch x86_64 -arch x86_64 ", " ")
link = link.replace("-o " + target, "-o " + out + "/libsystem_kernel.x86_64")
ld = src + "/build-host-tools/ld64/x86_64-apple-darwin20-ld"
link = link.replace("-fuse-ld=" + [w for w in link.split() if w.startswith("-fuse-ld=")][0][9:], "-fuse-ld=" + ld)
link = link.replace("-B " + [w for w in link.split() if w.startswith("/") and w.endswith("ld64/src/")][0],
                    "-B " + src + "/build-host-tools/ld64/")
link = link.replace(" src/external/dyld/libsystem_dyld_firstpass.dylib ", " " + libdyld + " ")
link = link.replace("src/libsimple-darling/liblibsimple_darling.a",
                    "src/libsimple-darling/CMakeFiles/libsimple_darling.dir/src/lock.c.o")
link = " ".join(w for w in link.split(" ") if "libsyscall_32.dir" not in w)
for obj, new in repl.items():
    assert link.count(" " + obj + " ") == 1, obj
    link = link.replace(" " + obj + " ", " " + new + " ")
open(out + "/link.sh", "w").write(link + "\n")
PY

sh "$OUT/cc.sh"
sh "$OUT/link.sh"

# post-link-fixups: same as the sysctl script (install-time version fields)
python3 - "$OUT/libsystem_kernel.x86_64" "$OUT/libsystem_kernel.dylib" <<'PY'
import struct, sys
d = bytearray(open(sys.argv[1], "rb").read())
n = struct.unpack_from("<I", d, 16)[0]; o = 32
for _ in range(n):
    cmd, sz = struct.unpack_from("<II", d, o)
    if cmd == 0xd:
        struct.pack_into("<II", d, o + 16, 0xffffffff, 0xffffffff)
    if cmd == 0xc:
        so = struct.unpack_from("<I", d, o + 8)[0]
        if bytes(d[o + so:o + sz]).split(b"\0")[0] == b"/usr/lib/system/libdyld.dylib":
            struct.pack_into("<II", d, o + 16, 0, 0x10000)
    o += sz
open(sys.argv[2], "wb").write(d)
PY
file "$OUT/libsystem_kernel.dylib"
md5 "$OUT/libsystem_kernel.dylib" 2>/dev/null || md5sum "$OUT/libsystem_kernel.dylib"
