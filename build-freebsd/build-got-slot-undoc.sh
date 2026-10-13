#!/bin/sh
# build-got-slot-undoc.sh — Control #162, step 40.
#
# Build ONLY tests/objc-got-slot-undoc-macho against the staged overlay,
# reusing build-real-macho-tests.sh's staging under ${DARLING_BUILD_DIR}/real-macho.
# Same discipline as build-constdict-desc-probe.sh: it deliberately does NOT
# rerun that script (a rerun rebuilds Foundation and would overwrite the
# image the step 33-39 measurements were taken against).
#
# Environment:
#   DARLING_SRC_DIR    — root of this repository   (default: script/..)
#   DARLING_BUILD_DIR  — scratch build dir          (required)
#   DARLING_OVERLAY    — darling overlay dir        (required)
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
SRC="${DARLING_SRC_DIR:-${SCRIPT_DIR}}"
BUILD="${DARLING_BUILD_DIR:?set DARLING_BUILD_DIR to your build dir}"
OVERLAY="${DARLING_OVERLAY:?set DARLING_OVERLAY to your overlay dir}"

RM="${BUILD}/real-macho"
SDK_FLAT="${RM}/sdk-flat"
STAGED="${RM}/staged-overlay"
if [ ! -d "${SDK_FLAT}/usr/include" ]; then
    mkdir -p "${SDK_FLAT}"
    tar xzf "${SRC}/tests/vendor/macosx-sdk-flat.tar.gz" -C "${SDK_FLAT}"
fi
if [ ! -f "${STAGED}/usr/lib/libSystem.B.dylib" ]; then
    echo "FATAL: ${STAGED}/usr/lib/libSystem.B.dylib missing — run" >&2
    echo "       sh build-freebsd/build-real-macho-tests.sh first" >&2
    exit 1
fi

# CoreFoundationExtras.dylib (the NSConstantDictionary stub class) is not
# part of the sibling script's staging. Copy it in as a REAL file.
if [ ! -f "${STAGED}/usr/lib/CoreFoundationExtras.dylib" ]; then
    cp "${OVERLAY}/usr/lib/CoreFoundationExtras.dylib" "${STAGED}/usr/lib/CoreFoundationExtras.dylib"
fi

PATH="/usr/local/bin:/usr/local/sbin:/usr/bin:/bin:/sbin:/usr/sbin"
export PATH

CLANG_FLAGS="-target x86_64-apple-macos10.12 -nostdinc -D__DARWIN_ONLY_UNIX_CONFORMANCE=1"
CLANG_FLAGS="${CLANG_FLAGS} -I${SDK_FLAT}/usr/include -I${SRC}/tests/vendor/fakesdk"
CLANG_FLAGS="${CLANG_FLAGS} -I${SRC}/src/external/foundation/include"
LD_FLAGS="-arch x86_64 -platform_version macos 10.12 10.12 -syslibroot ${STAGED} -Z"

mkdir -p "${BUILD}/got-slot-undoc"
echo "=== objc-got-slot-undoc-macho ==="
clang ${CLANG_FLAGS} -fblocks -fconstant-cfstrings -fexceptions \
    -fobjc-runtime=macosx-10.12 -DDARLING \
    -F"${SDK_FLAT}/Frameworks" -x objective-c -O1 -w \
    -c "${SRC}/tests/objc-got-slot-undoc.m" -o "${BUILD}/got-slot-undoc/objc-got-slot-undoc.o"

ld64.lld ${LD_FLAGS} -o "${SRC}/tests/objc-got-slot-undoc-macho" \
    "${BUILD}/got-slot-undoc/objc-got-slot-undoc.o" \
    "${STAGED}/usr/lib/CoreFoundationExtras.dylib" \
    "${STAGED}/System/Library/Frameworks/Foundation.framework/Versions/C/Foundation" \
    "${STAGED}/System/Library/Frameworks/CoreFoundation.framework/Versions/A/CoreFoundation" \
    "${STAGED}/usr/lib/libobjc.A.dylib" "${STAGED}/usr/lib/libicucore.A.dylib" \
    "${STAGED}/usr/lib/libc++.1.dylib" "${STAGED}/usr/lib/libSystem.B.dylib"

chmod 755 "${SRC}/tests/objc-got-slot-undoc-macho"
file "${SRC}/tests/objc-got-slot-undoc-macho"
