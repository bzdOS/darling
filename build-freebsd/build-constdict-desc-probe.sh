#!/bin/sh
# build-constdict-desc-probe.sh — Control #159, step 38.
#
# Build ONLY tests/objc-constdict-desc-macho (the NSConstantDictionary
# `description` marker probe) against the staged overlay, reusing
# build-real-macho-tests.sh's staging under
# ${DARLING_BUILD_DIR}/real-macho. It deliberately does NOT rerun that
# script: rerunning it rebuilds all 234 Foundation sources and overwrites
# the overlay's Foundation.dylib, which the step 33-37 measurements were
# taken against.
#
# Environment (same as the sibling scripts):
#   DARLING_SRC_DIR    — root of this repository   (default: script/..)
#   DARLING_BUILD_DIR  — scratch build dir          (required)
#   DARLING_OVERLAY    — darling overlay dir        (required)
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
SRC="${DARLING_SRC_DIR:-${SCRIPT_DIR}}"
BUILD="${DARLING_BUILD_DIR:?set DARLING_BUILD_DIR to your build dir}"
OVERLAY="${DARLING_OVERLAY:?set DARLING_OVERLAY to your overlay dir}"

# Reuse the sibling script's staging; build it if a fresh checkout never ran it.
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
# part of the sibling script's staging — the probes it builds never needed
# it. Copy it in as a regular file (never cp -a: virtiofs symlinks).
if [ ! -f "${STAGED}/usr/lib/CoreFoundationExtras.dylib" ]; then
    cp "${OVERLAY}/usr/lib/CoreFoundationExtras.dylib" "${STAGED}/usr/lib/CoreFoundationExtras.dylib"
fi

PATH="/usr/local/bin:/usr/local/sbin:/usr/bin:/bin:/sbin:/usr/sbin"
export PATH

CLANG_FLAGS="-target x86_64-apple-macos10.12 -nostdinc -D__DARWIN_ONLY_UNIX_CONFORMANCE=1"
CLANG_FLAGS="${CLANG_FLAGS} -I${SDK_FLAT}/usr/include -I${SRC}/tests/vendor/fakesdk"
# Foundation.h is not in the flattened SDK — the sibling script adds the
# submodule's own include tree (tests/hello-foundation.m builds the same way).
CLANG_FLAGS="${CLANG_FLAGS} -I${SRC}/src/external/foundation/include"
LD_FLAGS="-arch x86_64 -platform_version macos 10.12 10.12 -syslibroot ${STAGED} -Z"

mkdir -p "${BUILD}/constdict-desc"
echo "=== objc-constdict-desc-macho ==="
clang ${CLANG_FLAGS} -fblocks -fconstant-cfstrings -fexceptions \
    -fobjc-runtime=macosx-10.12 -DDARLING \
    -F"${SDK_FLAT}/Frameworks" -x objective-c -O1 -w \
    -c "${SRC}/tests/objc-constdict-desc.m" -o "${BUILD}/constdict-desc/objc-constdict-desc.o"

ld64.lld ${LD_FLAGS} -o "${SRC}/tests/objc-constdict-desc-macho" \
    "${BUILD}/constdict-desc/objc-constdict-desc.o" \
    "${STAGED}/usr/lib/CoreFoundationExtras.dylib" \
    "${STAGED}/System/Library/Frameworks/Foundation.framework/Versions/C/Foundation" \
    "${STAGED}/System/Library/Frameworks/CoreFoundation.framework/Versions/A/CoreFoundation" \
    "${STAGED}/usr/lib/libobjc.A.dylib" "${STAGED}/usr/lib/libicucore.A.dylib" \
    "${STAGED}/usr/lib/libc++.1.dylib" "${STAGED}/usr/lib/libSystem.B.dylib"

chmod 755 "${SRC}/tests/objc-constdict-desc-macho"
file "${SRC}/tests/objc-constdict-desc-macho"
