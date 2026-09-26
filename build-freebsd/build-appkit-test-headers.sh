#!/bin/sh
# build-freebsd/build-appkit-test-headers.sh
#
# Reproducibly build tests/hello-appkit.m — a real Mach-O AppKit test program —
# without the fragile /tmp/build-hello-appkit.sh include-path hack.
#
# WHY THIS EXISTS
# ---------------
# tests/vendor/macosx-sdk-flat.tar.gz is a *flattened* (symlink-dereferenced)
# macOS sysroot used as the -I/-F source when cross-compiling Mach-O tests with
# clang -target x86_64-apple-macos10.12 + LLVM's ld64.lld. It is intentionally
# NOT a complete SDK: it ships only CoreFoundation + Security framework headers
# (plus usr/include). There is NO genuine Apple SDK anywhere on the host — the
# only CoreGraphics.framework / *.sdk paths under / are Darling-built dylib
# overlays (e.g. $DARLING_OVERLAY/.../CoreGraphics.framework) and the
# vendored Developer/.../MacOSX.sdk tree, whose framework Headers are symlinks
# pointing BACK into the src/external/cocotron submodule.
#
# So the real, open-source header sources for an AppKit program are:
#   * src/external/cocotron/AppKit/include      (AppKit/AppKit.h + friends)
#   * src/external/cocotron/CoreGraphics/include (CoreGraphics/* umbrella + bits)
#   * src/external/cocotron/CoreText/include     (CoreText/CoreText.h)
#   * src/external/foundation/include            (Foundation/Foundation.h)
#   * tests/vendor/fakesdk                       (stdarg.h, stdbool.h, a minimal
#                                                  CoreGraphics + the HIToolbox
#                                                  kVK_* shim WaylandKeyCodes.h
#                                                  needs, and CFNetwork/AE stubs)
#   * src/freebsd-shims/missing-headers          (replacement IOKit/... headers
#                                                  for broken SDK symlinks)
#
# The previous /tmp/build-hello-appkit.sh put the cocotron CoreGraphics/CoreText
# -I paths BEFORE -I fakesdk (because fakesdk also ships a minimal CoreGraphics
# that conflicts) and force-pre-included a prefix header
# (CoreText/CoreText.h + CGWindowLevel.h + CGError.h). That ordering is real and
# required; this script makes it explicit, deterministic, and committed.
#
# INPUTS (all produced elsewhere — this script does NOT build them):
#   * tests/vendor/macosx-sdk-flat.tar.gz  (from build-freebsd/sync-flat-sdk.sh)
#   * $DARLING_OVERLAY/.../AppKit.framework/Versions/C/AppKit  and its
#     dependency closure (CoreGraphics, Onyx2D, Foundation, CoreFoundation,
#     libobjc, libicucore, libc++, libc++abi, libSystem) — produced by
#     build-freebsd/build-gui.sh (and build-real-macho-tests.sh).
#
# USAGE
#   sh build-freebsd/build-appkit-test-headers.sh
# Environment overrides:
#   DARLING_SRC_DIR   repo root            (default: this script's parent/..)
#   DARLING_OVERLAY   runtime framework root (required)
#   BUILD_DIR         scratch/output dir    (default: <SRC>/build-freebsd/appkit-test)
#   SKIP_LINK=1       only compile (-c), skip ld64.lld link step
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
SRC="${DARLING_SRC_DIR:-${SCRIPT_DIR}}"
OVERLAY="${DARLING_OVERLAY:?set DARLING_OVERLAY to your overlay dir}"
BUILD="${BUILD_DIR:-${SRC}/build-freebsd/appkit-test}"
SDK_FLAT="${BUILD}/sdk-flat"

COCOTRON="${SRC}/src/external/cocotron"
FOUND="${SRC}/src/external/foundation"
FAKESDK="${SRC}/tests/vendor/fakesdk"
MISSING="${SRC}/src/freebsd-shims/missing-headers"
VENDOR_SDK="${SRC}/tests/vendor/macosx-sdk-flat.tar.gz"

CG="${COCOTRON}/CoreGraphics/include"
CT="${COCOTRON}/CoreText/include"

# --- sanity checks -----------------------------------------------------------
for d in "${COCOTRON}/AppKit/include" "${CG}" "${CT}" "${FOUND}/include" "${FAKESDK}" "${MISSING}"; do
	[ -d "$d" ] || { echo "MISSING required source dir: $d" >&2; exit 1; }
done
[ -f "$VENDOR_SDK" ] || { echo "MISSING vendored SDK tarball: $VENDOR_SDK" >&2; exit 1; }

mkdir -p "$BUILD"

# --- unpack the flattened SDK once ------------------------------------------
# The flat SDK ships CoreFoundation.framework + Security.framework headers under
# Frameworks/, and usr/include. It does NOT need the host (symlink-deref works
# fine here on the build host; the virtiofs readlink() break only affects the
# dev VM, see tests/vendor/README.md).
if [ ! -f "$SDK_FLAT/usr/include/stdio.h" ]; then
	echo "=== unpacking vendored flat SDK -> $SDK_FLAT ==="
	mkdir -p "$SDK_FLAT"
	tar xzf "$VENDOR_SDK" -C "$SDK_FLAT"
fi

# --- generate the prefix header (was /tmp/cg-prefix.h) ----------------------
# AppKit's umbrella pulls in CoreGraphics, and the cocotron CoreGraphics needs
# CoreText's umbrella + CGWindowLevel.h/CGError.h materialized first to avoid a
# missing-symbol/forward-decl ordering failure. fakesdk's minimal CoreGraphics
# must NOT win, so cocotron's CG/CT come first in the -I list below.
PREFIX="$BUILD/appkit-prefix.h"
cat > "$PREFIX" <<EOF
#include <CoreText/CoreText.h>
#include "${CG}/CoreGraphics/CGWindowLevel.h"
#include "${CG}/CoreGraphics/CGError.h"
EOF

# --- the minimal correct include set ----------------------------------------
# Order matters:
#   1. SDK_FLAT/usr/include           (system C headers from the flat SDK)
#   2. $CG  (cocotron CoreGraphics)   BEFORE fakesdk so the REAL CoreGraphics wins
#   3. $CT  (cocotron CoreText)
#   4. fakesdk                         (stdarg/stdbool + HIToolbox kVK shim + stubs)
#   5. FOUND/include                   Foundation umbrella
#   6. COCOTRON/AppKit/include         AppKit umbrella
#   7. missing-headers                 replacement IOKit/... headers
# Plus -F SDK_FLAT/Frameworks for <CoreFoundation/CoreFoundation.h> framework form.
CLANG_FLAGS="-target x86_64-apple-macos10.12 -nostdinc -D__DARWIN_ONLY_UNIX_CONFORMANCE=1"
CLANG_FLAGS="$CLANG_FLAGS -I$SDK_FLAT/usr/include -I$CG -I$CT -I$FAKESDK"
APPKIT_FLAGS="$CLANG_FLAGS -fblocks -fconstant-cfstrings -fobjc-runtime=macosx-10.12 -DDARLING"
APPKIT_FLAGS="$APPKIT_FLAGS -F$SDK_FLAT/Frameworks -I$FOUND/include -I$COCOTRON/AppKit/include -I$CT"
APPKIT_FLAGS="$APPKIT_FLAGS -I$MISSING -include $PREFIX -x objective-c -O1 -w"

LD_FLAGS="-arch x86_64 -platform_version macos 10.12 10.12 -syslibroot $OVERLAY -Z"

echo "=== resolved clang -I/-F flags ==="
echo "$APPKIT_FLAGS" | tr ' ' '\n' | grep -E '^-I|^-F' | sed 's/^/  /'
echo "=== resolved ld64.lld flags ==="
echo "  $LD_FLAGS"

# --- compile ----------------------------------------------------------------
echo "=== compile tests/hello-appkit.m ==="
# shellcheck disable=SC2086
clang $APPKIT_FLAGS -c "$SRC/tests/hello-appkit.m" -o "$BUILD/hello-appkit.o"

if [ "${SKIP_LINK:-0}" = "1" ]; then
	echo "SKIP_LINK set: object only at $BUILD/hello-appkit.o"
	exit 0
fi

# --- link -------------------------------------------------------------------
# Verbatim runtime dependency closure from the working /tmp recipe. AppKit.dylib
# and its friends must already exist in OVERLAY (build-gui.sh / build-gui).
for f in \
	"$OVERLAY/System/Library/Frameworks/AppKit.framework/Versions/C/AppKit" \
	"$OVERLAY/System/Library/Frameworks/CoreGraphics.framework/Versions/A/CoreGraphics" \
	"$OVERLAY/System/Library/PrivateFrameworks/Onyx2D.framework/Versions/A/Onyx2D" \
	"$OVERLAY/System/Library/Frameworks/Foundation.framework/Versions/C/Foundation" \
	"$OVERLAY/System/Library/Frameworks/CoreFoundation.framework/Versions/A/CoreFoundation" \
	"$OVERLAY/usr/lib/libobjc.A.dylib" "$OVERLAY/usr/lib/libicucore.A.dylib" \
	"$OVERLAY/usr/lib/libc++.1.dylib" "$OVERLAY/usr/lib/libc++abi.dylib" \
	"$OVERLAY/usr/lib/libSystem.B.dylib" ; do
	[ -f "$f" ] || { echo "MISSING link input (run build-gui.sh first): $f" >&2; exit 1; }
done

echo "=== link -> $SRC/tests/hello-appkit-macho ==="
# shellcheck disable=SC2086
ld64.lld $LD_FLAGS -o "$SRC/tests/hello-appkit-macho" \
	"$BUILD/hello-appkit.o" \
	"$OVERLAY/System/Library/Frameworks/AppKit.framework/Versions/C/AppKit" \
	"$OVERLAY/System/Library/Frameworks/CoreGraphics.framework/Versions/A/CoreGraphics" \
	"$OVERLAY/System/Library/PrivateFrameworks/Onyx2D.framework/Versions/A/Onyx2D" \
	"$OVERLAY/System/Library/Frameworks/Foundation.framework/Versions/C/Foundation" \
	"$OVERLAY/System/Library/Frameworks/CoreFoundation.framework/Versions/A/CoreFoundation" \
	"$OVERLAY/usr/lib/libobjc.A.dylib" "$OVERLAY/usr/lib/libicucore.A.dylib" \
	"$OVERLAY/usr/lib/libc++.1.dylib" "$OVERLAY/usr/lib/libc++abi.dylib" \
	"$OVERLAY/usr/lib/libSystem.B.dylib"

chmod 755 "$SRC/tests/hello-appkit-macho"
file "$SRC/tests/hello-appkit-macho"
echo "OK -> $SRC/tests/hello-appkit-macho"
