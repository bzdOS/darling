#!/bin/sh
# Attempt to build the Cocotron GUI stack (Onyx2D, CoreGraphics, AppKit) as
# guest Mach-O dylibs, the way build-real-macho-tests.sh builds Foundation:
# raw clang + ld64.lld against the staged overlay, NOT CMake's COMPONENT_gui
# tree (that tree has never been exercised on this port -- see
# docs/SPEC-gui-build.md -- and depends on dozens of sibling frameworks that
# don't exist yet, via the -Wl,-dylib_file,... machinery in
# cmake/use_ld64.cmake, which only makes sense inside a full from-scratch
# tree build, not a standalone script).
#
# THIS SCRIPT HAS NEVER BEEN RUN. It was written by reading CMakeLists.txt
# files and the existing build-freebsd/*.sh scripts, without a FreeBSD box
# to test on. Read docs/SPEC-gui-build.md's "honest" section before trusting
# any of this. The script is deliberately structured to FATAL loudly and
# stop at the first missing dependency instead of limping on and producing a
# half-built, half-broken dylib -- per this repo's CLAUDE.md ("не собирать
# половину молча").
#
# What it can plausibly build, in order, and why each next stage is expected
# to fail (see docs/SPEC-gui-build.md for the full accounting):
#   1. Onyx2D.dylib   -- ALL its DEPENDENCIES (objc, system, CoreFoundation,
#                        Foundation, z, FreeType, fontconfig, jpeg, png,
#                        tiff, gif -- see src/external/cocotron/Onyx2D/
#                        CMakeLists.txt:154-164) are either already staged
#                        in the overlay or claim to be ordinary host
#                        libraries. This is the one stage with a real chance
#                        of linking.
#   2. CoreGraphics.dylib -- DEPENDENCIES include IOKit (CoreGraphics/
#                        CMakeLists.txt:131-138), and IOKit.framework has
#                        never been built into the overlay (only
#                        CoreFoundation/DirectoryService/Foundation/LDAP/
#                        SystemConfiguration exist under
#                        $OVERLAY/System/Library/Frameworks -- checked
#                        2026-08-26). This script FATALs before attempting
#                        the CoreGraphics link if IOKit.framework is absent,
#                        rather than let ld64.lld produce a wall of
#                        undefined-symbol errors.
#   3. AppKit.dylib   -- DEPENDENCIES include CoreText, CoreData, QuartzCore,
#                        ImageIO, CoreServices (AppKit/CMakeLists.txt:
#                        541-558), none of which exist in the overlay
#                        either. Same FATAL-before-attempting treatment.
#   4. Wayland.backend -- DOES NOT EXIST. Cocotron ships exactly one
#                        AppKit backend, X11.backend (src/external/cocotron/
#                        AppKit/X11.backend/), which talks to a real X
#                        server via libX11/libXrandr/libXcursor/libXext/
#                        libXkbfile (AppKit/CMakeLists.txt:25-40). There is
#                        no Wayland backend source anywhere in this tree to
#                        build -- one would have to be WRITTEN (a new
#                        *.backend/CMakeLists.txt-worth of X11Display.m/
#                        X11Window.m/X11Event.m-equivalent Objective-C
#                        talking to libwayland-client + xdg-shell instead of
#                        Xlib), which is a development task, not a build
#                        step. This script therefore does not attempt it: it
#                        FATALs immediately at stage 4 with this explanation.
#
# Usage: sh build-freebsd/build-gui.sh
# Run on the FreeBSD dev VM (185) -- never tested there (see above).
#
# Environment:
#   DARLING_BUILD_DIR — scratch build dir        (default: /var/darling-build)
#   DARLING_SRC_DIR    — root of this repository  (default: directory of this script/..)
#   DARLING_OVERLAY    — darling overlay dir      (default: /path/to/darling-overlay)
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
SRC="${DARLING_SRC_DIR:-${SCRIPT_DIR}}"
BUILD="${DARLING_BUILD_DIR:-/var/darling-build}/gui"
OVERLAY="${DARLING_OVERLAY:-/path/to/darling-overlay}"

SDK_FLAT="${BUILD}/sdk-flat"
STAGED_OVERLAY="${BUILD}/staged-overlay"
COCOTRON="${SRC}/src/external/cocotron"

# --- 0. Preflight: fail before touching anything if the basics aren't here. ---
# (build-real-macho-tests.sh doesn't bother with this because clang/ld64.lld
# not existing fails loudly and immediately anyway on the first invocation --
# but this script has a lot more setup before the first compiler invocation,
# so a missing tool would otherwise be discovered many minutes in.)
for tool in clang ld64.lld python3 pkg-config; do
	if ! command -v "${tool}" >/dev/null 2>&1; then
		echo "FATAL: required tool '${tool}' not found in PATH." >&2
		echo "  clang/ld64.lld: pkg install llvm (same as the other build-freebsd/*.sh scripts)" >&2
		echo "  pkg-config:     pkg install pkgconf" >&2
		exit 1
	fi
done

if [ ! -f "${SRC}/tests/vendor/macosx-sdk-flat.tar.gz" ]; then
	echo "FATAL: ${SRC}/tests/vendor/macosx-sdk-flat.tar.gz missing." >&2
	echo "  See tests/vendor/README.md to regenerate it, or run build-real-macho-tests.sh" >&2
	echo "  first (it unpacks the same tarball and this script needs the exact same headers)." >&2
	exit 1
fi

if [ ! -f "${OVERLAY}/System/Library/Frameworks/Foundation.framework/Versions/C/Foundation" ]; then
	echo "FATAL: no Foundation.dylib in ${OVERLAY}." >&2
	echo "  Run build-freebsd/build-real-macho-tests.sh first -- it builds Foundation.dylib" >&2
	echo "  from source and installs it into the overlay. AppKit/CoreGraphics/Onyx2D all" >&2
	echo "  #include Foundation/Foundation.h and link against it." >&2
	exit 1
fi

# native (non-Apple) headers/libs Onyx2D needs. pkg-config existing does NOT
# guarantee the .so is on the linker's default search path in a form
# ld64.lld's -lname resolution can find (see docs/SPEC-gui-build.md's "what
# will almost certainly not work" section) -- this only catches the
# even-more-basic case of the -dev package not being installed at all.
missing_pkgs=""
for mod in freetype2 fontconfig libpng zlib; do
	pkg-config --exists "${mod}" 2>/dev/null || missing_pkgs="${missing_pkgs} ${mod}"
done
# libtiff/giflib/libjpeg-turbo don't reliably ship .pc files with those exact
# names across FreeBSD pkg versions -- check for the header instead of
# trusting pkg-config for these three.
for hdr_pkg in "tiff.h:tiff" "gif_lib.h:giflib" "jpeglib.h:jpeg-turbo"; do
	hdr="${hdr_pkg%%:*}"
	pkgname="${hdr_pkg##*:}"
	if ! find /usr/local/include -maxdepth 1 -name "${hdr}" 2>/dev/null | grep -q .; then
		missing_pkgs="${missing_pkgs} ${pkgname}"
	fi
done
if [ -n "${missing_pkgs}" ]; then
	echo "FATAL: missing native dev packages needed by Onyx2D:${missing_pkgs}" >&2
	echo "  pkg install${missing_pkgs}" >&2
	exit 1
fi

rm -rf "${BUILD}"
mkdir -p "${SDK_FLAT}" "${STAGED_OVERLAY}/usr/lib/system"

# --- Stage the flat SDK + overlay dylibs, same reasoning as
# --- build-real-macho-tests.sh (virtiofs symlink readlink() is broken --
# --- see tests/vendor/README.md -- so everything below is real files,
# --- never traversed live off the 9p mount). ---
tar xzf "${SRC}/tests/vendor/macosx-sdk-flat.tar.gz" -C "${SDK_FLAT}"

cp "${OVERLAY}/usr/lib/libSystem.B.dylib" "${STAGED_OVERLAY}/usr/lib/libSystem.B.dylib"
cp "${OVERLAY}/usr/lib/libobjc.A.dylib" "${STAGED_OVERLAY}/usr/lib/libobjc.A.dylib"
ln -sf libobjc.A.dylib "${STAGED_OVERLAY}/usr/lib/libobjc.dylib"
cp "${OVERLAY}/usr/lib/libicucore.A.dylib" "${STAGED_OVERLAY}/usr/lib/libicucore.A.dylib"
cp "${OVERLAY}/usr/lib/libc++.1.dylib" "${STAGED_OVERLAY}/usr/lib/libc++.1.dylib"
cp "${OVERLAY}/usr/lib/libc++abi.dylib" "${STAGED_OVERLAY}/usr/lib/libc++abi.dylib"
cp "${OVERLAY}/usr/lib/libz.dylib" "${STAGED_OVERLAY}/usr/lib/libz.dylib" 2>/dev/null \
	|| echo "WARN: no libz.dylib in overlay -- Onyx2D's O2zlib.m link will fail" >&2
(cd "${OVERLAY}/usr/lib/system" && find . -maxdepth 1 -type f | pax -rw "${STAGED_OVERLAY}/usr/lib/system")

mkdir -p "${STAGED_OVERLAY}/System/Library/Frameworks/CoreFoundation.framework/Versions/A"
cp "${OVERLAY}/System/Library/Frameworks/CoreFoundation.framework/Versions/A/CoreFoundation" \
	"${STAGED_OVERLAY}/System/Library/Frameworks/CoreFoundation.framework/Versions/A/CoreFoundation"
mkdir -p "${STAGED_OVERLAY}/System/Library/Frameworks/Foundation.framework/Versions/C"
cp "${OVERLAY}/System/Library/Frameworks/Foundation.framework/Versions/C/Foundation" \
	"${STAGED_OVERLAY}/System/Library/Frameworks/Foundation.framework/Versions/C/Foundation"

# Common clang flags. -mmacosx-version-min=10.10 matches AppKit/CoreGraphics/
# Onyx2D's own CMakeLists.txt (they all set it; Foundation's build used 10.12
# because that's what the sqlite/objc/cf tests wanted -- GUI code was written
# for 10.10 and mixing minimums across a dependency chain is asking for
# trouble, so this script uses 10.10 throughout instead).
CLANG_FLAGS="-target x86_64-apple-macos10.10 -nostdinc -D__DARWIN_ONLY_UNIX_CONFORMANCE=1"
CLANG_FLAGS="${CLANG_FLAGS} -fblocks -fconstant-cfstrings -fobjc-runtime=macosx-10.10"
CLANG_FLAGS="${CLANG_FLAGS} -DOBJC_OLD_DISPATCH_PROTOTYPES=1 -DDARLING"
CLANG_FLAGS="${CLANG_FLAGS} -I${SDK_FLAT}/usr/include -I${SRC}/tests/vendor/fakesdk"
CLANG_FLAGS="${CLANG_FLAGS} -I${SDK_FLAT}/corefoundation-headers"
CLANG_FLAGS="${CLANG_FLAGS} -I${COCOTRON}/../foundation/include -I${COCOTRON}/../foundation/include/Foundation"
LD_FLAGS="-arch x86_64 -platform_version macos 10.10 10.10 -syslibroot ${STAGED_OVERLAY} -Z"

# Extract a CMake `set(<varname> ... )` list of source files, skipping
# comments and commented-out entries, deduping (AppKit_sources genuinely
# lists NSTreeNode.m twice -- see AppKit/CMakeLists.txt:333,419 -- CMake's
# own dedup makes that harmless there, but a second `clang -c` on the same
# file produces a second .o with the same symbols, which ld64.lld errors on
# as a duplicate symbol at link time; build-real-macho-tests.sh hit the same
# thing with Foundation's list and dedupes for the same reason).
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
		clang ${CLANG_FLAGS} ${extra_flags} -w -O0 -c "${comp_dir}/${rel}" -o "${obj}"
		objs="${objs} ${obj}"
	done < "${obj_dir}/sources.txt"
	echo "${objs}"
}

# require_frameworks NAME fw1 fw2 ... -- FATAL if any $STAGED_OVERLAY
# .../Frameworks/<fw>.framework/Versions/*/<fw> binary is missing, printing
# all missing ones at once (not just the first) so the failure message is
# actually actionable.
require_frameworks() {
	comp_name="$1"
	shift
	missing=""
	for fw in "$@"; do
		found=$(find "${STAGED_OVERLAY}/System/Library/Frameworks/${fw}.framework" \
			-type f -name "${fw}" 2>/dev/null | head -1)
		if [ -z "${found}" ]; then
			missing="${missing} ${fw}"
		fi
	done
	if [ -n "${missing}" ]; then
		echo "FATAL: ${comp_name} needs these frameworks, none of which have ever been" >&2
		echo "  built by this port (not in ${OVERLAY}):${missing}" >&2
		echo "  Refusing to attempt the ${comp_name} link -- see docs/SPEC-gui-build.md." >&2
		exit 1
	fi
}

echo "=== [1/4] Onyx2D ==="
ONYX2D="${COCOTRON}/Onyx2D"
ONYX2D_FLAGS="-I${ONYX2D} -I${ONYX2D}/.. -I${ONYX2D}/include -I${ONYX2D}/include/Onyx2D"
ONYX2D_FLAGS="${ONYX2D_FLAGS} -I${COCOTRON}/CoreText -I${COCOTRON}/CoreText/include"
ONYX2D_FLAGS="${ONYX2D_FLAGS} $(pkg-config --cflags freetype2 fontconfig libpng zlib 2>/dev/null)"
ONYX2D_FLAGS="${ONYX2D_FLAGS} -I/usr/local/include"
onyx2d_objs=$(compile_component Onyx2D "${ONYX2D}" Onyx2D_sources "${ONYX2D_FLAGS}")
mkdir -p "${STAGED_OVERLAY}/System/Library/PrivateFrameworks/Onyx2D.framework/Versions/A"
# shellcheck disable=SC2086
ld64.lld ${LD_FLAGS} -dylib \
	-install_name /System/Library/PrivateFrameworks/Onyx2D.framework/Versions/A/Onyx2D \
	-o "${STAGED_OVERLAY}/System/Library/PrivateFrameworks/Onyx2D.framework/Versions/A/Onyx2D" \
	${onyx2d_objs} \
	"${STAGED_OVERLAY}/System/Library/Frameworks/CoreFoundation.framework/Versions/A/CoreFoundation" \
	"${STAGED_OVERLAY}/System/Library/Frameworks/Foundation.framework/Versions/C/Foundation" \
	"${STAGED_OVERLAY}/usr/lib/libobjc.A.dylib" "${STAGED_OVERLAY}/usr/lib/libz.dylib" \
	"${STAGED_OVERLAY}/usr/lib/libc++.1.dylib" "${STAGED_OVERLAY}/usr/lib/libSystem.B.dylib" \
	$(pkg-config --libs freetype2 fontconfig libpng zlib 2>/dev/null) -ltiff -ljpeg -lgif
# ^ -ltiff/-ljpeg/-lgif passed bare because those pkgs don't reliably ship
# .pc files (see the preflight check above) -- if pkg names differ from the
# actual .so name on this FreeBSD version, THIS is where it breaks; see
# docs/SPEC-gui-build.md.
echo "Built: ${STAGED_OVERLAY}/System/Library/PrivateFrameworks/Onyx2D.framework/Versions/A/Onyx2D"

echo "=== [2/4] CoreGraphics ==="
require_frameworks CoreGraphics IOKit
# unreachable while IOKit.framework doesn't exist -- see header comment.
CG="${COCOTRON}/CoreGraphics"
CG_FLAGS="-I${CG} -I${CG}/.. -I${CG}/include -I${CG}/include/CoreGraphics"
CG_FLAGS="${CG_FLAGS} -I${COCOTRON}/CoreText -I${COCOTRON}/Onyx2D/include"
CG_FLAGS="${CG_FLAGS} -include ${CG}/../Onyx2D/include/Onyx2D/Onyx2D.h"
cg_objs=$(compile_component CoreGraphics "${CG}" CoreGraphics_sources "${CG_FLAGS}")
mkdir -p "${STAGED_OVERLAY}/System/Library/Frameworks/CoreGraphics.framework/Versions/A"
# shellcheck disable=SC2086
ld64.lld ${LD_FLAGS} -dylib \
	-install_name /System/Library/Frameworks/CoreGraphics.framework/Versions/A/CoreGraphics \
	-o "${STAGED_OVERLAY}/System/Library/Frameworks/CoreGraphics.framework/Versions/A/CoreGraphics" \
	${cg_objs} \
	"${STAGED_OVERLAY}/System/Library/Frameworks/CoreFoundation.framework/Versions/A/CoreFoundation" \
	"${STAGED_OVERLAY}/System/Library/Frameworks/Foundation.framework/Versions/C/Foundation" \
	"${STAGED_OVERLAY}/System/Library/PrivateFrameworks/Onyx2D.framework/Versions/A/Onyx2D" \
	"${STAGED_OVERLAY}/usr/lib/libobjc.A.dylib" "${STAGED_OVERLAY}/usr/lib/libSystem.B.dylib" \
	-lGL "${STAGED_OVERLAY}/System/Library/Frameworks/IOKit.framework/Versions/A/IOKit"

echo "=== [3/4] AppKit ==="
require_frameworks AppKit CoreText CoreData QuartzCore ImageIO CoreServices
# unreachable while those five don't exist -- see header comment.
AK="${COCOTRON}/AppKit"
AK_FLAGS="-I${AK} -I${AK}/.. -I${AK}/include -I${AK}/include/AppKit"
AK_FLAGS="${AK_FLAGS} -I${AK}/nib.subproj -I${AK}/NSColorPicker.subproj -I${AK}/NSMenu.subproj"
AK_FLAGS="${AK_FLAGS} -I${AK}/NSTextView.subproj -I${AK}/NSEvent.subproj -I${AK}/NSColor.subproj"
AK_FLAGS="${AK_FLAGS} -I${AK}/RTF.subproj -I${AK}/NSToolbar.subproj -I${AK}/NSDrawer.subproj"
AK_FLAGS="${AK_FLAGS} -I${AK}/X11.backend -I${COCOTRON}/CoreText -I${COCOTRON}/CoreGraphics/include"
AK_FLAGS="${AK_FLAGS} -I${COCOTRON}/Onyx2D/include $(pkg-config --cflags freetype2 fontconfig 2>/dev/null)"
ak_objs=$(compile_component AppKit "${AK}" AppKit_sources "${AK_FLAGS}")
mkdir -p "${STAGED_OVERLAY}/System/Library/Frameworks/AppKit.framework/Versions/C"
# shellcheck disable=SC2086
ld64.lld ${LD_FLAGS} -dylib \
	-install_name /System/Library/Frameworks/AppKit.framework/Versions/C/AppKit \
	-o "${STAGED_OVERLAY}/System/Library/Frameworks/AppKit.framework/Versions/C/AppKit" \
	${ak_objs} \
	"${STAGED_OVERLAY}/System/Library/Frameworks/CoreFoundation.framework/Versions/A/CoreFoundation" \
	"${STAGED_OVERLAY}/System/Library/Frameworks/Foundation.framework/Versions/C/Foundation" \
	"${STAGED_OVERLAY}/System/Library/Frameworks/CoreGraphics.framework/Versions/A/CoreGraphics" \
	"${STAGED_OVERLAY}/System/Library/PrivateFrameworks/Onyx2D.framework/Versions/A/Onyx2D" \
	"${STAGED_OVERLAY}/usr/lib/libobjc.A.dylib" "${STAGED_OVERLAY}/usr/lib/libSystem.B.dylib" \
	-lGL $(pkg-config --libs freetype2 fontconfig 2>/dev/null)

echo "=== [4/4] Wayland.backend ==="
# There is no Wayland.backend source anywhere under
# src/external/cocotron/AppKit/ -- only X11.backend/ exists (see this
# script's header comment for the full explanation). Building a backend
# means compiling *.backend/*.m into a small dylib and installing it under
# AppKit.framework/.../Resources/Backends/<name>.backend/Contents/MacOS/ --
# add_backend() in AppKit/CMakeLists.txt:567-595 does exactly that for X11.
# A Wayland equivalent would need NEW source (X11Display.m/X11Window.m/
# X11Event.m/X11Cursor.m all reimplemented against libwayland-client +
# xdg-shell instead of Xlib), which this script cannot manufacture. FATAL
# here rather than silently skip, so a `sh build-gui.sh; echo $?` that
# returns 0 never happens for this repo's actual GUI target (Wayland).
echo "FATAL: no Wayland.backend source exists in this tree." >&2
echo "  Only src/external/cocotron/AppKit/X11.backend/ exists (X11, not Wayland)." >&2
echo "  A Wayland backend is a development task (new Objective-C source against" >&2
echo "  libwayland-client + xdg-shell-client-protocol.h), not something this build" >&2
echo "  script can produce. See docs/SPEC-gui-build.md." >&2
exit 1
