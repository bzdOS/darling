#!/bin/sh
# build-crash-probe.sh — build tests/crash-probe-macho, a Mach-O that faults
# on purpose so mldr's crash handler can be exercised end to end.
#
# Usage: sh build-freebsd/build-crash-probe.sh
#
# Why this is worth existing: the crash path is worth testing precisely when
# something else is broken, and until now the only way to make mldr's handler
# run was the full window probe — an overlay, 59 images of closure walk, a
# live Wayland seat and a conjure process. This is one small binary that faults
# on the second instruction, so the handler can be checked on its own.
#
# tests/crash-probe-macho is a build product and is NOT committed, for the same
# reason no other tests/*-macho is.
#
# Run it with tests/run-smoke.sh, or through launch-dynamic if the overlay has
# to be staged first:
#   sudo env DARLING_TEST_BINARY=crash-probe-macho ... launch-dynamic
#
# Environment:
#   DARLING_SRC_DIR   — root of this repository (default: this script/..)
#   DARLING_OVERLAY   — overlay holding usr/lib/libSystem.B.dylib (required)
#   DARLING_BUILD_DIR — scratch dir for the .o and the staged link inputs
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT="${DARLING_SRC_DIR:-$(cd "${SCRIPT_DIR}/.." && pwd)}"
OVERLAY="${DARLING_OVERLAY:?set DARLING_OVERLAY to your overlay dir}"
PATH="/usr/local/bin:/usr/local/sbin:/usr/bin:/bin:/sbin:/usr/sbin"
export PATH

OBJDIR="${DARLING_BUILD_DIR:-$(mktemp -d)}/crash-probe"
STAGED="${OBJDIR}/staged-overlay"
mkdir -p "${OBJDIR}" "${STAGED}/usr/lib" "${STAGED}/usr/lib/system"

# The SDK headers come out of the committed flat tarball rather than the SDK
# tree, for the reason build-real-macho-tests.sh gives: virtiofs cannot resolve
# the SDK's symlinks. Unpacked into the scratch dir, never in place.
SDK_FLAT="${OBJDIR}/sdk-flat"
mkdir -p "${SDK_FLAT}"
if [ ! -d "${SDK_FLAT}/usr/include" ]; then
	tar xzf "${ROOT}/tests/vendor/macosx-sdk-flat.tar.gz" -C "${SDK_FLAT}"
fi

# libSystem is linked against, not staged into the repo: the probe calls
# printf and mmap through it, and a Mach-O that cannot even print before it
# faults is a probe that proves less. find|pax, never cp -a — the overlay's
# symlinks do not survive the copy.
cp "${OVERLAY}/usr/lib/libSystem.B.dylib" "${STAGED}/usr/lib/libSystem.B.dylib"
if [ -d "${OVERLAY}/usr/lib/system" ]; then
	(cd "${OVERLAY}/usr/lib/system" && find . -maxdepth 1 -type f | pax -rw "${STAGED}/usr/lib/system")
fi

CLANG_FLAGS="-target x86_64-apple-macos10.12 -nostdinc -w"
CLANG_FLAGS="${CLANG_FLAGS} -I${SDK_FLAT}/usr/include -I${ROOT}/tests/vendor/fakesdk"

echo "=== crash-probe.o ==="
# shellcheck disable=SC2086
clang ${CLANG_FLAGS} -O0 -c "${SCRIPT_DIR}/crash-probe.c" -o "${OBJDIR}/crash-probe.o"

echo "=== crash-probe-macho ==="
ld64.lld -arch x86_64 -platform_version macos 10.12 10.12 \
	-syslibroot "${STAGED}" -Z \
	-o "${ROOT}/tests/crash-probe-macho" \
	"${OBJDIR}/crash-probe.o" "${STAGED}/usr/lib/libSystem.B.dylib"

file "${ROOT}/tests/crash-probe-macho"
ls -la "${ROOT}/tests/crash-probe-macho"
