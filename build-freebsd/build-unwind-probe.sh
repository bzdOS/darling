#!/bin/sh
# build-unwind-probe.sh - build tests/src/unwind-probe.m (+ its C++ half) into
# tests/unwind-probe-macho: what an unimplemented selector returns in the guest,
# and whether ObjC and C++ exceptions unwind to their handlers.
#
# Usage: sh build-freebsd/build-unwind-probe.sh
#   Env: DARLING_SRC_DIR, DARLING_BUILD_DIR, DARLING_OVERLAY (as the other
#   build-freebsd scripts). Run it with
#   DARLING_TEST_BINARY=unwind-probe-macho under launch-dynamic (needs root).
set -e
SRC="${DARLING_SRC_DIR:?}"
BD="${DARLING_BUILD_DIR:?}"
OD="${DARLING_OVERLAY:?}"
export PATH="/usr/local/bin:/usr/local/sbin:/usr/bin:/bin:/sbin:/usr/sbin"
OUT="${SRC}/tests/unwind-probe-macho"
BUILD="${BD}/unwind-probe-test"
SDK_FLAT="${BD}/bundle-principal-class-test/sdk-flat"
FND="${SRC}/src/external/foundation"
CG="${SRC}/src/external/cocotron/CoreGraphics/include"
[ -d "${SDK_FLAT}/Frameworks" ] || { echo "FATAL: no flattened SDK at ${SDK_FLAT}" >&2; exit 1; }
mkdir -p "${BUILD}"
clang -target x86_64-apple-macos10.12 -nostdinc -D__DARWIN_ONLY_UNIX_CONFORMANCE=1 \
	-fblocks -fconstant-cfstrings -fobjc-runtime=macosx-10.12 -DDARLING \
	-F"${SDK_FLAT}/Frameworks" -I"${SRC}/tests/vendor/fakesdk" \
	-I"${FND}/include" -I"${FND}/include/Foundation" \
	-I"${SDK_FLAT}/usr/include" -I"${SDK_FLAT}/corefoundation-headers" \
	-I"${CG}" -I"${CG}/CoreGraphics" \
	-x objective-c -O1 -w -c "${SRC}/tests/src/unwind-probe.m" -o "${BUILD}/unwind-probe.o"
clang++ -target x86_64-apple-macos10.12 -nostdinc -nostdinc++ -std=c++17 -fexceptions -O1 -w \
	-c "${SRC}/tests/src/unwind-probe-cxx.cpp" -o "${BUILD}/unwind-probe-cxx.o"
ld64.lld -arch x86_64 -platform_version macos 10.12 10.12 \
	-syslibroot "${OD}" -Z -o "${OUT}" \
	"${BUILD}/unwind-probe.o" "${BUILD}/unwind-probe-cxx.o" \
	"${OD}/System/Library/Frameworks/Foundation.framework/Versions/C/Foundation" \
	"${OD}/System/Library/Frameworks/CoreFoundation.framework/Versions/A/CoreFoundation" \
	"${OD}/usr/lib/libobjc.A.dylib" "${OD}/usr/lib/libc++.1.dylib" \
	"${OD}/usr/lib/libc++abi.dylib" "${OD}/usr/lib/libSystem.B.dylib"
chmod 755 "${OUT}"
echo "binary: ${OUT}"
