#!/bin/sh
# Exit-code parity between the harness and the reference ld.lld.
#
# The harness is only worth reading if it fails where the reference fails and
# succeeds where the reference succeeds. Run after build-ld64.sh; it builds
# nothing, it only links a trivial dylib and compares.
#
# Env:
#   LLVM_PREFIX  installed llvm        (default /usr/local/llvm21)
#   OBJ_DIR      harness binary lives here (default /tmp/lld-instrumented)
#   WORK_DIR     scratch for the test inputs (default /tmp/lld-harness-parity)
set -eu

LLVM_PREFIX="${LLVM_PREFIX:-/usr/local/llvm21}"
OBJ_DIR="${OBJ_DIR:-/tmp/lld-instrumented}"
WORK_DIR="${WORK_DIR:-/tmp/lld-harness-parity}"

HARNESS="$OBJ_DIR/ld64.lld"
REF="$LLVM_PREFIX/bin/ld.lld"

mkdir -p "$WORK_DIR"
printf 'int trivial(void) { return 1; }\n' > "$WORK_DIR/trivial.c"
"$LLVM_PREFIX/bin/clang" -target arm64-apple-macos11 \
  -c "$WORK_DIR/trivial.c" -o "$WORK_DIR/trivial.o"

# one DESC CMD... -> RC and ERR hold the outcome, ERR without the main: markers
one() {
  DESC="$1"
  shift
  "$@" >/dev/null 2>"$WORK_DIR/err.txt" && RC=0 || RC=$?
  ERR=$(grep -v '^main: ' "$WORK_DIR/err.txt" | tr '\n' '|')
}

row() {
  [ "$2" -eq "$3" ] && same=yes || same=NO
  printf '%-32s %8s %10s  parity=%s\n' "$1" "$2" "$3" "$same"
  printf '  harness  stderr: %s\n' "$4"
  printf '  reference stderr: %s\n' "$5"
}

printf '== case 1: -v (config runs before the -v handler) ==\n'
one "case" "$HARNESS" -v; h=$RC; he=$ERR
one "case" "$REF" -flavor darwin -v; r=$RC; re=$ERR
row '-v' "$h" "$r" "$he" "$re"

printf '== case 2: failing link, input file does not exist ==\n'
one "case" "$HARNESS" -dylib -arch arm64 -platform_version macos 11.0 11.0 \
  "$WORK_DIR/does-not-exist.o" -o "$WORK_DIR/missing.dylib"
h=$RC; he=$ERR
one "case" "$REF" -flavor darwin -dylib -arch arm64 \
  -platform_version macos 11.0 11.0 "$WORK_DIR/does-not-exist.o" \
  -o "$WORK_DIR/missing.dylib"
r=$RC; re=$ERR
row '-dylib ... does-not-exist.o' "$h" "$r" "$he" "$re"

printf '== case 3: successful link, trivial arm64 dylib ==\n'
rm -f "$WORK_DIR/libtrivial.dylib"
one "case" "$HARNESS" -dylib -arch arm64 -platform_version macos 11.0 11.0 \
  -install_name /usr/lib/libtrivial.dylib "$WORK_DIR/trivial.o" \
  -o "$WORK_DIR/harness-trivial.dylib"
h=$RC; he=$ERR; hs=$(wc -c < "$WORK_DIR/harness-trivial.dylib" 2>/dev/null || echo 0)
rm -f "$WORK_DIR/libtrivial.dylib"
one "case" "$REF" -flavor darwin -dylib -arch arm64 \
  -platform_version macos 11.0 11.0 -install_name /usr/lib/libtrivial.dylib \
  "$WORK_DIR/trivial.o" -o "$WORK_DIR/reference-trivial.dylib"
r=$RC; re=$ERR; rs=$(wc -c < "$WORK_DIR/reference-trivial.dylib" 2>/dev/null || echo 0)
row '-dylib ... trivial.o' "$h" "$r" "$he" "$re"
printf '  output sizes: harness %s bytes, reference %s bytes\n' "$hs" "$rs"