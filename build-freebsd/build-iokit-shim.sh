#!/bin/sh
# Build the minimal IOKit.framework shim (src/freebsd-shims/iokit_shim.c) --
# NOT the real Apple IOKitUser -- as a guest Mach-O dylib, using the same raw
# clang + ld64.lld against the staged overlay method as
# build-real-macho-tests.sh and build-freebsd/build-gui.sh.
#
# WHY A SHIM, NOT THE REAL IOKitUser: see
# docs/SPEC-iokit-coregraphics-build.md and the header comment in
# src/freebsd-shims/iokit_shim.c. Short version: CoreGraphics.dylib only
# calls 6 IOKit symbols total (kIOMasterPortDefault, IOServiceMatching,
# IOServiceGetMatchingServices, IOIteratorNext, IOObjectRelease,
# IODisplayCreateInfoDictionary -- verified by reading CGDirectDisplay.m,
# not by trusting a prior guess of 4), and the real IOKitUser's ~40 other
# source files talk to a kernel IOKit device registry / iokitd bootstrap
# service that doesn't exist anywhere in this port's stack (src/external/
# iokitd is an uninitialized, empty submodule). This shim answers those 6
# symbols honestly ("no device found") instead.
#
# THIS SCRIPT HAS NEVER BEEN RUN. Written by reading build-real-macho-
# tests.sh, build-gui.sh, and the IOKitUser headers, without a FreeBSD box
# to test on. See docs/SPEC-iokit-coregraphics-build.md §7-8 ("Риски" /
# "Чего НЕ проверено") before trusting any of this.
#
# Usage: sh build-freebsd/build-iokit-shim.sh
# Run on the FreeBSD dev VM (185) -- needs clang and ld64.lld (pkg install
# llvm, same as the other build-freebsd/*.sh scripts), and
# tests/vendor/macosx-sdk-flat.tar.gz (same flat-SDK-headers dependency as
# build-real-macho-tests.sh / build-gui.sh; see tests/vendor/README.md for
# why the SDK is pre-flattened rather than -isysroot'd live off the 9p
# mount: virtiofs can't resolve the SDK's symlinks).
#
# Run this BEFORE build-gui.sh -- build-gui.sh's own staging step only
# copies IOKit.framework into ITS scratch overlay if this script has
# already installed it into $DARLING_OVERLAY first.
#
# Environment:
#   DARLING_BUILD_DIR — scratch build dir        (default: /var/darling-build)
#   DARLING_SRC_DIR    — root of this repository  (default: directory of this script/..)
#   DARLING_OVERLAY    — darling overlay dir      (required)
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
SRC="${DARLING_SRC_DIR:-${SCRIPT_DIR}}"
BUILD="${DARLING_BUILD_DIR:-/var/darling-build}/iokit-shim"
OVERLAY="${DARLING_OVERLAY:?set DARLING_OVERLAY to your overlay dir}"

SDK_FLAT="${BUILD}/sdk-flat"
STAGED_OVERLAY="${BUILD}/staged-overlay"
IOKITUSER="${SRC}/src/external/IOKitUser"

# --- 0. Preflight (same reasoning as build-gui.sh's preflight block). ---
for tool in clang ld64.lld; do
	if ! command -v "${tool}" >/dev/null 2>&1; then
		echo "FATAL: required tool '${tool}' not found in PATH." >&2
		echo "  pkg install llvm (same as the other build-freebsd/*.sh scripts)" >&2
		exit 1
	fi
done

if [ ! -f "${SRC}/tests/vendor/macosx-sdk-flat.tar.gz" ]; then
	echo "FATAL: ${SRC}/tests/vendor/macosx-sdk-flat.tar.gz missing." >&2
	echo "  See tests/vendor/README.md to regenerate it, or run build-real-macho-tests.sh first." >&2
	exit 1
fi

if [ ! -d "${IOKITUSER}/darling/include/IOKit" ]; then
	echo "FATAL: ${IOKITUSER}/darling/include/IOKit missing." >&2
	echo "  src/external/IOKitUser is a git submodule -- run" >&2
	echo "  'git submodule update --init src/external/IOKitUser' first." >&2
	echo "  (Do NOT init src/external/iokitd for this -- it is intentionally" >&2
	echo "  left empty; see docs/SPEC-iokit-coregraphics-build.md §1.1.)" >&2
	exit 1
fi

if [ ! -f "${OVERLAY}/System/Library/Frameworks/CoreFoundation.framework/Versions/A/CoreFoundation" ]; then
	echo "FATAL: no CoreFoundation.dylib in ${OVERLAY}." >&2
	echo "  Run build-freebsd/build-real-macho-tests.sh first -- iokit_shim.c" >&2
	echo "  calls CFDictionaryCreateMutable/CFStringCreateWithCString/CFRelease" >&2
	echo "  and needs the real CoreFoundation.framework to link against." >&2
	exit 1
fi

rm -rf "${BUILD}"
mkdir -p "${SDK_FLAT}" "${STAGED_OVERLAY}/usr/lib/system"

# --- Stage the flat SDK + overlay dylibs, same reasoning as
# --- build-real-macho-tests.sh / build-gui.sh (virtiofs symlink readlink()
# --- is broken -- see tests/vendor/README.md -- so everything below is
# --- real files, never traversed live off the 9p mount). ---
tar xzf "${SRC}/tests/vendor/macosx-sdk-flat.tar.gz" -C "${SDK_FLAT}"

cp "${OVERLAY}/usr/lib/libSystem.B.dylib" "${STAGED_OVERLAY}/usr/lib/libSystem.B.dylib"
(cd "${OVERLAY}/usr/lib/system" && find . -maxdepth 1 -type f | pax -rw "${STAGED_OVERLAY}/usr/lib/system")

mkdir -p "${STAGED_OVERLAY}/System/Library/Frameworks/CoreFoundation.framework/Versions/A"
cp "${OVERLAY}/System/Library/Frameworks/CoreFoundation.framework/Versions/A/CoreFoundation" \
	"${STAGED_OVERLAY}/System/Library/Frameworks/CoreFoundation.framework/Versions/A/CoreFoundation"

# Same -mmacosx-version-min=10.10 as build-gui.sh (CoreGraphics, the only
# consumer of this shim, targets 10.10 -- see build-gui.sh's own comment on
# why mixing minimums across a dependency chain is asking for trouble).
# -F${SDK_FLAT}/Frameworks is required, not optional: IOKitLib.h includes
# <CoreFoundation/CFBase.h> etc. by prefixed path, and the flat
# corefoundation-headers/ directory alone doesn't provide that prefix (see
# build-gui.sh's own comment on this exact trap).
CLANG_FLAGS="-target x86_64-apple-macos10.10 -nostdinc -D__DARWIN_ONLY_UNIX_CONFORMANCE=1"
CLANG_FLAGS="${CLANG_FLAGS} -DOBJC_OLD_DISPATCH_PROTOTYPES=1 -DDARLING"
CLANG_FLAGS="${CLANG_FLAGS} -I${SDK_FLAT}/usr/include -I${SRC}/tests/vendor/fakesdk"
# MUST come before -I.../IOKitUser/darling/include: IOKit/graphics/
# IOGraphicsLib.h there #includes IOKit/graphics/{IOFramebufferShared,
# IOGraphicsInterface}.h, and CGDirectDisplay.m separately #imports
# IOKit/graphics/IOGraphicsTypes.h and IOKit/hidsystem/IOLLEvent.h -- all
# four of those paths are DANGLING symlinks into two uninitialized git
# submodules (IOGraphics, IOHIDFamily) in the darling include tree; this
# repo has no working copy of them anywhere (checked the flat SDK tarball
# too -- not there either). See src/freebsd-shims/missing-headers/README.md
# for the full accounting of what's missing and why these replacements are
# safe (each provides only the identifiers actually referenced by the
# CoreGraphics sources this shim targets, two of the four are empty).
CLANG_FLAGS="${CLANG_FLAGS} -I${SRC}/src/freebsd-shims/missing-headers"
CLANG_FLAGS="${CLANG_FLAGS} -I${IOKITUSER}/darling/include"
CLANG_FLAGS="${CLANG_FLAGS} -F${SDK_FLAT}/Frameworks"
LD_FLAGS="-arch x86_64 -platform_version macos 10.10 10.10 -syslibroot ${STAGED_OVERLAY} -Z"

echo "=== IOKit shim ==="
mkdir -p "${BUILD}/obj"
clang ${CLANG_FLAGS} -w -O0 -c "${SRC}/src/freebsd-shims/iokit_shim.c" \
	-o "${BUILD}/obj/iokit_shim.o"

mkdir -p "${STAGED_OVERLAY}/System/Library/Frameworks/IOKit.framework/Versions/A"
# shellcheck disable=SC2086
ld64.lld ${LD_FLAGS} -dylib \
	-install_name /System/Library/Frameworks/IOKit.framework/Versions/A/IOKit \
	-o "${STAGED_OVERLAY}/System/Library/Frameworks/IOKit.framework/Versions/A/IOKit" \
	"${BUILD}/obj/iokit_shim.o" \
	"${STAGED_OVERLAY}/System/Library/Frameworks/CoreFoundation.framework/Versions/A/CoreFoundation" \
	"${STAGED_OVERLAY}/usr/lib/libSystem.B.dylib"
chmod 755 "${STAGED_OVERLAY}/System/Library/Frameworks/IOKit.framework/Versions/A/IOKit"
file "${STAGED_OVERLAY}/System/Library/Frameworks/IOKit.framework/Versions/A/IOKit"

# Install into the persistent overlay -- same "persistent side effect"
# pattern build-real-macho-tests.sh uses for Foundation.dylib, needed so
# build-gui.sh's own staging step (which copies FROM $OVERLAY, not from
# this script's scratch dir) picks it up on a later, separate run.
mkdir -p "${OVERLAY}/System/Library/Frameworks/IOKit.framework/Versions/A"
cp "${STAGED_OVERLAY}/System/Library/Frameworks/IOKit.framework/Versions/A/IOKit" \
	"${OVERLAY}/System/Library/Frameworks/IOKit.framework/Versions/A/IOKit"

echo "=== done ==="
echo "Installed: ${OVERLAY}/System/Library/Frameworks/IOKit.framework/Versions/A/IOKit"
echo "Next: sh build-freebsd/build-gui.sh (its own staging step will now find this)"
