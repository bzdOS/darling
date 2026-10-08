#!/bin/sh
# Compile every lld source the harness needs, one compiler call per file.
#
# The harness is built against the installed llvm headers, which are newer than
# the lld sources in the system tree. Two sources therefore need a one-argument
# patch, and the option table needs the tblgen step in gen-options.sh; everything
# else compiles as it stands. Three sources below carry a patch, the rest are
# compiled in place.
#
# Include order matters twice over:
#   * lld/include BEFORE the llvm install, because the installed llvm ships its
#     own lld/Common/ErrorHandler.h and that copy no longer declares
#     lld::outs()/lld::errs(), which the system lld sources still use.
#   * -I on the MachO directory, because Arch/*.cpp include "Arch/X.h" and
#     "InputFiles.h" relative to it.
#
# Env:
#   LLVM_PREFIX  installed llvm        (default /usr/local/llvm21)
#   LLD_SRC      lld sources            (default /usr/src/contrib/llvm-project)
#   GEN_DIR      generated table + staged patched sources (default /tmp/lld-gen)
#   OBJ_DIR      objects land here      (default /tmp/lld-instrumented)
set -eu

LLVM_PREFIX="${LLVM_PREFIX:-/usr/local/llvm21}"
LLD_SRC="${LLD_SRC:-/usr/src/contrib/llvm-project}"
GEN_DIR="${GEN_DIR:-/tmp/lld-gen}"
OBJ_DIR="${OBJ_DIR:-/tmp/lld-instrumented}"
HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)

CXX="$LLVM_PREFIX/bin/clang++"
INCLUDES="-I$GEN_DIR -I$LLD_SRC/lld/include -I$LLD_SRC/lld/MachO -I$LLVM_PREFIX/include"

# source path relative to $LLD_SRC/lld : patch to apply to it
PATCHED="MachO/DriverUtils.cpp:$HERE/patches/DriverUtils.opt-table.patch
MachO/LTO.cpp:$HERE/patches/LTO.thin-backend-pool.patch
Common/DWARF.cpp:$HERE/patches/DWARF.line-table-addr.patch"

patch_for() {
  echo "$PATCHED" | grep "^$1:" | cut -d: -f2 || true
}

mkdir -p "$GEN_DIR/lld/MachO" "$GEN_DIR/lld/Common" "$OBJ_DIR"

compile_one() {
  src="$1"
  rel="${src#$LLD_SRC/lld/}"
  patch=$(patch_for "$rel")
  if [ -n "$patch" ]; then
    staged="$GEN_DIR/lld/$rel"
    cp "$src" "$staged"
    patch -s -p1 -d "$GEN_DIR" -i "$patch"
    src="$staged"
  fi
  # shellcheck disable=SC2086
  if "$CXX" -std=c++17 -fno-exceptions -fno-rtti $INCLUDES \
       -c "$src" -o "$OBJ_DIR/$(basename "$src" .cpp).o"; then
    echo "OK   $rel"
  else
    echo "FAIL $rel" >&2
    return 1
  fi
}

rc=0
for src in "$LLD_SRC"/lld/MachO/*.cpp "$LLD_SRC"/lld/MachO/Arch/*.cpp \
           "$LLD_SRC"/lld/Common/*.cpp; do
  compile_one "$src" || rc=1
done

# The harness entry point with its markers and its exit-code convention.
# shellcheck disable=SC2086
"$CXX" -std=c++17 -fno-exceptions -fno-rtti $INCLUDES \
  -c "$HERE/main.cpp" -o "$OBJ_DIR/main.o" || rc=1

ls "$OBJ_DIR"/*.o | wc -l | tr -d ' ' | sed 's/^/objects: /'
exit $rc