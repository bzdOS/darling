#!/bin/sh
# build-malloc-size-probe.sh - build tests/src/malloc-size-probe.c into
# tests/malloc-size-probe-macho: malloc_size of small blocks.
#
# Usage: sh build-freebsd/build-malloc-size-probe.sh
#   Env: DARLING_SRC_DIR, DARLING_BUILD_DIR, DARLING_OVERLAY. Run it with
#   DARLING_TEST_BINARY=malloc-size-probe-macho under launch-dynamic (needs root).
set -e
SRC="${DARLING_SRC_DIR:?}"
BD="${DARLING_BUILD_DIR:?}"
OD="${DARLING_OVERLAY:?}"
export PATH="/usr/local/bin:/usr/local/sbin:/usr/bin:/bin:/sbin:/usr/sbin"
OUT="${SRC}/tests/malloc-size-probe-macho"
SDK_FLAT="${BD}/bundle-principal-class-test/sdk-flat"
[ -d "${SDK_FLAT}/usr/include" ] || { echo "FATAL: no flattened SDK at ${SDK_FLAT}" >&2; exit 1; }
BUILD=$(mktemp -d "${BD}/malloc-size-probe-test.XXXXXX") || { echo "FATAL: mktemp workdir failed for malloc-size-probe-test" >&2; exit 1; }
cleanup() { rm -rf "$BUILD"; }
trap cleanup EXIT INT TERM HUP
clang -target x86_64-apple-macos10.12 -nostdinc -D__DARWIN_ONLY_UNIX_CONFORMANCE=1 -O1 -w \
	-I"${SRC}/tests/vendor/fakesdk" -I"${SDK_FLAT}/usr/include" \
	-c "${SRC}/tests/src/malloc-size-probe.c" -o "${BUILD}/probe.o"
ld64.lld -arch x86_64 -platform_version macos 10.12 10.12 \
	-syslibroot "${OD}" -Z -o "${OUT}" "${BUILD}/probe.o" "${OD}/usr/lib/libSystem.B.dylib"
chmod 755 "${OUT}"
echo "binary: ${OUT}"
