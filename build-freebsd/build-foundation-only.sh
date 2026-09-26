#!/bin/sh
# Rebuild only Foundation.dylib (after editing src/external/foundation/src/*)
# and install it into $OVERLAY. Mirrors the Foundation section of
# build-real-macho-tests.sh.
set -e
SCRIPT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
SRC="${DARLING_SRC_DIR:-${SCRIPT_DIR}}"
BUILD="${DARLING_BUILD_DIR:?set DARLING_BUILD_DIR to your build dir}/real-macho"
OVERLAY="${DARLING_OVERLAY:?set DARLING_OVERLAY to your overlay dir}"
STAGED_OVERLAY="${BUILD}/staged-overlay"
SDK_FLAT="${BUILD}/sdk-flat"
FOUND="${SRC}/src/external/foundation"
CF="${SRC}/src/external/corefoundation"

CLANG_FLAGS="-target x86_64-apple-macos10.12 -nostdinc -D__DARWIN_ONLY_UNIX_CONFORMANCE=1"
CLANG_FLAGS="${CLANG_FLAGS} -I${SDK_FLAT}/usr/include -I${SRC}/tests/vendor/fakesdk"
LD_FLAGS="-arch x86_64 -platform_version macos 10.12 10.12 -syslibroot ${STAGED_OVERLAY} -Z"

FOUND_FLAGS="${CLANG_FLAGS} -fblocks -fconstant-cfstrings -fexceptions -fobjc-runtime=macosx-10.12"
FOUND_FLAGS="${FOUND_FLAGS} -include ${CF}/CoreFoundation_Prefix.h -include ${CF}/macros.h"
FOUND_FLAGS="${FOUND_FLAGS} -I${SDK_FLAT}/corefoundation-headers"
FOUND_FLAGS="${FOUND_FLAGS} -I${FOUND}/include -I${FOUND}/include/Foundation -I${FOUND}/src"
FOUND_FLAGS="${FOUND_FLAGS} -DNSBUILDINGFOUNDATION=1 -DINCLUDE_OBJC -DDEPLOYMENT_TARGET_MACOSX=1"
FOUND_FLAGS="${FOUND_FLAGS} -D__CONSTANT_CFSTRINGS__=1 -D__CONSTANT_STRINGS__=1 -DOBJC_OLD_DISPATCH_PROTOTYPES=1"
FOUND_FLAGS="${FOUND_FLAGS} -DPAGE_SIZE=4096 -DDARLING -F${SDK_FLAT}/Frameworks"
FOUND_FLAGS="${FOUND_FLAGS} -Wno-error=implicit-function-declaration -Wno-error=int-conversion"

mkdir -p "${BUILD}/found-build"
found_objs=""
python3 - "${FOUND}/CMakeLists.txt" <<'PYEOF' > "${BUILD}/foundation_sources.txt"
import re, sys
txt = open(sys.argv[1]).read()
m = re.search(r"set\(foundation_sources\n(.*?)\n\)", txt, re.S)
excluded = {"src/NSTask.m", "src/NSNetServices.m"}
seen = set()
for line in m.group(1).splitlines():
	f = line.split("#")[0].strip()
	if f and f not in excluded and f not in seen:
		seen.add(f)
		print(f)
PYEOF

while IFS= read -r rel; do
	[ -z "${rel}" ] && continue
	extra=""
	case "${rel}" in
		*.c) extra="-x objective-c" ;;
	esac
	obj="${BUILD}/found-build/$(echo "${rel}" | tr '/' '_').o"
	clang ${FOUND_FLAGS} ${extra} -w -O0 -c "${FOUND}/${rel}" -o "${obj}"
	found_objs="${found_objs} ${obj}"
done < "${BUILD}/foundation_sources.txt"

mkdir -p "${STAGED_OVERLAY}/System/Library/Frameworks/Foundation.framework/Versions/C"
# shellcheck disable=SC2086
ld64.lld ${LD_FLAGS} -dylib \
	-install_name /System/Library/Frameworks/Foundation.framework/Versions/C/Foundation \
	-o "${STAGED_OVERLAY}/System/Library/Frameworks/Foundation.framework/Versions/C/Foundation" \
	${found_objs} \
	"${STAGED_OVERLAY}/System/Library/Frameworks/CoreFoundation.framework/Versions/A/CoreFoundation" \
	"${STAGED_OVERLAY}/usr/lib/libobjc.A.dylib" "${STAGED_OVERLAY}/usr/lib/libicucore.A.dylib" \
	"${STAGED_OVERLAY}/usr/lib/libc++.1.dylib" "${STAGED_OVERLAY}/usr/lib/libc++abi.dylib" \
	"${STAGED_OVERLAY}/usr/lib/libSystem.B.dylib"

mkdir -p "${OVERLAY}/System/Library/Frameworks/Foundation.framework/Versions/C"
cp "${STAGED_OVERLAY}/System/Library/Frameworks/Foundation.framework/Versions/C/Foundation" \
	"${OVERLAY}/System/Library/Frameworks/Foundation.framework/Versions/C/Foundation"
echo "=== Foundation rebuilt and installed to overlay ==="
file "${OVERLAY}/System/Library/Frameworks/Foundation.framework/Versions/C/Foundation"
