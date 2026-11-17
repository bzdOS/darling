#!/bin/sh
# build-coretext-probe.sh - build tests/src/coretext-probe.c twice: tests/coretext-probe-macho (linked against CoreText) and
# tests/coretext-extras-probe-macho (linked against CoreTextExtras.dylib).
# Usage: sh build-freebsd/build-coretext-probe.sh      Env: DARLING_SRC_DIR, DARLING_BUILD_DIR, DARLING_OVERLAY.
set -e
SRC="${DARLING_SRC_DIR:?}"
BD="${DARLING_BUILD_DIR:?}"
OD="${DARLING_OVERLAY:?}"
export PATH="/usr/local/bin:/usr/local/sbin:/usr/bin:/bin:/sbin:/usr/sbin"
BUILD="${BD}/coretext-probe-test"
SDK_FLAT="${BD}/bundle-principal-class-test/sdk-flat"
[ -d "${SDK_FLAT}/usr/include" ] || { echo "FATAL: no flattened SDK at ${SDK_FLAT}" >&2; exit 1; }
mkdir -p "${BUILD}"
clang -target x86_64-apple-macos10.12 -nostdinc -D__DARWIN_ONLY_UNIX_CONFORMANCE=1 -O1 -w \
	-I"${SRC}/tests/vendor/fakesdk" -I"${SDK_FLAT}/usr/include" \
	-c "${SRC}/tests/src/coretext-probe.c" -o "${BUILD}/probe.o"
CF="${OD}/System/Library/Frameworks/CoreFoundation.framework/Versions/A/CoreFoundation"
CT="${OD}/System/Library/Frameworks/CoreText.framework/Versions/A/CoreText"
ld64.lld -arch x86_64 -platform_version macos 10.12 10.12 -syslibroot "${OD}" -Z -o "${SRC}/tests/coretext-probe-macho" \
	"${BUILD}/probe.o" "${CT}" "${CF}" "${OD}/usr/lib/libSystem.B.dylib"
ld64.lld -arch x86_64 -platform_version macos 10.12 10.12 -syslibroot "${OD}" -Z -o "${SRC}/tests/coretext-extras-probe-macho" \
	"${BUILD}/probe.o" "${OD}/usr/lib/CoreTextExtras.dylib" "${CF}" "${OD}/usr/lib/libSystem.B.dylib"
chmod 755 "${SRC}/tests/coretext-probe-macho" "${SRC}/tests/coretext-extras-probe-macho"
echo "binaries: ${SRC}/tests/coretext-probe-macho ${SRC}/tests/coretext-extras-probe-macho"
