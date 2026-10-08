#!/bin/sh
# Build the instrumented ld64.lld harness from scratch and check it.
#
# Steps: regenerate the option table, compile every lld source one file per
# compiler call, link with main.cpp, run the exit-code parity against the
# reference ld.lld.
#
# Env:
#   LLVM_PREFIX  installed llvm        (default /usr/local/llvm21)
#   LLD_SRC      lld sources            (default /usr/src/contrib/llvm-project)
#   GEN_DIR      generated table + staged patched sources (default /tmp/lld-gen)
#   OBJ_DIR      objects and the binary (default /tmp/lld-instrumented)
#   WORK_DIR     scratch for the parity inputs (default /tmp/lld-harness-parity)
set -eu

LLVM_PREFIX="${LLVM_PREFIX:-/usr/local/llvm21}"
LLD_SRC="${LLD_SRC:-/usr/src/contrib/llvm-project}"
GEN_DIR="${GEN_DIR:-/tmp/lld-gen}"
OBJ_DIR="${OBJ_DIR:-/tmp/lld-instrumented}"
WORK_DIR="${WORK_DIR:-/tmp/lld-harness-parity}"
HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)

CXX="$LLVM_PREFIX/bin/clang++"
export LLVM_PREFIX LLD_SRC GEN_DIR OBJ_DIR WORK_DIR

# A stale object in $OBJ_DIR is a duplicate-symbol link error, not a hint.
rm -f "$OBJ_DIR"/*.o

sh "$HERE/gen-options.sh"
sh "$HERE/compile-sources.sh"

OBJS=$(ls "$OBJ_DIR"/*.o)
# shellcheck disable=SC2086
"$CXX" -o "$OBJ_DIR/ld64.lld" $OBJS \
  -L"$LLVM_PREFIX/lib" -lLLVM -lc++ -lcxxrt -lpthread
echo "linked $OBJ_DIR/ld64.lld ($(wc -c < "$OBJ_DIR/ld64.lld") bytes)"

sh "$HERE/parity.sh"