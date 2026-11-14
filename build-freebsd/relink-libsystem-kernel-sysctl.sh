#!/bin/sh
# relink-libsystem-kernel-sysctl.sh -- rebuild libsystem_kernel.dylib after a change to
# emulation/src/xnu_syscall/bsd/impl/misc/sysctl.c, without a tree build.
#
# Why not the usual narrow build: narrow-build-target.sh stops on missing firstpass
# dylibs. Here only ONE object changes, so the existing objects are relinked:
#   1. recompile sysctl.c for x86_64 only, with the exact ninja command, into $OUT
#      (nothing is written into the build tree);
#   2. relink the thin x86_64 dylib from the tree's objects with the exact ninja link
#      command, with these substitutions: x86_64 only; the real cctools ld64 from
#      build-host-tools; libdyld taken from the overlay (the firstpass dylib does not
#      link); the i386 libsyscall objects dropped; lock.c.o instead of the stale archive;
#   3. post-process (post-link-fixups below) and write $OUT/libsystem_kernel.dylib.
# Verified once against an unchanged object: same text, same exports, same binds as the
# tree's own libsystem_kernel.dylib.x86_64; only LC_UUID and the debug-map timestamps differ.
#
# Usage: DARLING_SRC_DIR=... DARLING_BUILD_DIR=... sh build-freebsd/relink-libsystem-kernel-sysctl.sh <outdir>
set -e
SRC="${DARLING_SRC_DIR:?set DARLING_SRC_DIR}"
BD="${DARLING_BUILD_DIR:?set DARLING_BUILD_DIR}"
OVERLAY="${DARLING_OVERLAY:-}"
OUT="${1:?usage: relink-libsystem-kernel-sysctl.sh <outdir>}"
mkdir -p "$OUT"
cd "$BD"
export PATH="${BD}/src/external/cctools-port/cctools/misc:${PATH}"
TARGET=src/external/xnu/darling/src/libsystem_kernel/libsystem_kernel.dylib
OBJ=src/external/xnu/darling/src/libsystem_kernel/emulation/CMakeFiles/emulation.dir/src/xnu_syscall/bsd/impl/misc/sysctl.c.o
LIBDYLD="${OVERLAY:?set DARLING_OVERLAY (libdyld input)}/usr/lib/system/libdyld.dylib"

ninja -t commands "$TARGET" > "$OUT/commands.txt"

DARLING_SRC_DIR="$SRC" python3 - "$OUT" "$OBJ" "$TARGET" "$LIBDYLD" "$SRC" <<'PY'
import sys, shlex
out, obj, target, libdyld, src = sys.argv[1:6]
lines = open(out + "/commands.txt").read().split("\n")
# 1. compile
cmd = [l for l in lines if "-o " + obj + " " in l][0]
t = shlex.split(cmd); o = []; i = 0
while i < len(t):
    x = t[i]
    if x == "-arch": i += 2; continue
    if x in ("-MT", "-MF", "-o"): i += 2; continue
    if x == "-MD": i += 1; continue
    o.append(x); i += 1
j = o.index("-c")
o[j:j] = ["-arch", "x86_64", "-o", out + "/sysctl.o"]
open(out + "/cc.sh", "w").write(" ".join(shlex.quote(x) for x in o) + "\n")
# 2. link
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
assert link.count(" " + obj + " ") == 1
link = link.replace(" " + obj + " ", " " + out + "/sysctl.o ")
open(out + "/link.sh", "w").write(link + "\n")
PY

sh "$OUT/cc.sh"
sh "$OUT/link.sh"
file "$OUT/libsystem_kernel.x86_64"

# post-link-fixups: the installed copy carries LC_ID_DYLIB current_version and
# compatibility_version = 0xffffffff; and the original link recorded libdyld as
# compat 1.0.0 / current 0.0.0 (this link used the overlay libdyld as its input).
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
