#!/bin/sh
# Rebuild only CoreFoundation.dylib (after editing src/external/corefoundation/*)
# and install it into $OVERLAY. Mirrors the CoreFoundation section of the
# Darling CMakeLists (cf_c_sources + cf_sources, -init ___CFInitialize,
# -alias_list, -sectcreate __UNICODE, -reexported_symbols_list).
set -e
SCRIPT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
SRC="${DARLING_SRC_DIR:-${SCRIPT_DIR}}"
BUILD="${DARLING_BUILD_DIR:?set DARLING_BUILD_DIR to your build dir}/real-macho"
OVERLAY="${DARLING_OVERLAY:?set DARLING_OVERLAY to your overlay dir}"
STAGED_OVERLAY="${BUILD}/staged-overlay"
SDK_FLAT="${BUILD}/sdk-flat"
CF="${SRC}/src/external/corefoundation"

CLANG_FLAGS="-target x86_64-apple-macos10.12 -nostdinc -D__DARWIN_ONLY_UNIX_CONFORMANCE=1"
CLANG_FLAGS="${CLANG_FLAGS} -I${SDK_FLAT}/usr/include -I${SRC}/tests/vendor/fakesdk"
LD_FLAGS="-arch x86_64 -platform_version macos 10.12 10.12 -syslibroot ${STAGED_OVERLAY} -Z"

CF_FLAGS="${CLANG_FLAGS} -fblocks -fconstant-cfstrings -fexceptions -fobjc-runtime=macosx-10.12"
CF_FLAGS="${CF_FLAGS} -include ${CF}/CoreFoundation_Prefix.h -include ${CF}/macros.h"
CF_FLAGS="${CF_FLAGS} -I${SDK_FLAT}/corefoundation-headers"
CF_FLAGS="${CF_FLAGS} -I${CF}/include -I${CF}/src -I${SRC}/src/external/foundation/src -I${SRC}/src/external/foundation/include"
CF_FLAGS="${CF_FLAGS} -I${SRC}/src/frameworks/CoreServices/include"
CF_FLAGS="${CF_FLAGS} -F${SDK_FLAT}/Frameworks"
CF_FLAGS="${CF_FLAGS} -include ${SRC}/build-freebsd/cf-stubs/cf-deritem-force.h"
CF_FLAGS="${CF_FLAGS} -DCF_BUILDING_CF -DDEPLOYMENT_TARGET_MACOSX=1 -DU_SHOW_DRAFT_API=1 -DU_SHOW_CPLUSPLUS_API=0"
CF_FLAGS="${CF_FLAGS} -DINCLUDE_OBJC -DDARLING -DDISPATCH_SUPPORT=1 -D__CONSTANT_CFSTRINGS__=1 -D__CONSTANT_STRINGS__=1 -DOBJC_OLD_DISPATCH_PROTOTYPES=1"
CF_FLAGS="${CF_FLAGS} -DCF_PRIVATE="
CF_FLAGS="${CF_FLAGS} -Wno-error=implicit-function-declaration -Wno-error=int-conversion -Wno-error=incompatible-function-pointer-types -w"

mkdir -p "${BUILD}/cf-build"

# Pull the two source lists out of the CMakeLists.
python3 - "${CF}/CMakeLists.txt" <<'PYEOF' > "${BUILD}/cf_sources.txt"
import re, sys
txt = open(sys.argv[1]).read()
def grab(name):
    m = re.search(r"set\(" + name + r"\n(.*?)\n\)", txt, re.S)
    out = []
    for line in m.group(1).splitlines():
        f = line.split("#")[0].strip()
        if f and not f.startswith("${") and f not in out:
            out.append(f)
    return out
c = grab("cf_c_sources")
# cf_sources = cf_c_sources + explicit .m/.S list after it
m = re.search(r"set\(cf_sources\n(.*?)\n\)", txt, re.S)
seen = set(c)
for line in m.group(1).splitlines():
    f = line.split("#")[0].strip()
    if f and not f.startswith("${") and f not in seen:
        seen.add(f)
        c.append(f)
for f in c:
    print(f)
PYEOF
# CFCalendarConstants.c holds the kCFDateFormatter* constant symbols but is
# not listed in cf_c_sources upstream; include it explicitly.
echo "CFCalendarConstants.c" >> "${BUILD}/cf_sources.txt"

cf_objs=""
while IFS= read -r rel; do
    [ -z "${rel}" ] && continue
    case "${rel}" in
        *.S) echo "SKIP asm: ${rel}"; continue ;;
        *)   extra="-x objective-c" ;;
    esac
    obj="${BUILD}/cf-build/$(echo "${rel}" | tr '/' '_').o"
    # Recompile when missing OR when the source is newer (timestamp-aware
    # cache: editing a debug print must not silently link a stale object).
    if [ ! -f "${obj}" ] || [ "${CF}/${rel}" -nt "${obj}" ]; then
        clang ${CF_FLAGS} ${extra} -O0 -c "${CF}/${rel}" -o "${obj}"
    fi
    cf_objs="${cf_objs} ${obj}"
done < "${BUILD}/cf_sources.txt"

# GC no-op stub (objc_isAuto is absent from this runtime).
clang ${CF_FLAGS} -x objective-c -O0 -c "${SRC}/build-freebsd/cf-stubs/cf-gc-stub.c" -o "${BUILD}/cf-build/cf-gc-stub.o"
cf_objs="${cf_objs} ${BUILD}/cf-build/cf-gc-stub.o"

# Forwarding trampoline stubs (replaces skipped .S files).
clang ${CF_FLAGS} -x objective-c -O0 -c "${SRC}/build-freebsd/cf-stubs/cf-forwarding-stub.c" -o "${BUILD}/cf-build/cf-forwarding-stub.o"
cf_objs="${cf_objs} ${BUILD}/cf-build/cf-forwarding-stub.o"

# ___CFConstantStringClassReference: real alias of the NSCFConstantString class
# (see cf-conststr-alias.s); ld64.lld would otherwise demote -alias results.
clang -target x86_64-apple-macos10.12 -c "${SRC}/build-freebsd/cf-stubs/cf-conststr-alias.s" -o "${BUILD}/cf-build/cf-conststr-alias.o"
cf_objs="${cf_objs} ${BUILD}/cf-build/cf-conststr-alias.o"

# NOTE: _OBJC_CLASS_$_NSObject / _OBJC_METACLASS_$_NSObject must NOT be
# defined or exported by CF. CF's NSObject.m only builds categories plus
# zero-size "$ld$add$os10.N$" availability markers; the real root class comes
# from libobjc.A.dylib. Aliasing the marker symbols (cf-nsobject-alias.s) put
# a garbage class in a literal pool and crashed libobjc's readClass.

mkdir -p "${STAGED_OVERLAY}/System/Library/Frameworks/CoreFoundation.framework/Versions/A"
# ld64.lld demotes _OBJC_CLASS_$/__OBJC_METACLASS_$ symbols to private extern
# (and does not implement -alias_list/-keep_private_externs/-reexported_*),
# but Foundation/AppKit expect the public CF classes (e.g. _OBJC_CLASS_$_NSObject)
# to be exported from CoreFoundation. Emit a full exported-symbols list from the
# object files (every symbol that was global there). Because -exported_symbols_list
# makes everything else private, we must list ALL originally-global symbols so the
# only behavioural change is that the demoted ObjC class symbols get forced global.
nm ${BUILD}/cf-build/*.o 2>/dev/null | awk '$2 != "U" && $2 ~ /^[A-Z]$/ {print $3}' | sort -u > "${BUILD}/cf-all-exports.txt"
# clang moves the NSObject class definition under the versioned
# $ld$add$os10.N$_OBJC_CLASS_$_NSObject symbol and leaves the canonical base
# name undefined; we alias it (cf-nsobject-alias.s) but ld64.lld still demotes
# _OBJC_CLASS_$ aliases to private. Force the base names global via the export
# list (which makes listed symbols global without restricting the others).
printf '_OBJC_CLASS_$___NSCFConstantString\n_OBJC_METACLASS_$___NSCFConstantString\n___CFConstantStringClassReference\n_OBJC_CLASS_$_NSObject\n_OBJC_METACLASS_$_NSObject\n' >> "${BUILD}/cf-all-exports.txt"
ld64.lld ${LD_FLAGS} -dylib \
    -install_name /System/Library/Frameworks/CoreFoundation.framework/Versions/A/CoreFoundation \
    -compatibility_version 150.0.0 -current_version 255.0.0 \
    -init ___CFInitialize \
    -alias_list "${CF}/SymbolAliases" \
    -sectcreate __UNICODE __csbitmaps "${CF}/CFCharacterSetBitmaps.bitmap" \
    -sectcreate __UNICODE __properties "${CF}/CFUniCharPropertyDatabase.data" \
    -sectcreate __UNICODE __data "${CF}/CFUnicodeData-L.mapping" \
    -reexported_symbols_list "${CF}/reexport_x86_64.exp" \
    -o "${STAGED_OVERLAY}/System/Library/Frameworks/CoreFoundation.framework/Versions/A/CoreFoundation" \
    ${cf_objs} \
    "${STAGED_OVERLAY}/usr/lib/libobjc.A.dylib" \
    "${STAGED_OVERLAY}/usr/lib/libicucore.A.dylib" \
    "${STAGED_OVERLAY}/usr/lib/system/libdispatch.dylib" \
    "${STAGED_OVERLAY}/usr/lib/libc++.1.dylib" "${STAGED_OVERLAY}/usr/lib/libc++abi.dylib" \
    "${STAGED_OVERLAY}/usr/lib/libSystem.B.dylib" \
    "${STAGED_OVERLAY}/usr/lib/system/libsystem_pthread.dylib" \
    "${STAGED_OVERLAY}/usr/lib/system/libsystem_malloc.dylib" \
    "${STAGED_OVERLAY}/usr/lib/system/libsystem_c.dylib"

mkdir -p "${OVERLAY}/System/Library/Frameworks/CoreFoundation.framework/Versions/A"
cp "${STAGED_OVERLAY}/System/Library/Frameworks/CoreFoundation.framework/Versions/A/CoreFoundation" \
   "${OVERLAY}/System/Library/Frameworks/CoreFoundation.framework/Versions/A/CoreFoundation"
echo "=== CoreFoundation rebuilt and installed to overlay ==="
file "${OVERLAY}/System/Library/Frameworks/CoreFoundation.framework/Versions/A/CoreFoundation"
