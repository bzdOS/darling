#!/bin/sh
# build-wayland-surface-test.sh — build tests/wayland-surface-commit.m, linking
# the WAYLAND SHIM OBJECTS directly (wayland_shim.o + wayland_ifaces.o +
# wayland_tramp.o from build-wayland-backend.sh's obj dir), so the test can
# drive the guest->sway surface/buffer/commit seam without the AppKit bundle.
#
# Usage: sh build-freebsd/build-wayland-surface-test.sh
#   Env: DARLING_SRC_DIR, DARLING_BUILD_DIR, DARLING_OVERLAY (same as others).
#   Depends on build-wayland-backend.sh having run (for the shim objs).

set -e
SCRIPT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
SRC="${DARLING_SRC_DIR:-${SCRIPT_DIR}}"
BD="${DARLING_BUILD_DIR:?set DARLING_BUILD_DIR to your build dir}"
OD="${DARLING_OVERLAY:?set DARLING_OVERLAY to your overlay dir}"

B="${BD}/real-macho"
SF="${B}/sdk-flat"
SO="${B}/staged-overlay"
FND="${SRC}/src/external/foundation"

WB_OBJ="${BD}/wayland-backend/obj"

export PATH="/usr/local/bin:/usr/local/sbin:/usr/bin:/bin:/sbin:/usr/sbin"

clang -target x86_64-apple-macos10.12 -nostdinc -D__DARWIN_ONLY_UNIX_CONFORMANCE=1 \
  -I"${FND}/include" -I"${SF}/usr/include" -I"${SRC}/tests/vendor/fakesdk" \
  -I"${SRC}/src/startup/mldr/elfcalls" \
  $(pkg-config --cflags wayland-client) \
  -fblocks -fconstant-cfstrings -fobjc-runtime=macosx-10.12 -DDARLING \
  -F"${SF}/Frameworks" -x objective-c -O1 -w \
  -c "${SRC}/tests/wayland-surface-commit.m" -o "${B}/wayland-surface-commit.o"

ld64.lld -arch x86_64 -platform_version macos 10.12 10.12 \
  -syslibroot "${SO}" -Z \
  -o "${SRC}/tests/wayland-surface-commit-macho" \
  "${B}/wayland-surface-commit.o" \
  "${WB_OBJ}/wayland_shim.o" "${WB_OBJ}/wayland_ifaces.o" "${WB_OBJ}/wayland_tramp.o" \
  "${SO}/System/Library/Frameworks/Foundation.framework/Versions/C/Foundation" \
  "${SO}/System/Library/Frameworks/CoreFoundation.framework/Versions/A/CoreFoundation" \
  "${SO}/usr/lib/libobjc.A.dylib" \
  "${SO}/usr/lib/libicucore.A.dylib" \
  "${SO}/usr/lib/libc++.1.dylib" \
  "${SO}/usr/lib/libc++abi.dylib" \
  "${SO}/usr/lib/libSystem.B.dylib"

chmod 755 "${SRC}/tests/wayland-surface-commit-macho"
echo "=== done ==="
file "${SRC}/tests/wayland-surface-commit-macho"
