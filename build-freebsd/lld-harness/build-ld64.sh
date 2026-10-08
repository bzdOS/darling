#!/bin/sh
# Build the instrumented ld64.lld harness and run the option-table check.
#
# The Mach-O lld objects are compiled one file at a time by the per-file compile
# scripts into $OBJ_DIR; this script only rebuilds the option table (see
# gen-options.sh), the patched DriverUtils, then links everything and checks
# that the option table no longer crashes the constructor.
#
# Env:
#   LLVM_PREFIX  installed llvm        (default /usr/local/llvm21)
#   LLD_SRC      lld/llvm sources      (default /usr/src/contrib/llvm-project)
#   GEN_DIR      generated headers     (default /tmp/lld-gen)
#   OBJ_DIR      harness objects + lld (default /tmp/lld-instrumented)
set -eu

LLVM_PREFIX="${LLVM_PREFIX:-/usr/local/llvm21}"
LLD_SRC="${LLD_SRC:-/usr/src/contrib/llvm-project}"
GEN_DIR="${GEN_DIR:-/tmp/lld-gen}"
OBJ_DIR="${OBJ_DIR:-/tmp/lld-instrumented}"
HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)

CXX="$LLVM_PREFIX/bin/clang++"
INCLUDES="-I$GEN_DIR -I$GEN_DIR/lld/MachO -I$LLD_SRC/lld/include -I$LLVM_PREFIX/include -I$LLD_SRC/llvm/include"

# Options.inc must be on the include path, and the patched DriverUtils.cpp must
# sit next to Config.h (a quoted include resolves against the source directory
# first, as it does in the real lld build).
sh "$HERE/gen-options.sh"

mkdir -p "$GEN_DIR/lld/MachO" "$OBJ_DIR"
for h in Config.h Driver.h Dwarf.h EhFrame.h ExportTrie.h ICF.h InputFiles.h \
         InputSection.h LTO.h Layout.h MachOStructs.h MapFile.h MarkLive.h \
         ObjC.h OutputSection.h OutputSegment.h Relocations.h \
         SectionPriorities.h SymbolTable.h Symbols.h SyntheticSections.h \
         Target.h; do
  ln -sf "$LLD_SRC/lld/MachO/$h" "$GEN_DIR/lld/MachO/$h"
done
cp "$LLD_SRC/lld/MachO/DriverUtils.cpp" "$GEN_DIR/lld/MachO/DriverUtils.cpp"
patch -s -p0 "$GEN_DIR/lld/MachO/DriverUtils.cpp" \
  < "$HERE/patches/DriverUtils.opt-table.patch"

# shellcheck disable=SC2086
"$CXX" -std=c++17 -fno-exceptions -fno-rtti $INCLUDES \
  -c "$GEN_DIR/lld/MachO/DriverUtils.cpp" -o "$OBJ_DIR/DriverUtils.o"

# every hand-built DriverUtils object is replaced by the one compiled above
OBJS=$(ls "$OBJ_DIR"/*.o | grep -v '/DriverUtils[^/]*\.o$')
# shellcheck disable=SC2086
"$CXX" -o "$OBJ_DIR/ld64.lld" "$OBJ_DIR/DriverUtils.o" $OBJS \
  -L"$LLVM_PREFIX/lib" -lLLVM -lc++ -lcxxrt -lpthread

echo "linked $OBJ_DIR/ld64.lld ($(wc -c < "$OBJ_DIR/ld64.lld") bytes)"

# The check: the option-table constructor must not fault. Exit codes are
# inverted by the harness main(), so both the code and the stderr text are shown.
"$OBJ_DIR/ld64.lld" -v; echo "-v rc=$?"
"$OBJ_DIR/ld64.lld" --help 2>/dev/null | wc -l