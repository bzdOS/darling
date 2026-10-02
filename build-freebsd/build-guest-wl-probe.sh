#!/bin/sh
# Build tests/guest-wl-session-roundtrip-macho end to end, with no manual
# steps: installs the fixed libDER/DERItem.h over the sdk-flat copy (the
# include-order bug the session probe kept paying ~12 min to work around),
# cross-compiles the probe, and links it against the overlay.
#
# Usage: sh build-freebsd/build-guest-wl-probe.sh
# Env:   DARLING_SRC_DIR (default: this script's ..), DARLING_BUILD_DIR
#        (default: the build tree next to the source tree, overridable).
#
# The fix and the reasoning live in the installed header's own comment;
# the short version: DERItem.h claimed _DER_ITEM_H_ before including
# libDER_config.h, which includes oids.h — oids.h then skipped its
# DERItem typedef (guard "already claimed") and used the type anyway.
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
SRC="${DARLING_SRC_DIR:-${SCRIPT_DIR}/..}"
BD="${DARLING_BUILD_DIR:-${SRC}/../build}"
SDK="${BD}/real-macho/sdk-flat"
OD="${DARLING_OVERLAY:-${SRC}/../overlay}"
FIX="${SCRIPT_DIR}/probe-sdk-fix/libDER/DERItem.h"
TARGET="${SRC}/tests/guest-wl-session-roundtrip-macho"
SRC_C="${SRC}/tests/src/guest-wl-session-roundtrip.c"
OBJ=/tmp/guest-wl-session-roundtrip.o

# --- the header fix, idempotent -------------------------------------------
if [ ! -f "${SDK}/usr/include/libDER/DERItem.h" ]; then
	echo "FATAL: ${SDK}/usr/include/libDER/DERItem.h missing — wrong SDK?" >&2
	exit 1
fi
if cmp -s "${FIX}" "${SDK}/usr/include/libDER/DERItem.h"; then
	echo "sdk fix: already installed"
else
	cp "${FIX}" "${SDK}/usr/include/libDER/DERItem.h"
	echo "sdk fix: installed fixed libDER/DERItem.h into ${SDK}"
fi

FND="${SRC}/src/external/foundation"
CG="${SRC}/src/external/cocotron"

echo "=== compile ==="
clang -target x86_64-apple-macos10.12 -nostdinc \
	-D__DARWIN_ONLY_UNIX_CONFORMANCE=1 \
	-fobjc-runtime=macosx-10.12 -x objective-c -O1 -w \
	-I "${SRC}/tests/vendor/fakesdk" \
	-I "${FND}/include" -I "${FND}/include/Foundation" \
	-I "${SDK}/usr/include" \
	-F "${SDK}/Frameworks" \
	-I "${CG}" -I "${CG}/CoreGraphics" \
	-c "${SRC_C}" -o "${OBJ}"

echo "=== link ==="
LINK_LOG=/tmp/guest-wl-link.log
if ! ld64.lld -arch x86_64 -platform_version macos 10.12 10.12 \
	-syslibroot "${OD}" -Z \
	-o "${TARGET}" "${OBJ}" \
	"${OD}/System/Library/Frameworks/AppKit.framework/Versions/C/AppKit" \
	"${OD}/System/Library/Frameworks/Foundation.framework/Versions/C/Foundation" \
	"${OD}/System/Library/Frameworks/CoreFoundation.framework/Versions/A/CoreFoundation" \
	"${OD}/usr/lib/libobjc.A.dylib" \
	"${OD}/usr/lib/libicucore.A.dylib" \
	"${OD}/usr/lib/libc++.1.dylib" \
	"${OD}/usr/lib/libc++abi.dylib" \
	"${OD}/usr/lib/libSystem.B.dylib" > "${LINK_LOG}" 2>&1; then
	echo "FATAL: ld64.lld failed:" >&2
	grep -v 'version 11.0.0' "${LINK_LOG}" >&2 || cat "${LINK_LOG}" >&2
	exit 1
fi
grep -v 'version 11.0.0' "${LINK_LOG}" | grep -v '^$' || true
chmod 755 "${TARGET}"
file "${TARGET}"
echo "OK: ${TARGET}"
