#!/bin/sh
# build-wayland-window-test.sh — build tests/src/wayland-window-create.m into
# tests/wayland-window-create-macho, a guest Mach-O that drives the vendored
# Wayland.backend from a real WaylandDisplay to a real wl_shm buffer.
#
# Usage: sh build-freebsd/build-wayland-window-test.sh
#   Env: DARLING_SRC_DIR, DARLING_BUILD_DIR, DARLING_OVERLAY (same as the
#   other build-freebsd scripts).
#
# The output goes to $DARLING_SRC_DIR/tests/wayland-window-create-macho
# because that is where launch-dynamic looks for it (launch-chrome.sh copies
# $DARLING_SRC_DIR/tests/$TEST_BIN into the staging dir it hands over), so
# after this script runs, the guest run is just:
#
#   WAYLAND_DISPLAY=wayland-1 XDG_RUNTIME_DIR=/tmp/wayland-test \
#   TEST_BIN=wayland-window-create-macho sh build-freebsd/launch-chrome.sh
#
# and that needs root, because launch-dynamic checks getuid()==0 and mounts
# the overlay through vchroot. Building does not.
#
# WHY A GATE AT THE TOP: the probe declares the backend's private methods
# itself, because the backend's sources were never committed and cannot be
# rebuilt (see tests/vendor/wayland-backend/README.md). Those declarations are
# the one thing that silently rots: get one signature wrong and the guest
# crashes in a way that looks like a backend bug. So the declarations are
# checked against the vendored dylib first, and a mismatch stops the build
# rather than producing a binary that cannot work.
#
# It is not enough for the encoding to match, either -- see the comments in
# the checker about the leading `^` and about which int is width.

set -e
SCRIPT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
SRC="${DARLING_SRC_DIR:-${SCRIPT_DIR}}"
BD="${DARLING_BUILD_DIR:?set DARLING_BUILD_DIR to your build dir}"
OD="${DARLING_OVERLAY:?set DARLING_OVERLAY to your overlay dir}"

export PATH="/usr/local/bin:/usr/local/sbin:/usr/bin:/bin:/sbin:/usr/sbin"

SRC_TEST="${SRC}/tests/src/wayland-window-create.m"
OUT="${SRC}/tests/wayland-window-create-macho"
VENDORED_BACKEND="${SRC}/tests/vendor/wayland-backend/Wayland"
BUILD="${BD}/wayland-window-test"
SDK_FLAT="${BUILD}/sdk-flat"

[ -f "${SRC_TEST}" ] || { echo "FATAL: ${SRC_TEST} missing" >&2; exit 1; }
[ -f "${VENDORED_BACKEND}" ] || {
	echo "FATAL: ${VENDORED_BACKEND} missing." >&2
	echo "  That dylib is the only copy of the backend in existence; see" >&2
	echo "  tests/vendor/wayland-backend/README.md." >&2
	exit 1
}

# --- Gate: the probe's declarations must match the backend ---------------
echo "=== checking the probe against the vendored backend ==="
python3 "${SRC}/build-freebsd/check-wayland-window-probe.py" "${VENDORED_BACKEND}" || {
	echo >&2
	echo "FATAL: the probe and the backend disagree -- NOT building a binary" >&2
	echo "       that cannot work. Fix the declarations in ${SRC_TEST} first." >&2
	exit 1
}

# --- The flattened SDK ---------------------------------------------------
# Same trick as the other scripts: unpack the vendored tarball rather than
# using the SDK tree in place, whose symlinks do not resolve off the host.
if [ ! -f "${SRC}/tests/vendor/macosx-sdk-flat.tar.gz" ]; then
	echo "FATAL: ${SRC}/tests/vendor/macosx-sdk-flat.tar.gz missing." >&2
	echo "  See tests/vendor/README.md to regenerate it." >&2
	exit 1
fi
mkdir -p "${SDK_FLAT}"
if [ ! -d "${SDK_FLAT}/Frameworks" ]; then
	echo "=== unpacking the flattened SDK ==="
	tar xzf "${SRC}/tests/vendor/macosx-sdk-flat.tar.gz" -C "${SDK_FLAT}"
fi

FND="${SRC}/src/external/foundation"
COCOTRON="${SRC}/src/external/cocotron"
CG="${COCOTRON}/CoreGraphics/include"

mkdir -p "${BUILD}"

# No AppKit headers here on purpose. AppKit/include/AppKit/NSGraphics.h:21
# pulls <ApplicationServices/ApplicationServices.h>, which is in no vendored
# tree, so including AppKit cannot compile at all in this repo. The probe
# declares the three AppKit classes it uses; Foundation.h still supplies
# NSBundle/NSString/NSArray and CGRect comes from the real CGGeometry.h.
echo "=== compiling the probe ==="
# shellcheck disable=SC2086
clang -target x86_64-apple-macos10.12 -nostdinc -D__DARWIN_ONLY_UNIX_CONFORMANCE=1 \
	-fblocks -fconstant-cfstrings -fobjc-runtime=macosx-10.12 -DDARLING \
	-F"${SDK_FLAT}/Frameworks" \
	-I"${SRC}/tests/vendor/fakesdk" \
	-I"${FND}/include" -I"${FND}/include/Foundation" \
	-I"${SDK_FLAT}/usr/include" -I"${SDK_FLAT}/corefoundation-headers" \
	-I"${CG}" -I"${CG}/CoreGraphics" \
	-x objective-c -O1 -w \
	-c "${SRC_TEST}" -o "${BUILD}/wayland-window-create.o"

# -syslibroot is the overlay, not the sdk-flat: the probe links the real
# guest Foundation/CoreFoundation/AppKit and libSystem, which is what makes it
# a guest program rather than a host one.
echo "=== linking against the overlay ==="
ld64.lld -arch x86_64 -platform_version macos 10.12 10.12 \
	-syslibroot "${OD}" -Z \
	-o "${OUT}" \
	"${BUILD}/wayland-window-create.o" \
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
echo "It loads the backend by its GUEST path, so the overlay must be reachable"
echo "as DYLD_ROOT_PATH at run time. The guest run needs root:"
echo "  WAYLAND_DISPLAY=wayland-1 XDG_RUNTIME_DIR=/tmp/wayland-test \\"
echo "  TEST_BIN=wayland-window-create-macho sh build-freebsd/launch-chrome.sh"
echo
echo "If the shm format negotiation still declines, the seat needs input"
echo "devices; build-freebsd/conjure-wayland-input.c provides them and must"
echo "be RUNNING alongside the guest (its devices exist only while it does)."
