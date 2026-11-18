#!/bin/sh
# build-appkit.sh - rebuild ONLY AppKit.dylib (step [4/4] of build-gui.sh) against the CURRENT overlay dylibs.
#
# build-gui.sh is the only other script that builds AppKit, but it wipes its whole build dir and rebuilds Onyx2D,
# CoreGraphics, the stub frameworks and ImageIO first. This script does none of that: it compiles the AppKit sources
# with the same flags, links against the dylibs that are in $DARLING_OVERLAY right now (no staged copy of the overlay),
# and installs exactly one file: AppKit.framework/Versions/C/AppKit. The compile step, the flags and the link line are
# copies of build-gui.sh's; build-appkit-sync-test.sh fails if they drift apart.
#
# Patches: only cocotron-appkit-canInit-null.patch is applied here (idempotent, same rule as apply-patches.sh:
# already applied -> skip, applies cleanly -> apply, otherwise FATAL). build-gui.sh applies no patches itself;
# apply-patches.sh (all of series/) is run by build-all.sh and build-darlingserver.sh/build-stub-data.sh.
#
# It only deletes inside "$DARLING_BUILD_DIR/appkit-only".
#
# Usage: DARLING_SRC_DIR=... DARLING_BUILD_DIR=... DARLING_OVERLAY=... sh build-freebsd/build-appkit.sh
# Environment:
#   DARLING_BUILD_DIR - build dir root  (default: /opt/darling/build); scratch is <it>/appkit-only
#   DARLING_SRC_DIR   - repository root (default: directory of this script/..)
#   DARLING_OVERLAY   - overlay dir     (required)
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
SRC="${DARLING_SRC_DIR:-${SCRIPT_DIR}}"
BUILD_ROOT="${DARLING_BUILD_DIR:-/opt/darling/build}"
BUILD="${BUILD_ROOT}/appkit-only"
OVERLAY="${DARLING_OVERLAY:?set DARLING_OVERLAY to your overlay dir}"

SDK_FLAT="${BUILD}/sdk-flat"
COCOTRON="${SRC}/src/external/cocotron"
AK_DEST="${OVERLAY}/System/Library/Frameworks/AppKit.framework/Versions/C/AppKit"

# --- 0. Preflight: nothing is touched before this passes. ---
for tool in clang ld64.lld python3 pkg-config git md5; do
	if ! command -v "${tool}" >/dev/null 2>&1; then
		echo "FATAL: required tool '${tool}' not found in PATH." >&2
		exit 1
	fi
done
if [ ! -f "${SRC}/tests/vendor/macosx-sdk-flat.tar.gz" ]; then
	echo "FATAL: ${SRC}/tests/vendor/macosx-sdk-flat.tar.gz missing." >&2
	exit 1
fi
if [ ! -f "${AK_DEST}" ]; then
	echo "FATAL: no current AppKit at ${AK_DEST} (nothing to replace; refusing to create a new framework)." >&2
	exit 1
fi
missing=""
for lib in \
	System/Library/Frameworks/CoreFoundation.framework/Versions/A/CoreFoundation \
	System/Library/Frameworks/Foundation.framework/Versions/C/Foundation \
	System/Library/Frameworks/CoreGraphics.framework/Versions/A/CoreGraphics \
	System/Library/Frameworks/CoreData.framework/Versions/A/CoreData \
	System/Library/Frameworks/QuartzCore.framework/Versions/A/QuartzCore \
	System/Library/Frameworks/CoreText.framework/Versions/A/CoreText \
	System/Library/Frameworks/ImageIO.framework/Versions/A/ImageIO \
	System/Library/Frameworks/LaunchServices.framework/Versions/A/LaunchServices \
	System/Library/PrivateFrameworks/Onyx2D.framework/Versions/A/Onyx2D \
	usr/lib/libobjc.A.dylib usr/lib/libSystem.B.dylib \
	usr/lib/system/libdispatch.dylib usr/lib/system/libunwind.dylib \
	usr/lib/native/libGL.dylib usr/lib/native/libFreeType.dylib usr/lib/native/libfontconfig.dylib; do
	[ -f "${OVERLAY}/${lib}" ] || missing="${missing} ${lib}"
done
if [ -n "${missing}" ]; then
	echo "FATAL: missing in ${OVERLAY}:${missing}" >&2
	exit 1
fi

# --- 1. The one source patch this script owns (idempotent). ---
AK_PATCH="${SRC}/build-freebsd/patches/cocotron-appkit-canInit-null.patch"
if git -C "${COCOTRON}" apply --reverse --check "${AK_PATCH}" >/dev/null 2>&1; then
	echo "ALREADY cocotron-appkit-canInit-null.patch"
elif git -C "${COCOTRON}" apply --check "${AK_PATCH}" >/dev/null 2>&1 && git -C "${COCOTRON}" apply "${AK_PATCH}"; then
	echo "APPLIED cocotron-appkit-canInit-null.patch"
else
	echo "FATAL: cocotron-appkit-canInit-null.patch neither applies nor is already applied." >&2
	exit 1
fi

# --- 2. Clean ONLY our own scratch dir. ---
case "${BUILD}" in
"${BUILD_ROOT}/appkit-only") ;;
*) echo "FATAL: refusing to clean '${BUILD}': not ${BUILD_ROOT}/appkit-only" >&2; exit 1 ;;
esac
case "${BUILD_ROOT}" in
""|/) echo "FATAL: unsafe DARLING_BUILD_DIR '${BUILD_ROOT}'" >&2; exit 1 ;;
esac
rm -rf "${BUILD}"
mkdir -p "${SDK_FLAT}"
tar xzf "${SRC}/tests/vendor/macosx-sdk-flat.tar.gz" -C "${SDK_FLAT}"

# --- Flags, extract_sources, compile_component: COPIES of build-gui.sh (kept in sync by build-appkit-sync-test.sh). ---
CLANG_BASE="-target x86_64-apple-macos10.10 -nostdinc -D__DARWIN_ONLY_UNIX_CONFORMANCE=1"
CLANG_BASE="${CLANG_BASE} -fblocks -fconstant-cfstrings -fobjc-runtime=macosx-10.10"
CLANG_BASE="${CLANG_BASE} -DOBJC_OLD_DISPATCH_PROTOTYPES=1 -DDARLING"
SDK_SYSTEM_INCLUDES="-I${SDK_FLAT}/usr/include"
SDK_INCLUDES="-I${SRC}/tests/vendor/fakesdk"
SDK_INCLUDES="${SDK_INCLUDES} -I${SDK_FLAT}/corefoundation-headers"
SDK_INCLUDES="${SDK_INCLUDES} -I${COCOTRON}/../foundation/include -I${COCOTRON}/../foundation/include/Foundation"
SDK_INCLUDES="${SDK_INCLUDES} -F${SDK_FLAT}/Frameworks"
# Differs from build-gui.sh on purpose: the sysroot is the current overlay, not a staged copy.
LD_FLAGS="-arch x86_64 -platform_version macos 10.10 10.10 -syslibroot ${OVERLAY} -Z"

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

compile_component() {
	comp_name="$1"        # e.g. Onyx2D
	comp_dir="$2"         # e.g. ${COCOTRON}/Onyx2D
	sources_var="$3"      # e.g. Onyx2D_sources
	extra_flags="$4"      # component-specific -I / -include
	obj_dir="${BUILD}/${comp_name}-build"
	mkdir -p "${obj_dir}"
	objs=""
	extract_sources "${sources_var}" "${comp_dir}/CMakeLists.txt" > "${obj_dir}/sources.txt"
	if [ ! -s "${obj_dir}/sources.txt" ]; then
		echo "FATAL: ${sources_var} extracted empty from ${comp_dir}/CMakeLists.txt -- CMakeLists.txt format changed, extract_sources()'s regex needs updating." >&2
		exit 1
	fi
	while IFS= read -r rel; do
		[ -z "${rel}" ] && continue
		obj="${obj_dir}/$(echo "${rel}" | tr '/' '_').o"
		# shellcheck disable=SC2086
		clang ${CLANG_BASE} ${SDK_SYSTEM_INCLUDES} ${extra_flags} ${SDK_INCLUDES} -w -O0 -c "${comp_dir}/${rel}" -o "${obj}"
		objs="${objs} ${obj}"
	done < "${obj_dir}/sources.txt"
	echo "${objs}"
}

# APPKIT_KEEP_GOING=1 (this script only, diagnostic): compile every source without stopping, report, never link/stage
# when anything failed. The redefinition is indented on purpose so build-appkit-sync-test.sh still compares the
# column-0 compile_component above with build-gui.sh's.
if [ "${APPKIT_KEEP_GOING:-}" = 1 ]; then
	compile_component() {
		comp_name="$1"; comp_dir="$2"; sources_var="$3"; extra_flags="$4"
		obj_dir="${BUILD}/${comp_name}-build"
		mkdir -p "${obj_dir}"
		objs=""; kg_ok=0; kg_fail=0
		: > "${obj_dir}/failed.txt"
		extract_sources "${sources_var}" "${comp_dir}/CMakeLists.txt" > "${obj_dir}/sources.txt"
		if [ ! -s "${obj_dir}/sources.txt" ]; then
			echo "FATAL: ${sources_var} extracted empty from ${comp_dir}/CMakeLists.txt" >&2
			exit 1
		fi
		while IFS= read -r rel; do
			[ -z "${rel}" ] && continue
			obj="${obj_dir}/$(echo "${rel}" | tr '/' '_').o"
			# shellcheck disable=SC2086
			if clang ${CLANG_BASE} ${SDK_SYSTEM_INCLUDES} ${extra_flags} ${SDK_INCLUDES} -w -O0 -c "${comp_dir}/${rel}" -o "${obj}" > "${obj}.err" 2>&1; then
				kg_ok=$((kg_ok + 1))
				objs="${objs} ${obj}"
			else
				kg_fail=$((kg_fail + 1))
				echo "${rel}" >> "${obj_dir}/failed.txt"
			fi
		done < "${obj_dir}/sources.txt"
		echo "${kg_ok} ${kg_fail}" > "${obj_dir}/counts.txt"
		echo "${objs}"
	}
fi

echo "=== AppKit (only) ==="
AK="${COCOTRON}/AppKit"
AK_FLAGS="-I${AK} -I${AK}/.. -I${AK}/include -I${AK}/include/AppKit"
AK_FLAGS="${AK_FLAGS} -I${AK}/nib.subproj -I${AK}/NSColorPicker.subproj -I${AK}/NSMenu.subproj"
AK_FLAGS="${AK_FLAGS} -I${AK}/NSTextView.subproj -I${AK}/NSEvent.subproj -I${AK}/NSColor.subproj"
AK_FLAGS="${AK_FLAGS} -I${AK}/RTF.subproj -I${AK}/NSToolbar.subproj -I${AK}/NSDrawer.subproj"
AK_FLAGS="${AK_FLAGS} -I${AK}/X11.backend -I${COCOTRON}/CoreText -I${COCOTRON}/CoreText/include -I${COCOTRON}/CoreGraphics/include"
AK_FLAGS="${AK_FLAGS} -I${COCOTRON}/Onyx2D/include"
AK_FLAGS="${AK_FLAGS} -I${COCOTRON}/QuartzCore/include"
AK_FLAGS="${AK_FLAGS} -I${SRC}/src/freebsd-shims/missing-headers"
AK_FLAGS="${AK_FLAGS} -I${SRC}/src/frameworks/OpenGL/include"
AK_FLAGS="${AK_FLAGS} -I${COCOTRON}/CoreData/include"
AK_FLAGS="${AK_FLAGS} -I${SRC}/src/frameworks/CoreServices/include"
AK_FLAGS="${AK_FLAGS} -I${SRC}/src/frameworks/ImageIO/include"
AK_FLAGS="${AK_FLAGS} -I${AK}/private-include"
AK_FLAGS="${AK_FLAGS} -include CoreGraphics/CGGeometry.h"
AK_FLAGS="${AK_FLAGS} -include math.h -include stdlib.h -include CoreFoundation/CoreFoundation.h -include Foundation/Foundation.h"
AK_FLAGS="${AK_FLAGS} $(pkg-config --cflags freetype2 fontconfig 2>/dev/null)"
ak_objs=$(compile_component AppKit "${AK}" AppKit_sources "${AK_FLAGS}")
if [ "${APPKIT_KEEP_GOING:-}" = 1 ]; then
	read -r kg_ok kg_fail < "${BUILD}/AppKit-build/counts.txt"
	echo "BUILD-APPKIT compile ok=${kg_ok} fail=${kg_fail}"
	while IFS= read -r kg_f; do
		[ -z "${kg_f}" ] && continue
		kg_e=$(grep -a -m1 'error:' "${BUILD}/AppKit-build/$(echo "${kg_f}" | tr '/' '_').o.err" || true)
		echo "FAILED ${kg_f}: ${kg_e}"
	done < "${BUILD}/AppKit-build/failed.txt"
	if [ "${kg_fail}" -gt 0 ]; then
		echo "BUILD-APPKIT keep-going: failures, no link, no staging" >&2
		exit 1
	fi
fi
AK_NEW="${BUILD}/AppKit.new"
# shellcheck disable=SC2086
ld64.lld ${LD_FLAGS} -dylib \
	-install_name /System/Library/Frameworks/AppKit.framework/Versions/C/AppKit \
	-o "${AK_NEW}" \
	${ak_objs} \
	"${OVERLAY}/System/Library/Frameworks/CoreFoundation.framework/Versions/A/CoreFoundation" \
	"${OVERLAY}/System/Library/Frameworks/Foundation.framework/Versions/C/Foundation" \
	"${OVERLAY}/System/Library/Frameworks/CoreGraphics.framework/Versions/A/CoreGraphics" \
	"${OVERLAY}/System/Library/Frameworks/CoreData.framework/Versions/A/CoreData" \
	"${OVERLAY}/System/Library/Frameworks/QuartzCore.framework/Versions/A/QuartzCore" \
	"${OVERLAY}/System/Library/Frameworks/CoreText.framework/Versions/A/CoreText" \
	"${OVERLAY}/System/Library/Frameworks/ImageIO.framework/Versions/A/ImageIO" \
	"${OVERLAY}/System/Library/Frameworks/LaunchServices.framework/Versions/A/LaunchServices" \
	"${OVERLAY}/System/Library/PrivateFrameworks/Onyx2D.framework/Versions/A/Onyx2D" \
	"${OVERLAY}/usr/lib/libobjc.A.dylib" "${OVERLAY}/usr/lib/libSystem.B.dylib" \
	"${OVERLAY}/usr/lib/system/libdispatch.dylib" \
	"${OVERLAY}/usr/lib/system/libunwind.dylib" \
	"${OVERLAY}/usr/lib/native/libGL.dylib" \
	"${OVERLAY}/usr/lib/native/libFreeType.dylib" \
	"${OVERLAY}/usr/lib/native/libfontconfig.dylib"

# --- Stage exactly one file. Copy next to the target, then rename over it (a new inode: a process that has the old
# --- AppKit mapped keeps its bytes). The temp name is gone after the mv. ---
BUILT_MD5=$(md5 -q "${AK_NEW}")
echo "BUILT AppKit md5=${BUILT_MD5}"
cp "${AK_NEW}" "${AK_DEST}.new-$$"
mv -f "${AK_DEST}.new-$$" "${AK_DEST}"
STAGED_MD5=$(md5 -q "${AK_DEST}")
echo "STAGED ${AK_DEST} md5=${STAGED_MD5}"
if [ "${BUILT_MD5}" != "${STAGED_MD5}" ]; then
	echo "FATAL: staged AppKit md5 differs from the built one." >&2
	exit 1
fi
echo "BUILD-APPKIT done md5=${STAGED_MD5}"
