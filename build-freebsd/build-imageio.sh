#!/bin/sh
# build-imageio.sh - build the real ImageIO.framework (src/frameworks/ImageIO, on top of Onyx2D's O2ImageSource)
# with clang + ld64.lld, the same flags as build-gui.sh. This is the only ImageIO build path; build-gui.sh calls it.
# Usage: DARLING_OVERLAY=... [DARLING_BUILD_DIR=/var/darling-build] [DARLING_SRC_DIR=...] sh build-freebsd/build-imageio.sh [--install]
#   without --install: artifact only, in $DARLING_BUILD_DIR/imageio/staged-overlay/.../ImageIO
#   --install: the artifact is copied into the overlay; the previous image is kept once in
#              $DARLING_BACKUP_DIR (default /opt/darling/backup) as ImageIO.pre-r5
# Does not touch any other build directory.
set -e
SCRIPT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
SRC="${DARLING_SRC_DIR:-${SCRIPT_DIR}}"
BUILD="${DARLING_BUILD_DIR:?set DARLING_BUILD_DIR (darling.toml [env])}/imageio"
OVERLAY="${DARLING_OVERLAY:?set DARLING_OVERLAY to your overlay dir}"
BACKUP_DIR="${DARLING_BACKUP_DIR:-/opt/darling/backup}"
SDK_FLAT="${BUILD}/sdk-flat"
STAGED_OVERLAY="${BUILD}/staged-overlay"
COCOTRON="${SRC}/src/external/cocotron"
IIO="${SRC}/src/frameworks/ImageIO"
FW=System/Library/Frameworks/ImageIO.framework/Versions/A/ImageIO

for tool in clang ld64.lld; do
	command -v "${tool}" >/dev/null 2>&1 || { echo "FATAL: ${tool} not found" >&2; exit 1; }
done
for f in System/Library/Frameworks/Foundation.framework/Versions/C/Foundation \
	System/Library/Frameworks/CoreFoundation.framework/Versions/A/CoreFoundation \
	System/Library/Frameworks/CoreGraphics.framework/Versions/A/CoreGraphics \
	System/Library/PrivateFrameworks/Onyx2D.framework/Versions/A/Onyx2D \
	usr/lib/libobjc.A.dylib usr/lib/libSystem.B.dylib; do
	[ -f "${OVERLAY}/${f}" ] || { echo "FATAL: ${OVERLAY}/${f} missing" >&2; exit 1; }
done

rm -rf "${BUILD}"
mkdir -p "${SDK_FLAT}" "${STAGED_OVERLAY}/usr/lib/system"
tar xzf "${SRC}/tests/vendor/macosx-sdk-flat.tar.gz" -C "${SDK_FLAT}"
for f in usr/lib/libSystem.B.dylib usr/lib/libobjc.A.dylib; do cp "${OVERLAY}/${f}" "${STAGED_OVERLAY}/${f}"; done
ln -sf libobjc.A.dylib "${STAGED_OVERLAY}/usr/lib/libobjc.dylib"
(cd "${OVERLAY}/usr/lib/system" && find . -maxdepth 1 -type f | pax -rw "${STAGED_OVERLAY}/usr/lib/system")
for f in System/Library/Frameworks/Foundation.framework/Versions/C/Foundation \
	System/Library/Frameworks/CoreFoundation.framework/Versions/A/CoreFoundation \
	System/Library/Frameworks/CoreGraphics.framework/Versions/A/CoreGraphics \
	System/Library/PrivateFrameworks/Onyx2D.framework/Versions/A/Onyx2D; do
	mkdir -p "${STAGED_OVERLAY}/$(dirname "${f}")"
	cp "${OVERLAY}/${f}" "${STAGED_OVERLAY}/${f}"
done

# flags exactly as build-gui.sh
CLANG_BASE="-target x86_64-apple-macos10.10 -nostdinc -D__DARWIN_ONLY_UNIX_CONFORMANCE=1"
CLANG_BASE="${CLANG_BASE} -fblocks -fconstant-cfstrings -fobjc-runtime=macosx-10.10"
CLANG_BASE="${CLANG_BASE} -DOBJC_OLD_DISPATCH_PROTOTYPES=1 -DDARLING"
SDK_SYSTEM_INCLUDES="-I${SDK_FLAT}/usr/include"
SDK_INCLUDES="-I${SRC}/tests/vendor/fakesdk"
SDK_INCLUDES="${SDK_INCLUDES} -I${SDK_FLAT}/corefoundation-headers"
SDK_INCLUDES="${SDK_INCLUDES} -I${COCOTRON}/../foundation/include -I${COCOTRON}/../foundation/include/Foundation"
SDK_INCLUDES="${SDK_INCLUDES} -F${SDK_FLAT}/Frameworks"
LD_FLAGS="-arch x86_64 -platform_version macos 10.10 10.10 -syslibroot ${STAGED_OVERLAY} -Z"
IIO_FLAGS="-I${IIO}/include -I${COCOTRON}/Onyx2D/include -I${COCOTRON}/CoreGraphics/include -I${SRC}/src/freebsd-shims/missing-headers"

objs=""
for s in CGImageSource CGImageDestination CGImageProperties; do
	clang ${CLANG_BASE} ${IIO_FLAGS} ${SDK_SYSTEM_INCLUDES} ${SDK_INCLUDES} -w -O0 -c "${IIO}/src/${s}.m" -o "${BUILD}/${s}.o"
	objs="${objs} ${BUILD}/${s}.o"
done
mkdir -p "${STAGED_OVERLAY}/$(dirname "${FW}")"
# shellcheck disable=SC2086
ld64.lld ${LD_FLAGS} -dylib \
	-install_name /${FW} -compatibility_version 65535.255.255 -current_version 65535.255.255 \
	-o "${STAGED_OVERLAY}/${FW}" \
	${objs} \
	"${STAGED_OVERLAY}/System/Library/Frameworks/CoreFoundation.framework/Versions/A/CoreFoundation" \
	"${STAGED_OVERLAY}/System/Library/Frameworks/Foundation.framework/Versions/C/Foundation" \
	"${STAGED_OVERLAY}/System/Library/PrivateFrameworks/Onyx2D.framework/Versions/A/Onyx2D" \
	"${STAGED_OVERLAY}/System/Library/Frameworks/CoreGraphics.framework/Versions/A/CoreGraphics" \
	"${STAGED_OVERLAY}/usr/lib/libobjc.A.dylib" "${STAGED_OVERLAY}/usr/lib/libSystem.B.dylib"
echo "Built: ${STAGED_OVERLAY}/${FW} md5=$(md5 -q "${STAGED_OVERLAY}/${FW}")"

if [ "${1:-}" = "--install" ]; then
	mkdir -p "${BACKUP_DIR}"
	[ -e "${BACKUP_DIR}/ImageIO.pre-r5" ] || cp -p "${OVERLAY}/${FW}" "${BACKUP_DIR}/ImageIO.pre-r5"
	cp "${STAGED_OVERLAY}/${FW}" "${OVERLAY}/${FW}"
	echo "Installed: ${OVERLAY}/${FW}"
fi
