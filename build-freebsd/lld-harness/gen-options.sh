#!/bin/sh
# Regenerate the Mach-O option table with the installed llvm-tblgen.
#
# The generated file lives in $GEN_DIR; the copy under generated/ in this
# directory is the reference to compare against (sha256 in README.md).
set -eu

LLVM_PREFIX="${LLVM_PREFIX:-/usr/local/llvm21}"
LLD_SRC="${LLD_SRC:-/usr/src/contrib/llvm-project}"
GEN_DIR="${GEN_DIR:-/tmp/lld-gen}"

TBLGEN="$LLVM_PREFIX/bin/llvm-tblgen"
OPTIONS_TD="$LLD_SRC/lld/MachO/Options.td"

mkdir -p "$GEN_DIR"

"$TBLGEN" --gen-opt-parser-defs \
  -I"$LLD_SRC/lld/MachO" \
  -I"$LLVM_PREFIX/include" \
  -o "$GEN_DIR/Options.inc" \
  "$OPTIONS_TD"

echo "wrote $GEN_DIR/Options.inc ($(wc -c < "$GEN_DIR/Options.inc") bytes)"
sha256 "$GEN_DIR/Options.inc"