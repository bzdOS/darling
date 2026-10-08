#!/bin/sh
# The stub-helper segfault, through the harness and through the reference.
#
# One object (the thin dyld stub binder) plus -flat_namespace + -undefined
# suppress is the minimal input that kills lld::macho::link; the harness and
# ld.lld die the same way. Prints both exit codes, both stderr tails and the
# harness backtrace.
#
# Env:
#   BUILD_DIR        darling build tree holding the thin object (required)
#   OBJ_DIR          harness binary + its core (default /tmp/lld-instrumented)
#   LLVM_PREFIX      installed llvm, for the reference (default /usr/local/llvm21)
#   WORK_DIR         scratch (default /tmp/lld-harness-parity)
set -eu

BUILD_DIR="${DARLING_BUILD_DIR:?set DARLING_BUILD_DIR to the darling build tree}"
LLVM_PREFIX="${LLVM_PREFIX:-/usr/local/llvm21}"
OBJ_DIR="${OBJ_DIR:-/tmp/lld-instrumented}"
WORK_DIR="${WORK_DIR:-/tmp/lld-harness-parity}"

HARNESS="$OBJ_DIR/ld64.lld"
REF="$LLVM_PREFIX/bin/ld.lld"
OBJ="$BUILD_DIR/src/external/dyld/CMakeFiles/system_dyld_obj.dir/src/dyld_stub_binder.S.o.x86_64.o"
[ -f "$OBJ" ] || { echo "missing input object: $OBJ" >&2; exit 2; }

mkdir -p "$WORK_DIR"
set -- -arch x86_64 -platform_version macos 11.0 11.0 -dylib \
  -flat_namespace -undefined suppress \
  -install_name /usr/lib/system/libdyld.dylib

echo "== harness =="
( cd "$OBJ_DIR" && ./ld64.lld "$@" -o "$WORK_DIR/harness-stub.dylib" "$OBJ" ) \
  2> "$WORK_DIR/harness.err" || echo "rc=$?"
grep -E '^main: ' "$WORK_DIR/harness.err" | sed 's/^/  marker: /'

echo "== reference =="
"$REF" -flavor darwin "$@" -o "$WORK_DIR/reference-stub.dylib" "$OBJ" \
  2> "$WORK_DIR/reference.err" || echo "rc=$?"
grep -E '#[0-9]+ 0x' "$WORK_DIR/reference.err" | sed -n '6,9p'

echo "== harness backtrace =="
lldb -b -c "$OBJ_DIR/ld64.lld.core" -o 'bt 12' -o quit "$HARNESS" 2>&1 |
  sed -n '/stop reason/,/thread_start/p'