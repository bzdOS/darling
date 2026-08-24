#!/bin/sh
# Cross-compile real Mach-O test binaries with a real toolchain (clang
# targeting x86_64-apple-macos + LLVM's ld64.lld, linking against the actual
# libSystem.B.dylib), as opposed to the hand-assembled ones gen-hello-*.py
# produces byte by byte.
#
# Produces:
#   tests/hello-cctools-macho — trivial puts() program (tests/hello-cctools.c)
#   tests/sqlite3-real-macho  — the real, unmodified upstream SQLite CLI
#                               (src/external/sqlite/{sqlite3,shell}.c)
#   tests/hello-objc-macho    — minimal Objective-C program exercising the
#                               real libobjc.A.dylib runtime (tests/hello-objc.m)
#   tests/hello-cf-macho      — minimal CoreFoundation program (CFString
#                               create/uppercase/length) against the real
#                               CoreFoundation.framework (tests/hello-cf.c)
#
# Usage: sh build-freebsd/build-real-macho-tests.sh
# Run on the FreeBSD dev VM (185) — needs clang and ld64.lld (both ship with
# the llvm* pkg already required for other builds in this repo).
#
# Environment:
#   DARLING_BUILD_DIR — scratch build dir        (default: /tmp/darling-build)
#   DARLING_SRC_DIR    — root of this repository  (default: directory of this script/..)
#   DARLING_OVERLAY    — darling overlay dir      (default: /path/to/darling-overlay)
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
SRC="${DARLING_SRC_DIR:-${SCRIPT_DIR}}"
BUILD="${DARLING_BUILD_DIR:-/tmp/darling-build}/real-macho"
OVERLAY="${DARLING_OVERLAY:-/path/to/darling-overlay}"

SDK_FLAT="${BUILD}/sdk-flat"
STAGED_OVERLAY="${BUILD}/staged-overlay"

rm -rf "${BUILD}"
mkdir -p "${SDK_FLAT}" "${STAGED_OVERLAY}/usr/lib/system"

# --- Unpack the pre-flattened SDK headers (see tests/vendor/README.md for
# --- why this can't just be -isysroot'd from the SDK tree directly: virtiofs
# --- can't resolve the SDK's symlinks, and re-flattening them from the guest
# --- risks the fuse_msgbuf OOM incident documented there). ---
tar xzf "${SRC}/tests/vendor/macosx-sdk-flat.tar.gz" -C "${SDK_FLAT}"

# --- Stage libSystem.B.dylib + its usr/lib/system/ dependency closure as
# --- regular files (same virtiofs-symlink reasoning as launch-dynamic-smoke.c:
# --- readlink() over the mount is broken, so copy with find|pax, never cp -a). ---
cp "${OVERLAY}/usr/lib/libSystem.B.dylib" "${STAGED_OVERLAY}/usr/lib/libSystem.B.dylib"
cp "${OVERLAY}/usr/lib/libobjc.A.dylib" "${STAGED_OVERLAY}/usr/lib/libobjc.A.dylib"
ln -sf libobjc.A.dylib "${STAGED_OVERLAY}/usr/lib/libobjc.dylib"
cp "${OVERLAY}/usr/lib/libicucore.A.dylib" "${STAGED_OVERLAY}/usr/lib/libicucore.A.dylib"
cp "${OVERLAY}/usr/lib/libc++.1.dylib" "${STAGED_OVERLAY}/usr/lib/libc++.1.dylib"
cp "${OVERLAY}/usr/lib/libc++abi.dylib" "${STAGED_OVERLAY}/usr/lib/libc++abi.dylib"
(cd "${OVERLAY}/usr/lib/system" && find . -maxdepth 1 -type f | pax -rw "${STAGED_OVERLAY}/usr/lib/system")
# CoreFoundation.framework's actual binary lives at the bottom of a chain of
# framework-bundle convenience symlinks (Versions/Current -> A, etc.) that
# virtiofs can't resolve (see tests/vendor/README.md); the real regular file
# is always at Versions/A/<Name> directly.
mkdir -p "${STAGED_OVERLAY}/System/Library/Frameworks/CoreFoundation.framework/Versions/A"
cp "${OVERLAY}/System/Library/Frameworks/CoreFoundation.framework/Versions/A/CoreFoundation" \
    "${STAGED_OVERLAY}/System/Library/Frameworks/CoreFoundation.framework/Versions/A/CoreFoundation"

CLANG_FLAGS="-target x86_64-apple-macos10.12 -nostdinc -D__DARWIN_ONLY_UNIX_CONFORMANCE=1"
CLANG_FLAGS="${CLANG_FLAGS} -I${SDK_FLAT}/sdk-flat/usr/include -I${SRC}/tests/vendor/fakesdk"
LD_FLAGS="-arch x86_64 -platform_version macos 10.12 10.12 -syslibroot ${STAGED_OVERLAY} -Z"

echo "=== hello-cctools-macho ==="
clang ${CLANG_FLAGS} -O1 -w -c "${SRC}/tests/hello-cctools.c" -o "${BUILD}/hello-cctools.o"
ld64.lld ${LD_FLAGS} -o "${SRC}/tests/hello-cctools-macho" \
    "${BUILD}/hello-cctools.o" "${STAGED_OVERLAY}/usr/lib/libSystem.B.dylib"
chmod 755 "${SRC}/tests/hello-cctools-macho"
file "${SRC}/tests/hello-cctools-macho"

echo "=== sqlite3-real-macho ==="
# SQLite-specific config, matching its own CMakeLists.txt where applicable:
#   SQLITE_THREADSAFE=0        — single-threaded; Darling's pthreads route
#                                through Mach traps to darlingserver, a
#                                separate, not-yet-exercised path (#198).
#   SQLITE_OMIT_LOAD_EXTENSION — no dlopen() of extension .dylibs.
SQLITE_FLAGS="-DSQLITE_THREADSAFE=0 -DSQLITE_OMIT_LOAD_EXTENSION"
clang ${CLANG_FLAGS} ${SQLITE_FLAGS} -O1 -w \
    -c "${SRC}/src/external/sqlite/sqlite3.c" -o "${BUILD}/sqlite3.o"
clang ${CLANG_FLAGS} ${SQLITE_FLAGS} -O1 -w \
    -I"${SRC}/src/external/sqlite" \
    -c "${SRC}/src/external/sqlite/shell.c" -o "${BUILD}/shell.o"
ld64.lld ${LD_FLAGS} -o "${SRC}/tests/sqlite3-real-macho" \
    "${BUILD}/sqlite3.o" "${BUILD}/shell.o" "${STAGED_OVERLAY}/usr/lib/libSystem.B.dylib"
chmod 755 "${SRC}/tests/sqlite3-real-macho"
file "${SRC}/tests/sqlite3-real-macho"

echo "=== hello-objc-macho ==="
clang ${CLANG_FLAGS} -fobjc-runtime=macosx-10.12 -x objective-c -O1 -w \
    -c "${SRC}/tests/hello-objc.m" -o "${BUILD}/hello-objc.o"
ld64.lld ${LD_FLAGS} -o "${SRC}/tests/hello-objc-macho" \
    "${BUILD}/hello-objc.o" "${STAGED_OVERLAY}/usr/lib/libobjc.A.dylib" \
    "${STAGED_OVERLAY}/usr/lib/libSystem.B.dylib"
chmod 755 "${SRC}/tests/hello-objc-macho"
file "${SRC}/tests/hello-objc-macho"

echo "=== hello-cf-macho ==="
# Individual headers, not <CoreFoundation/CoreFoundation.h> — that umbrella
# is a symlink to a file that doesn't exist in the vendored submodule
# checkout (see tests/hello-cf.c's comment).
clang ${CLANG_FLAGS} -F"${SDK_FLAT}/sdk-flat/Frameworks" -O1 -w \
    -c "${SRC}/tests/hello-cf.c" -o "${BUILD}/hello-cf.o"
ld64.lld ${LD_FLAGS} -o "${SRC}/tests/hello-cf-macho" \
    "${BUILD}/hello-cf.o" \
    "${STAGED_OVERLAY}/System/Library/Frameworks/CoreFoundation.framework/Versions/A/CoreFoundation" \
    "${STAGED_OVERLAY}/usr/lib/libobjc.A.dylib" "${STAGED_OVERLAY}/usr/lib/libicucore.A.dylib" \
    "${STAGED_OVERLAY}/usr/lib/libc++.1.dylib" "${STAGED_OVERLAY}/usr/lib/libSystem.B.dylib"
chmod 755 "${SRC}/tests/hello-cf-macho"
file "${SRC}/tests/hello-cf-macho"

echo "=== done ==="
echo "Run with: echo 'select 21*2;' | DARLING_TEST_BINARY=sqlite3-real-macho <launch-dynamic-smoke binary>"
echo "Run with: DARLING_TEST_BINARY=hello-objc-macho <launch-dynamic-smoke binary>"
echo "Run with: DARLING_TEST_BINARY=hello-cf-macho <launch-dynamic-smoke binary>"
