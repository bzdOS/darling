#!/bin/sh
# build-bundle-principal-class-test.sh — build tests/src/bundle-principal-class.m
# into tests/bundle-principal-class-macho, the guest Mach-O that asks -[NSBundle
# principalClass] why it is nil for the window probe's backend.
#
# Usage: sh build-freebsd/build-bundle-principal-class-test.sh
#   Env: DARLING_SRC_DIR, DARLING_BUILD_DIR, DARLING_OVERLAY (same as the
#   other build-freebsd scripts).
#
# Deliberately the same compile and link lines as build-wayland-window-test.sh,
# because the whole point is that this program runs in the same guest, against
# the same overlay Foundation, as the probe it explains. What is different is
# what it does NOT do:
#
#   - no backend declaration gate. That gate exists because the window probe
#     declares the backend's private methods by hand and a wrong signature
#     crashes the guest; this program declares none of them, so there is
#     nothing to check. Skipping it is not a weaker build here.
#   - no Wayland, no seat, no compositor. principalClass is answered long
#     before wl_display_connect is called, so a probe that reached for Wayland
#     could only fail for reasons of its own.
#
# The output goes to $DARLING_SRC_DIR/tests/$OUT_NAME because that is where
# launch-dynamic looks for the test binary (it copies it into the staging dir
# it hands over). Building does not need root; running does.
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
SRC="${DARLING_SRC_DIR:-${SCRIPT_DIR}}"
BD="${DARLING_BUILD_DIR:?set DARLING_BUILD_DIR to your build dir}"
OD="${DARLING_OVERLAY:?set DARLING_OVERLAY to your overlay dir}"

export PATH="/usr/local/bin:/usr/local/sbin:/usr/bin:/bin:/sbin:/usr/sbin"

OUT_NAME="bundle-principal-class-macho"
SRC_TEST="${SRC}/tests/src/bundle-principal-class.m"
OUT="${SRC}/tests/${OUT_NAME}"
BUILD="${BD}/bundle-principal-class-test"
SDK_FLAT="${BUILD}/sdk-flat"

[ -f "${SRC_TEST}" ] || { echo "FATAL: ${SRC_TEST} missing" >&2; exit 1; }

FND="${SRC}/src/external/foundation"
COCOTRON="${SRC}/src/external/cocotron"
CG="${COCOTRON}/CoreGraphics/include"

mkdir -p "${BUILD}"

# The flattened SDK is unpacked once and shared with the other build scripts by
# symlink, so this script does not need a second copy of a ~100 MB tarball's
# worth of headers on disk.
if [ ! -d "${SDK_FLAT}/Frameworks" ]; then
	if [ ! -f "${SRC}/tests/vendor/macosx-sdk-flat.tar.gz" ]; then
		echo "FATAL: ${SRC}/tests/vendor/macosx-sdk-flat.tar.gz missing." >&2
		echo "  See tests/vendor/README.md to regenerate it." >&2
		exit 1
	fi
	if [ -d "${BD}/wayland-window-test/sdk-flat/Frameworks" ]; then
		mkdir -p "$(dirname "${SDK_FLAT}")"
		ln -sfn "${BD}/wayland-window-test/sdk-flat" "${SDK_FLAT}"
	else
		mkdir -p "${SDK_FLAT}"
		tar xzf "${SRC}/tests/vendor/macosx-sdk-flat.tar.gz" -C "${SDK_FLAT}"
	fi
fi
[ -d "${SDK_FLAT}/Frameworks" ] || {
	echo "FATAL: no flattened SDK at ${SDK_FLAT}" >&2
	exit 1
}

echo "=== compiling the bundle probe ==="
# shellcheck disable=SC2086
clang -target x86_64-apple-macos10.12 -nostdinc -D__DARWIN_ONLY_UNIX_CONFORMANCE=1 \
	-fblocks -fconstant-cfstrings -fobjc-runtime=macosx-10.12 -DDARLING \
	-F"${SDK_FLAT}/Frameworks" \
	-I"${SRC}/tests/vendor/fakesdk" \
	-I"${FND}/include" -I"${FND}/include/Foundation" \
	-I"${SDK_FLAT}/usr/include" -I"${SDK_FLAT}/corefoundation-headers" \
	-I"${CG}" -I"${CG}/CoreGraphics" \
	-x objective-c -O1 -w \
	-c "${SRC_TEST}" -o "${BUILD}/bundle-principal-class.o"

echo "=== linking against the overlay ==="
ld64.lld -arch x86_64 -platform_version macos 10.12 10.12 \
	-syslibroot "${OD}" -Z \
	-o "${OUT}" \
	"${BUILD}/bundle-principal-class.o" \
	"${OD}/System/Library/Frameworks/Foundation.framework/Versions/C/Foundation" \
	"${OD}/System/Library/Frameworks/CoreFoundation.framework/Versions/A/CoreFoundation" \
	"${OD}/System/Library/Frameworks/AppKit.framework/Versions/C/AppKit" \
	"${OD}/usr/lib/libobjc.A.dylib" \
	"${OD}/usr/lib/libicucore.A.dylib" \
	"${OD}/usr/lib/libc++.1.dylib" \
	"${OD}/usr/lib/libc++abi.dylib" \
	"${OD}/usr/lib/libSystem.B.dylib"

chmod 755 "${OUT}"

echo "=== done ==="
echo "binary: ${OUT}"
file "${OUT}"
echo
echo "It needs no Wayland seat. The guest run still needs root (the overlay is"
echo "mounted through vchroot by launch-dynamic):"
echo "  DARLING_TEST_BINARY=${OUT_NAME} sh build-freebsd/run-bundle-principal-class.sh"
