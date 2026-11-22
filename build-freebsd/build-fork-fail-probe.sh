#!/bin/sh
# build-fork-fail-probe.sh - build tests/src/fork-fail-probe.c into tests/fork-fail-probe-macho
# (fork and posix_spawn return when the child cannot be created).
# Usage: sh build-freebsd/build-fork-fail-probe.sh      Env: DARLING_SRC_DIR, DARLING_BUILD_DIR, DARLING_OVERLAY.
set -e
SRC="${DARLING_SRC_DIR:?}"
BD="${DARLING_BUILD_DIR:?}"
OD="${DARLING_OVERLAY:?}"
export PATH="/usr/local/bin:/usr/local/sbin:/usr/bin:/bin:/sbin:/usr/sbin"
OUT="${SRC}/tests/fork-fail-probe-macho"
SDK_FLAT="${BD}/bundle-principal-class-test/sdk-flat"
[ -d "${SDK_FLAT}/usr/include" ] || { echo "FATAL: no flattened SDK at ${SDK_FLAT}" >&2; exit 1; }
BUILD=$(mktemp -d "${BD}/fork-fail-probe-test.XXXXXX") || { echo "FATAL: mktemp workdir failed for fork-fail-probe-test" >&2; exit 1; }
cleanup() { rm -rf "$BUILD"; }
trap cleanup EXIT INT TERM HUP
clang -target x86_64-apple-macos10.12 -nostdinc -D__DARWIN_ONLY_UNIX_CONFORMANCE=1 -O1 -w \
	-I"${SRC}/tests/vendor/fakesdk" -I"${SDK_FLAT}/usr/include" \
	-c "${SRC}/tests/src/fork-fail-probe.c" -o "${BUILD}/probe.o"
ld64.lld -arch x86_64 -platform_version macos 10.12 10.12 \
	-syslibroot "${OD}" -Z -o "${OUT}" "${BUILD}/probe.o" "${OD}/usr/lib/libSystem.B.dylib"
chmod 755 "${OUT}"
echo "binary: ${OUT}"
