#!/bin/sh
# Build CoreData.framework and (most of) CoreServices.framework as guest
# Mach-O dylibs, following the same raw-clang + ld64.lld pattern as
# build-freebsd/build-real-macho-tests.sh (Foundation) and
# build-freebsd/build-gui.sh (Onyx2D/CoreGraphics/AppKit): extract source
# lists from CMakeLists.txt, compile each file with
# `clang -target x86_64-apple-macos...`, link with `ld64.lld -syslibroot`.
#
# THIS SCRIPT HAS NEVER BEEN RUN. Written by reading CMakeLists.txt and the
# existing build-freebsd/*.sh scripts only, without a FreeBSD box to test on.
# Per this repo's CLAUDE.md ("не собирать половину молча"), it FATALs loudly
# at the first missing dependency instead of limping on to a half-linked
# dylib -- see docs/SPEC-coredata-coreservices-build.md for the full
# reasoning, including the two subframeworks this script deliberately does
# NOT build (FSEvents, LaunchServices -- see "Deliberate deviations" there).
#
# What it builds, in order:
#   1. CoreData.framework       -- single dylib, cocotron submodule, ObjC.
#   2. CoreServices' subframeworks, each its own small dylib nested under
#      CoreServices.framework/Versions/A/Frameworks/<Sub>.framework/:
#        AE, DictionaryServices, OSServices, SearchKit, SharedFileList
#          (trivial -- 1-2 files each, CoreFoundation + system only)
#        Metadata   (2 files, additionally needs libdispatch.dylib)
#        CarbonCore (28 files, C++, additionally needs real ICU C++ headers
#                    AND libicucore.A.dylib actually exporting ICU's C++
#                    class symbols -- HIGHEST RISK ITEM, see spec doc)
#      SKIPPED (loudly, not silently): FSEvents (calls Linux-only
#      fanotify_init/fanotify_mark/inotify_init1 -- no FreeBSD equivalent in
#      this port's libSystem) and LaunchServices (needs xpc/xpc.h +
#      xpc/private.h + launch_priv.h -- XPC is not implemented in this port).
#   3. CoreServices.framework itself -- the umbrella `constants.m`, linked
#      with `-reexport_library` for each subframework actually built in step
#      2 (NOT FSEvents/LaunchServices/CFNetwork -- CFNetwork is also skipped,
#      see spec doc) plus `-reexported_symbols_list` from the checked-in
#      reexport.exp (a real ld64 feature, never exercised elsewhere in this
#      repo's build scripts -- see spec doc risk list).
#
# Usage: sh build-freebsd/build-coredata-coreservices.sh
# Run on the FreeBSD dev VM (185) -- never tested there (see above).
#
# Environment:
#   DARLING_BUILD_DIR — scratch build dir        (default: /var/darling-build)
#   DARLING_SRC_DIR    — root of this repository  (default: directory of this script/..)
#   DARLING_OVERLAY    — darling overlay dir      (required)
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
SRC="${DARLING_SRC_DIR:-${SCRIPT_DIR}}"
BUILD="${DARLING_BUILD_DIR:-/var/darling-build}/coredata-coreservices"
OVERLAY="${DARLING_OVERLAY:?set DARLING_OVERLAY to your overlay dir}"

SDK_FLAT="${BUILD}/sdk-flat"
STAGED_OVERLAY="${BUILD}/staged-overlay"
COCOTRON="${SRC}/src/external/cocotron"
CS="${SRC}/src/frameworks/CoreServices"

# --- 0. Preflight. Same reasoning as build-gui.sh: this script does a lot of
# --- setup before the first compiler invocation, so check the basics first. ---
for tool in clang ld64.lld python3; do
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

if [ ! -f "${OVERLAY}/System/Library/Frameworks/Foundation.framework/Versions/C/Foundation" ]; then
	echo "FATAL: no Foundation.dylib in ${OVERLAY}." >&2
	echo "  Run build-freebsd/build-real-macho-tests.sh first -- CoreData and CoreServices" >&2
	echo "  both #include Foundation/Foundation.h and link against it." >&2
	exit 1
fi

rm -rf "${BUILD}"
mkdir -p "${SDK_FLAT}" "${STAGED_OVERLAY}/usr/lib/system"

# --- Stage the flat SDK + overlay dylibs (identical reasoning to
# --- build-real-macho-tests.sh / build-gui.sh: virtiofs can't resolve the
# --- SDK's/overlay's symlinks, so everything below is real regular files). ---
tar xzf "${SRC}/tests/vendor/macosx-sdk-flat.tar.gz" -C "${SDK_FLAT}"

cp "${OVERLAY}/usr/lib/libSystem.B.dylib" "${STAGED_OVERLAY}/usr/lib/libSystem.B.dylib"
cp "${OVERLAY}/usr/lib/libobjc.A.dylib" "${STAGED_OVERLAY}/usr/lib/libobjc.A.dylib"
ln -sf libobjc.A.dylib "${STAGED_OVERLAY}/usr/lib/libobjc.dylib"
cp "${OVERLAY}/usr/lib/libicucore.A.dylib" "${STAGED_OVERLAY}/usr/lib/libicucore.A.dylib"
cp "${OVERLAY}/usr/lib/libc++.1.dylib" "${STAGED_OVERLAY}/usr/lib/libc++.1.dylib"
cp "${OVERLAY}/usr/lib/libc++abi.dylib" "${STAGED_OVERLAY}/usr/lib/libc++abi.dylib"
(cd "${OVERLAY}/usr/lib/system" && find . -maxdepth 1 -type f | pax -rw "${STAGED_OVERLAY}/usr/lib/system")

mkdir -p "${STAGED_OVERLAY}/System/Library/Frameworks/CoreFoundation.framework/Versions/A"
cp "${OVERLAY}/System/Library/Frameworks/CoreFoundation.framework/Versions/A/CoreFoundation" \
	"${STAGED_OVERLAY}/System/Library/Frameworks/CoreFoundation.framework/Versions/A/CoreFoundation"
mkdir -p "${STAGED_OVERLAY}/System/Library/Frameworks/Foundation.framework/Versions/C"
cp "${OVERLAY}/System/Library/Frameworks/Foundation.framework/Versions/C/Foundation" \
	"${STAGED_OVERLAY}/System/Library/Frameworks/Foundation.framework/Versions/C/Foundation"

# libdispatch.dylib -- only Metadata/MDItem.c (of the pieces this script
# actually builds) calls dispatch_once(); everything else in scope here is
# dispatch-free. Not fatal at staging time -- fatal later, right before the
# Metadata compile, so the rest of the script (CoreData, the other 4 trivial
# subframeworks, CarbonCore) can still be attempted if libdispatch is absent.
if [ -f "${OVERLAY}/usr/lib/system/libdispatch.dylib" ]; then
	cp "${OVERLAY}/usr/lib/system/libdispatch.dylib" "${STAGED_OVERLAY}/usr/lib/system/libdispatch.dylib"
fi

# Common clang flags. -target x86_64-apple-macos10.12 (NOT the 10.10 that
# CoreData/CMakeLists.txt and CoreServices' own CMakeLists.txt individually
# request) -- per this task's explicit instruction: Foundation.dylib already
# in the overlay was built at 10.12 (build-real-macho-tests.sh), and mixing
# -mmacosx-version-min across one link line (CoreData/CoreServices linking
# against that Foundation) is exactly the kind of trouble build-gui.sh's own
# comment warns about. 10.12 throughout is a deliberate deviation from both
# frameworks' own CMakeLists.txt, not an oversight.
CLANG_FLAGS="-target x86_64-apple-macos10.12 -nostdinc -D__DARWIN_ONLY_UNIX_CONFORMANCE=1"
CLANG_FLAGS="${CLANG_FLAGS} -fblocks -fconstant-cfstrings -fobjc-runtime=macosx-10.12"
CLANG_FLAGS="${CLANG_FLAGS} -DOBJC_OLD_DISPATCH_PROTOTYPES=1 -DDARLING"
CLANG_FLAGS="${CLANG_FLAGS} -I${SDK_FLAT}/usr/include -I${SRC}/tests/vendor/fakesdk"
CLANG_FLAGS="${CLANG_FLAGS} -I${SDK_FLAT}/corefoundation-headers"
CLANG_FLAGS="${CLANG_FLAGS} -I${SRC}/src/external/foundation/include -I${SRC}/src/external/foundation/include/Foundation"
# -F, not just -I: <CoreFoundation/CoreFoundation.h> (the umbrella header, used
# by CoreData's forced -include and several CoreServices .cpp files) only
# resolves via a framework search path into the SDK's real
# Frameworks/CoreFoundation.framework/Headers/ tree -- see build-gui.sh's own
# comment on this exact trap, and docs/SPEC-remaining-frameworks.md's warning
# that this is the single most-hit early failure mode in this build style.
CLANG_FLAGS="${CLANG_FLAGS} -F${SDK_FLAT}/Frameworks"
LD_FLAGS="-arch x86_64 -platform_version macos 10.12 10.12 -syslibroot ${STAGED_OVERLAY} -Z"

# Extract a CMake `set(<varname> ...)` list of source files (copied verbatim
# from build-gui.sh -- see its own comment for the NSTreeNode.m dedup
# rationale). Only usable for CMakeLists.txt that actually declare a
# `set(<Name>_sources ...)` variable -- CarbonCore is the only target in this
# script that does; the umbrella and the small CoreServices subframeworks
# list their sources inline inside the add_framework(...) call itself (see
# docs/SPEC-coredata-coreservices-build.md's note on this), so those are
# hardcoded below instead of extracted.
extract_sources() {
	varname="$1"
	cmakelists="$2"
	python3 - "${cmakelists}" "${varname}" <<'PYEOF'
import re, sys
txt = open(sys.argv[1]).read()
varname = sys.argv[2]
m = re.search(r"set\(" + re.escape(varname) + r"\n(.*?)\n\)", txt, re.S)
if not m:
	sys.exit("FATAL: could not find set(%s ...) in %s" % (varname, sys.argv[1]))
seen = set()
for line in m.group(1).splitlines():
	f = line.split("#")[0].strip()
	if f and f not in seen:
		seen.add(f)
		print(f)
PYEOF
}

# compile_list NAME COMP_DIR EXTRA_FLAGS file1 file2 ... -- compiles each
# file (relative to COMP_DIR) into COMP_DIR-independent objects under
# ${BUILD}/<NAME>-build/, printing the resulting .o paths on stdout (one
# `echo` at the end, space-separated, matching build-gui.sh's
# compile_component() calling convention).
compile_list() {
	comp_name="$1"; comp_dir="$2"; extra_flags="$3"; shift 3
	obj_dir="${BUILD}/${comp_name}-build"
	mkdir -p "${obj_dir}"
	objs=""
	for rel in "$@"; do
		obj="${obj_dir}/$(echo "${rel}" | tr '/' '_').o"
		case "${rel}" in
			*.cpp) lang="-x objective-c++" ;;
			*.m)   lang="-x objective-c" ;;
			*.c)   lang="-x objective-c" ;;  # matches build-real-macho-tests.sh's
			                                  # convention for Foundation's .c files
			*)     lang="" ;;
		esac
		# shellcheck disable=SC2086
		clang ${CLANG_FLAGS} ${extra_flags} ${lang} -w -O0 -c "${comp_dir}/${rel}" -o "${obj}"
		objs="${objs} ${obj}"
	done
	echo "${objs}"
}

require_dylib() {
	label="$1"; path="$2"
	if [ ! -f "${path}" ]; then
		echo "FATAL: ${label} needs ${path}, which does not exist." >&2
		exit 1
	fi
}

echo "=== [1/3] CoreData ==="
CD="${COCOTRON}/CoreData"
CD_FLAGS="-I${CD}/include -Wno-nonportable-include-path -Wno-deprecated-objc-isa-usage"
CD_FLAGS="${CD_FLAGS} -include math.h -include stdlib.h"
CD_FLAGS="${CD_FLAGS} -include CoreFoundation/CoreFoundation.h -include Foundation/Foundation.h"
cd_sources=$(extract_sources CoreData_sources "${CD}/CMakeLists.txt")
if [ -z "${cd_sources}" ]; then
	echo "FATAL: CoreData_sources extracted empty from ${CD}/CMakeLists.txt -- regex needs updating." >&2
	exit 1
fi
# shellcheck disable=SC2046
cd_objs=$(compile_list CoreData "${CD}" "${CD_FLAGS}" $(echo "${cd_sources}"))
mkdir -p "${STAGED_OVERLAY}/System/Library/Frameworks/CoreData.framework/Versions/A"
# shellcheck disable=SC2086
ld64.lld ${LD_FLAGS} -dylib \
	-install_name /System/Library/Frameworks/CoreData.framework/Versions/A/CoreData \
	-o "${STAGED_OVERLAY}/System/Library/Frameworks/CoreData.framework/Versions/A/CoreData" \
	${cd_objs} \
	"${STAGED_OVERLAY}/System/Library/Frameworks/CoreFoundation.framework/Versions/A/CoreFoundation" \
	"${STAGED_OVERLAY}/System/Library/Frameworks/Foundation.framework/Versions/C/Foundation" \
	"${STAGED_OVERLAY}/usr/lib/libobjc.A.dylib" "${STAGED_OVERLAY}/usr/lib/libSystem.B.dylib"
mkdir -p "${OVERLAY}/System/Library/Frameworks/CoreData.framework/Versions/A"
cp "${STAGED_OVERLAY}/System/Library/Frameworks/CoreData.framework/Versions/A/CoreData" \
	"${OVERLAY}/System/Library/Frameworks/CoreData.framework/Versions/A/CoreData"
echo "Built: ${STAGED_OVERLAY}/System/Library/Frameworks/CoreData.framework/Versions/A/CoreData"

echo "=== [2/3] CoreServices subframeworks ==="
CS_FRAMEWORKS="${STAGED_OVERLAY}/System/Library/Frameworks/CoreServices.framework/Versions/A/Frameworks"
CS_FLAGS="-I${CS}/include"

# build_sub NAME SRC_SUBDIR file1 file2 ... -- compiles + links one
# CoreServices subframework into
# CoreServices.framework/Versions/A/Frameworks/<NAME>.framework/Versions/A/<NAME>,
# the layout darling_framework.cmake's add_framework(... PARENT "CoreServices")
# uses (see cmake/darling_framework.cmake's root_dir computation). Prints the
# built dylib's path on success -- callers collect these for the umbrella's
# -reexport_library list.
build_sub() {
	name="$1"; subdir="$2"; extra_flags="$3"; shift 3
	dir="${CS}/src/${subdir}"
	objs=$(compile_list "${name}" "${dir}" "${CS_FLAGS} ${extra_flags}" "$@")
	out="${CS_FRAMEWORKS}/${name}.framework/Versions/A/${name}"
	mkdir -p "$(dirname "${out}")"
	# shellcheck disable=SC2086
	ld64.lld ${LD_FLAGS} -dylib \
		-install_name "/System/Library/Frameworks/CoreServices.framework/Versions/A/Frameworks/${name}.framework/Versions/A/${name}" \
		-o "${out}" \
		${objs} \
		"${STAGED_OVERLAY}/System/Library/Frameworks/CoreFoundation.framework/Versions/A/CoreFoundation" \
		"${STAGED_OVERLAY}/usr/lib/libobjc.A.dylib" "${STAGED_OVERLAY}/usr/lib/libSystem.B.dylib" \
		${SUB_EXTRA_LIBS:-}
	echo "Built: ${out}" >&2
	echo "${out}"
}

echo "--- AE (empty.c + stub.c, CoreFoundation+system only) ---"
SUB_EXTRA_LIBS="" ae_out=$(build_sub AE AE "" empty.c stub.c)

echo "--- DictionaryServices (empty.c, CoreFoundation+system only) ---"
SUB_EXTRA_LIBS="" ds_out=$(build_sub DictionaryServices DictionaryServices "" empty.c)

echo "--- OSServices (empty.c, CoreFoundation+system only) ---"
SUB_EXTRA_LIBS="" os_out=$(build_sub OSServices OSServices "" empty.c)

echo "--- SearchKit (SKAnalysis.c + SKIndex.c, CoreFoundation+system only) ---"
SUB_EXTRA_LIBS="" sk_out=$(build_sub SearchKit SearchKit "" SKAnalysis.c SKIndex.c)

echo "--- SharedFileList (constants.c, CoreFoundation+system only) ---"
SUB_EXTRA_LIBS="" sfl_out=$(build_sub SharedFileList SharedFileList "" constants.c)

echo "--- Metadata (MDQuery.c + MDItem.c -- needs libdispatch.dylib) ---"
require_dylib "Metadata" "${STAGED_OVERLAY}/usr/lib/system/libdispatch.dylib"
SUB_EXTRA_LIBS="${STAGED_OVERLAY}/usr/lib/system/libdispatch.dylib" \
	md_out=$(build_sub Metadata Metadata "" MDQuery.c MDItem.c)

echo "--- CarbonCore (28 files, C++ -- HIGHEST RISK: real ICU C++ headers +"
echo "    libicucore.A.dylib must actually export ICU's C++ class symbols,"
echo "    never verified anywhere in this repo -- see spec doc §Risks) ---"
# All other targets in this script (and in build-gui.sh / build-real-macho-
# tests.sh before it) are C/ObjC only. CarbonCore is the FIRST target in this
# repo's raw-clang build scripts to need the real C++ standard library
# (<vector>, <map>, <mutex>, ...) -- and CLANG_FLAGS' `-nostdinc` (needed to
# force every other header through the staged SDK/overlay instead of the
# build host's own /usr/include) also strips clang's normal libc++ resource-
# dir search path as a side effect. Verified locally (Ubuntu clang 18, not
# the FreeBSD box): `clang -nostdinc -stdlib=libc++ -x c++ -E` on a file with
# just `#include <vector>` fails with "'vector' file not found" -- i.e. this
# is a real, reproduced failure mode of the -nostdinc approach, not a
# theoretical one. `-stdlib=libc++` alone does not re-add the path once
# -nostdinc has removed it.
# Fix: search the FreeBSD box's own installed libc++ headers at run time
# (their exact path depends on which llvm/clang pkg version is installed)
# and -isystem them explicitly, rather than hardcoding a path this script's
# author has never seen on a real FreeBSD 15.1 + pkg install llvm system.
CXX_V1="$(find /usr/local -maxdepth 6 -type d -path '*c++/v1' 2>/dev/null | head -1)"
if [ -z "${CXX_V1}" ]; then
	echo "FATAL: could not find a c++/v1 (libc++ headers) directory under /usr/local." >&2
	echo "  CarbonCore needs <vector>/<map>/<mutex>/... and CLANG_FLAGS' -nostdinc" >&2
	echo "  strips clang's normal libc++ search path (verified locally -- see this" >&2
	echo "  script's comment above this check). Find the real path with:" >&2
	echo "    find /usr/local -type d -path '*c++/v1'" >&2
	echo "  then either install it (pkg install llvm claims to ship libc++) or hardcode" >&2
	echo "  the path this FATAL couldn't find automatically." >&2
	exit 1
fi
CC_DIR="${CS}/src/CarbonCore"
CC_FLAGS="-std=c++17 -stdlib=libc++ -isystem ${CXX_V1}"
CC_FLAGS="${CC_FLAGS} -I${SRC}/src/external/icu/icuSources/common -I${SRC}/src/external/icu/icuSources/i18n"
CC_FLAGS="${CC_FLAGS} -I${SRC}/src/external/xnu/darling/src/libsystem_kernel/emulation/linux"
# Self-test before committing to all 28 files: fail fast and clearly on a
# trivial `#include <vector>` rather than deep into CarbonCore's largest
# files with a confusing error (per this repo's CLAUDE.md: fail loud, not
# half-built).
echo '#include <vector>' > "${BUILD}/cxx-stdlib-probe.cpp"
mkdir -p "${BUILD}/CarbonCore-build"
# shellcheck disable=SC2086
if ! clang ${CLANG_FLAGS} ${CS_FLAGS} ${CC_FLAGS} -x objective-c++ -w -O0 -fsyntax-only "${BUILD}/cxx-stdlib-probe.cpp" 2>"${BUILD}/cxx-stdlib-probe.log"; then
	echo "FATAL: libc++ headers found at ${CXX_V1} but '#include <vector>' still fails:" >&2
	cat "${BUILD}/cxx-stdlib-probe.log" >&2
	exit 1
fi
cc_sources=$(extract_sources CarbonCore_SRCS "${CC_DIR}/CMakeLists.txt")
if [ -z "${cc_sources}" ]; then
	echo "FATAL: CarbonCore_SRCS extracted empty from ${CC_DIR}/CMakeLists.txt -- regex needs updating." >&2
	exit 1
fi
# shellcheck disable=SC2046
cc_objs=$(compile_list CarbonCore "${CC_DIR}" "${CS_FLAGS} ${CC_FLAGS}" $(echo "${cc_sources}"))
cc_out="${CS_FRAMEWORKS}/CarbonCore.framework/Versions/A/CarbonCore"
mkdir -p "$(dirname "${cc_out}")"
# shellcheck disable=SC2086
ld64.lld ${LD_FLAGS} -dylib \
	-install_name /System/Library/Frameworks/CoreServices.framework/Versions/A/Frameworks/CarbonCore.framework/Versions/A/CarbonCore \
	-o "${cc_out}" \
	${cc_objs} \
	"${STAGED_OVERLAY}/System/Library/Frameworks/CoreFoundation.framework/Versions/A/CoreFoundation" \
	"${STAGED_OVERLAY}/System/Library/Frameworks/Foundation.framework/Versions/C/Foundation" \
	"${STAGED_OVERLAY}/usr/lib/libobjc.A.dylib" "${STAGED_OVERLAY}/usr/lib/libicucore.A.dylib" \
	"${STAGED_OVERLAY}/usr/lib/libc++.1.dylib" "${STAGED_OVERLAY}/usr/lib/libc++abi.dylib" \
	"${STAGED_OVERLAY}/usr/lib/libSystem.B.dylib"
echo "Built: ${cc_out}"

echo ""
echo "=== DELIBERATELY SKIPPED (not attempted, not silently dropped) ==="
echo "  FSEvents:       FSEventsImpl.m calls inotify_init1/inotify_add_watch," >&2
echo "                  fseventsd.m calls fanotify_init/fanotify_mark -- real" >&2
echo "                  Linux syscalls with no FreeBSD equivalent surfaced" >&2
echo "                  anywhere in this port's libSystem. Needs a kqueue-based" >&2
echo "                  reimplementation -- a development task, not a build step." >&2
echo "  LaunchServices: LSRunning.m needs xpc/xpc.h + xpc/private.h; LaunchServices.c" >&2
echo "                  needs launch_priv.h -- XPC is not implemented in this port." >&2
echo "  CFNetwork:      out of scope for this task (own external submodule, own" >&2
echo "                  CMakeLists.txt, not analyzed here); CoreServices' umbrella" >&2
echo "                  reexports it in the real CMakeLists.txt but that reexport" >&2
echo "                  is dropped below, not attempted." >&2

echo ""
echo "=== [3/3] CoreServices umbrella ==="
CS_UMB_FLAGS="${CS_FLAGS}"
umb_objs=$(compile_list CoreServicesUmbrella "${CS}" "${CS_UMB_FLAGS}" constants.m)
cs_out="${STAGED_OVERLAY}/System/Library/Frameworks/CoreServices.framework/Versions/A/CoreServices"
mkdir -p "$(dirname "${cs_out}")"
# reexport.exp lists ~30 libc/libm math-function symbols (see
# docs/SPEC-coredata-coreservices-build.md) -- a real macOS CoreServices
# quirk (it reexports part of libSystem for historical compat), copied
# verbatim, arch-specific per the real CMakeLists.txt's TARGET_ARM64 check
# (this script only ever builds x86_64, so always reexport.exp, never
# reexport_arm64.exp).
#
# NOTE: passed bare (-reexport_library / -reexported_symbols_list), NOT
# -Wl,-reexport_library,... like the CMakeLists.txt uses -- that -Wl, prefix
# is for when CMake links via the `cc` driver; this script invokes ld64.lld
# directly, so its native flag spelling applies without a -Wl, wrapper. If
# ld64.lld actually parses these as un-prefixed top-level options is UNVERIFIED
# (see spec doc §Risks -- this exact mechanism has never been exercised by any
# script in this repo before this one).
# shellcheck disable=SC2086
ld64.lld ${LD_FLAGS} -dylib \
	-install_name /System/Library/Frameworks/CoreServices.framework/Versions/A/CoreServices \
	-reexported_symbols_list "${CS}/reexport.exp" \
	-o "${cs_out}" \
	${umb_objs} \
	-reexport_library "${ae_out}" \
	-reexport_library "${ds_out}" \
	-reexport_library "${os_out}" \
	-reexport_library "${sk_out}" \
	-reexport_library "${sfl_out}" \
	-reexport_library "${md_out}" \
	-reexport_library "${cc_out}" \
	"${STAGED_OVERLAY}/System/Library/Frameworks/CoreFoundation.framework/Versions/A/CoreFoundation" \
	"${STAGED_OVERLAY}/usr/lib/libSystem.B.dylib"
mkdir -p "${OVERLAY}/System/Library/Frameworks/CoreServices.framework/Versions/A"
cp "${cs_out}" "${OVERLAY}/System/Library/Frameworks/CoreServices.framework/Versions/A/CoreServices"
# Subframeworks also need to land in the persistent overlay -- AppKit's own
# build only require_frameworks-checks the umbrella's top-level binary (see
# build-gui.sh:293), but anything that #imports <CarbonCore/...> etc. directly
# at compile time (LaunchServices does; nothing in AppKit itself is known to)
# would need these staged too.
mkdir -p "${OVERLAY}/System/Library/Frameworks/CoreServices.framework/Versions/A/Frameworks"
cp -R "${CS_FRAMEWORKS}/." "${OVERLAY}/System/Library/Frameworks/CoreServices.framework/Versions/A/Frameworks/"

echo "Built: ${cs_out}"
echo ""
echo "=== done ==="
echo "CoreData.framework and CoreServices.framework (7 of 9 subframeworks --"
echo "FSEvents and LaunchServices skipped, see banner above) staged into:"
echo "  ${OVERLAY}/System/Library/Frameworks/CoreData.framework"
echo "  ${OVERLAY}/System/Library/Frameworks/CoreServices.framework"
echo "This narrows build-gui.sh's AppKit-stage require_frameworks call"
echo "(build-freebsd/build-gui.sh:293) from 5 missing frameworks to 3"
echo "(CoreText, QuartzCore, ImageIO -- all blocked on Onyx2D/CoreGraphics,"
echo "see docs/SPEC-remaining-frameworks.md)."
