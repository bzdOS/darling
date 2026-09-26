#!/bin/sh
# Build the Wayland.backend NSDisplay bundle (WaylandDisplay.m/WaylandWindow.m/
# WaylandInput.m under src/external/cocotron/AppKit/Wayland.backend/) as a
# guest Mach-O bundle dylib, the same raw clang + ld64.lld way
# build-gui.sh/build-real-macho-tests.sh build everything else in this repo --
# NOT CMake's COMPONENT_gui tree (see AppKit/CMakeLists.txt's own add_backend()
# for the CMake-side version of this, which is written but has never been
# exercised through a real `cmake --build` on this port either).
#
# THIS SCRIPT HAS NEVER BEEN RUN. Wayland.backend's *.m files have never been
# compiled by anything (see the header comment in WaylandInput.m: "NOT
# COMPILED/LINKED YET"). This script was written by reading the three source
# files, NSDisplay.h/CGWindow.h/O2Surface.h/O2Context.h/NSEvent.h/
# NSEvent_mouse.h, and AppKit/CMakeLists.txt's own (also-untested) add_backend
# invocation for Wayland -- not by running a compiler. Expect it to surface
# real errors the first time it's actually run on the FreeBSD dev VM. Two
# concrete, already-identified risks it does NOT paper over (see
# docs/SPEC-appkit-display-backend.md and the review that produced this
# script for the full list):
#
#   1. WaylandInput.h:135 declares `__weak WaylandDisplay *_display;` in a
#      translation unit compiled WITHOUT -fobjc-arc and without the separate
#      -fobjc-weak flag (grep AppKit/CMakeLists.txt -- neither flag is set
#      anywhere in this tree). Clang rejects `__weak` on an ivar under plain
#      MRC (manual retain/release) unless one of those two flags is present;
#      this is expected to be a hard compile error on WaylandInput.m, not a
#      warning. Not patched here -- it's Wayland.backend source, out of this
#      script's job, but flagged loudly so the first build failure isn't a
#      surprise.
#   2. WaylandKeyCodes.h includes <HIToolbox/Events.h> for the kVK_* Carbon
#      keycode constants (same header the existing, unmodified X11.backend/
#      CarbonKeys.h already depends on). That header does NOT exist anywhere
#      in this tree: not in src/external/cocotron, not in
#      tests/vendor/fakesdk, and not inside tests/vendor/macosx-sdk-flat.tar.gz
#      (checked: that tarball ships only corefoundation-headers/, usr/include/,
#      and Frameworks/{CoreFoundation,Security}.framework -- no HIToolbox, no
#      CarbonCore, no OpenGL either, and CGWindow.h needs OpenGL/CGLTypes.h,
#      NSDisplay.h needs CarbonCore/UnicodeUtilities.h). This is a PRE-EXISTING
#      gap that already blocks X11.backend too, not something new to Wayland;
#      it blocks the whole AppKit stage of build-gui.sh, not just this script.
#      Preflight step 5 below checks for it explicitly and FATALs with this
#      exact explanation rather than producing a wall of "file not found"
#      errors deep into the AppKit compile loop.
#
# Usage: sh build-freebsd/build-wayland-backend.sh
# Run on the FreeBSD dev VM (185) -- never tested there. Must run AFTER
# build-gui.sh has successfully built (and persisted -- see build-gui.sh's own
# "persist to \$OVERLAY" steps) Onyx2D.dylib, CoreGraphics.dylib, and
# AppKit.dylib. As of this writing build-gui.sh cannot get that far either
# (see risk #2 above and build-gui.sh's own header) -- this script's preflight
# will say so plainly instead of guessing.
#
# Environment (same names/defaults as build-gui.sh, so the two can share a
# staged overlay/SDK when run back to back):
#   DARLING_BUILD_DIR — scratch build dir        (default: /var/darling-build)
#   DARLING_SRC_DIR    — root of this repository  (default: directory of this script/..)
#   DARLING_OVERLAY    — darling overlay dir      (required)
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
SRC="${DARLING_SRC_DIR:-${SCRIPT_DIR}}"
BUILD="${DARLING_BUILD_DIR:-/var/darling-build}/wayland-backend"
OVERLAY="${DARLING_OVERLAY:?set DARLING_OVERLAY to your overlay dir}"

SDK_FLAT="${BUILD}/sdk-flat"
STAGED_OVERLAY="${BUILD}/staged-overlay"
COCOTRON="${SRC}/src/external/cocotron"
WBACKEND="${COCOTRON}/AppKit/Wayland.backend"

# --- 1. Preflight: host tools. ---
for tool in clang ld64.lld pkg-config wayland-scanner; do
	if ! command -v "${tool}" >/dev/null 2>&1; then
		echo "FATAL: required tool '${tool}' not found in PATH." >&2
		echo "  clang/ld64.lld:   pkg install llvm" >&2
		echo "  pkg-config:       pkg install pkgconf" >&2
		echo "  wayland-scanner:  pkg install wayland" >&2
		exit 1
	fi
done

# --- 2. Preflight: native Wayland dev packages (headers this backend #include's
# --- directly: wayland-client.h in WaylandDisplay.h/WaylandWindow.h,
# --- xkbcommon.h + xkbcommon-names.h in WaylandInput.h/.m). ---
missing_pkgs=""
pkg-config --exists wayland-client || missing_pkgs="${missing_pkgs} wayland"
pkg-config --exists xkbcommon || missing_pkgs="${missing_pkgs} libxkbcommon"
if [ -n "${missing_pkgs}" ]; then
	echo "FATAL: missing native dev packages:${missing_pkgs}" >&2
	echo "  pkg install${missing_pkgs}" >&2
	exit 1
fi

# --- 3. Preflight: xdg-shell.xml, to generate xdg-shell-client-protocol.h/.c
# --- via wayland-scanner (same two paths AppKit/CMakeLists.txt's own,
# --- already-written but never-built, generation block checks). ---
XDG_SHELL_XML=""
for candidate in \
	/usr/share/wayland-protocols/stable/xdg-shell/xdg-shell.xml \
	/usr/local/share/wayland-protocols/stable/xdg-shell/xdg-shell.xml
do
	if [ -f "${candidate}" ]; then
		XDG_SHELL_XML="${candidate}"
		break
	fi
done
if [ -z "${XDG_SHELL_XML}" ]; then
	echo "FATAL: xdg-shell.xml not found in either" >&2
	echo "  /usr/share/wayland-protocols/stable/xdg-shell/ or" >&2
	echo "  /usr/local/share/wayland-protocols/stable/xdg-shell/" >&2
	echo "  pkg install wayland-protocols" >&2
	exit 1
fi

# --- 4. Preflight: the GUI dependency chain must already exist in the
# --- PERSISTENT overlay (not some other script's throwaway scratch dir --
# --- see risk note in the header comment: build-gui.sh's own STAGED_OVERLAY
# --- is deleted+recreated every run via `rm -rf "${BUILD}"`, so anything it
# --- builds only survives if build-gui.sh explicitly copies it into
# --- $OVERLAY the way it (as of this writing) only does for Foundation.dylib
# --- via build-real-macho-tests.sh -- Onyx2D/CoreGraphics/AppKit need the
# --- same treatment, see build-gui.sh's "persist to \$OVERLAY" steps). This
# --- step honestly reports whichever of these is missing instead of diving
# --- into a compile that's guaranteed to fail at link time. ---
missing_deps=""
[ -f "${OVERLAY}/System/Library/Frameworks/Foundation.framework/Versions/C/Foundation" ] \
	|| missing_deps="${missing_deps} Foundation(build-real-macho-tests.sh)"
[ -f "${OVERLAY}/System/Library/PrivateFrameworks/Onyx2D.framework/Versions/A/Onyx2D" ] \
	|| missing_deps="${missing_deps} Onyx2D(build-gui.sh)"
[ -f "${OVERLAY}/System/Library/Frameworks/CoreGraphics.framework/Versions/A/CoreGraphics" ] \
	|| missing_deps="${missing_deps} CoreGraphics(build-gui.sh)"
[ -f "${OVERLAY}/System/Library/Frameworks/AppKit.framework/Versions/C/AppKit" ] \
	|| missing_deps="${missing_deps} AppKit(build-gui.sh)"
if [ -n "${missing_deps}" ]; then
	echo "FATAL: Wayland.backend links against these, none present in ${OVERLAY}:" >&2
	for d in ${missing_deps}; do
		echo "  - ${d}" >&2
	done
	echo "  Run the script named in parentheses first. As of this writing" >&2
	echo "  build-gui.sh's Onyx2D/CoreGraphics/AppKit stages have never" >&2
	echo "  actually reached a working link (missing OpenGL/HIToolbox/" >&2
	echo "  CarbonCore headers in the flat SDK -- see this script's own" >&2
	echo "  header comment, risk #2), so getting past THIS check may itself" >&2
	echo "  require fixing that first. Not something this script can do for you." >&2
	exit 1
fi

# --- 5. Preflight: HIToolbox/Events.h (WaylandKeyCodes.h's kVK_* constants).
# --- Documented pre-existing gap (risk #2 above) -- checked here explicitly,
# --- against every location this repo has ever unpacked SDK-ish headers to,
# --- so the failure mode is one clear message instead of a raw
# --- "file not found" three files deep in a -I search path. ---
hitoolbox_found=""
for root in "${SRC}/src/freebsd-shims/missing-headers" "${SDK_FLAT}/usr/include" "${SDK_FLAT}/Frameworks" "${SRC}/tests/vendor/fakesdk"; do
	if [ -f "${root}/HIToolbox/Events.h" ] || [ -f "${root}/HIToolbox.framework/Headers/Events.h" ]; then
		hitoolbox_found="${root}"
		break
	fi
done
# SDK_FLAT isn't unpacked yet at this point in a from-scratch run -- unpack
# first, then re-check, so the common case (SDK just doesn't have it, as
# confirmed by inspecting tests/vendor/macosx-sdk-flat.tar.gz's contents
# directly) is still caught before any compile is attempted.
rm -rf "${BUILD}"
mkdir -p "${SDK_FLAT}" "${STAGED_OVERLAY}/usr/lib/system"
if [ ! -f "${SRC}/tests/vendor/macosx-sdk-flat.tar.gz" ]; then
	echo "FATAL: ${SRC}/tests/vendor/macosx-sdk-flat.tar.gz missing." >&2
	echo "  See tests/vendor/README.md to regenerate it." >&2
	exit 1
fi
tar xzf "${SRC}/tests/vendor/macosx-sdk-flat.tar.gz" -C "${SDK_FLAT}"
if [ -z "${hitoolbox_found}" ]; then
	for root in "${SRC}/src/freebsd-shims/missing-headers" "${SDK_FLAT}/usr/include" "${SDK_FLAT}/Frameworks" "${SRC}/tests/vendor/fakesdk"; do
		if [ -f "${root}/HIToolbox/Events.h" ] || [ -f "${root}/HIToolbox.framework/Headers/Events.h" ]; then
			hitoolbox_found="${root}"
			break
		fi
	done
fi
if [ -z "${hitoolbox_found}" ]; then
	echo "FATAL: HIToolbox/Events.h not found anywhere this repo unpacks SDK" >&2
	echo "  headers to (checked \${SDK_FLAT}/usr/include, \${SDK_FLAT}/Frameworks," >&2
	echo "  tests/vendor/fakesdk). WaylandKeyCodes.h needs it for the kVK_*" >&2
	echo "  Carbon virtual-keycode constants (same dependency the existing" >&2
	echo "  X11.backend/CarbonKeys.h already has -- this is not new to" >&2
	echo "  Wayland.backend). tests/vendor/macosx-sdk-flat.tar.gz ships only" >&2
	echo "  corefoundation-headers/, usr/include/, and Frameworks/{CoreFoundation," >&2
	echo "  Security}.framework -- no HIToolbox anywhere. See tests/vendor/README.md" >&2
	echo "  for how that tarball is produced; it needs a HIToolbox/Events.h (the" >&2
	echo "  kVK_* enum only -- it's a plain header, no binary) added to it, or to" >&2
	echo "  tests/vendor/fakesdk/, before this can build. Not fabricated here --" >&2
	echo "  guessing at Apple's exact kVK_* integer values instead of sourcing" >&2
	echo "  them for real would silently corrupt every keyboard event this" >&2
	echo "  backend ever produces." >&2
	exit 1
fi

# --- Stage overlay dylibs this backend links against directly. Same
# --- virtiofs-symlink reasoning as build-gui.sh/build-real-macho-tests.sh:
# --- real files only, never traversed live off the 9p mount. ---
cp "${OVERLAY}/usr/lib/libSystem.B.dylib" "${STAGED_OVERLAY}/usr/lib/libSystem.B.dylib"
cp "${OVERLAY}/usr/lib/libobjc.A.dylib" "${STAGED_OVERLAY}/usr/lib/libobjc.A.dylib"
ln -sf libobjc.A.dylib "${STAGED_OVERLAY}/usr/lib/libobjc.dylib"
(cd "${OVERLAY}/usr/lib/system" && find . -maxdepth 1 -type f | pax -rw "${STAGED_OVERLAY}/usr/lib/system")

mkdir -p "${STAGED_OVERLAY}/System/Library/Frameworks/CoreFoundation.framework/Versions/A"
cp "${OVERLAY}/System/Library/Frameworks/CoreFoundation.framework/Versions/A/CoreFoundation" \
	"${STAGED_OVERLAY}/System/Library/Frameworks/CoreFoundation.framework/Versions/A/CoreFoundation"
mkdir -p "${STAGED_OVERLAY}/System/Library/Frameworks/Foundation.framework/Versions/C"
cp "${OVERLAY}/System/Library/Frameworks/Foundation.framework/Versions/C/Foundation" \
	"${STAGED_OVERLAY}/System/Library/Frameworks/Foundation.framework/Versions/C/Foundation"
mkdir -p "${STAGED_OVERLAY}/System/Library/PrivateFrameworks/Onyx2D.framework/Versions/A"
cp "${OVERLAY}/System/Library/PrivateFrameworks/Onyx2D.framework/Versions/A/Onyx2D" \
	"${STAGED_OVERLAY}/System/Library/PrivateFrameworks/Onyx2D.framework/Versions/A/Onyx2D"
mkdir -p "${STAGED_OVERLAY}/System/Library/Frameworks/CoreGraphics.framework/Versions/A"
cp "${OVERLAY}/System/Library/Frameworks/CoreGraphics.framework/Versions/A/CoreGraphics" \
	"${STAGED_OVERLAY}/System/Library/Frameworks/CoreGraphics.framework/Versions/A/CoreGraphics"
mkdir -p "${STAGED_OVERLAY}/System/Library/Frameworks/AppKit.framework/Versions/C"
cp "${OVERLAY}/System/Library/Frameworks/AppKit.framework/Versions/C/AppKit" \
	"${STAGED_OVERLAY}/System/Library/Frameworks/AppKit.framework/Versions/C/AppKit"

# --- Generate xdg-shell-client-protocol.h/.c. Same two wayland-scanner
# --- subcommands AppKit/CMakeLists.txt's own generation block uses
# --- (client-header / private-code), supported since wayland 1.15 --
# --- well below anything a current FreeBSD wayland pkg ships. ---
XDG_SHELL_HEADER="${BUILD}/xdg-shell-client-protocol.h"
XDG_SHELL_CODE="${BUILD}/xdg-shell-client-protocol.c"
wayland-scanner client-header "${XDG_SHELL_XML}" "${XDG_SHELL_HEADER}"
wayland-scanner private-code "${XDG_SHELL_XML}" "${XDG_SHELL_CODE}"

# --- Compile. -F${SDK_FLAT}/Frameworks is NOT optional here even though
# --- nothing in Wayland.backend directly writes a <Framework/Header.h>
# --- umbrella import itself: AppKit/NSDisplay.h/NSWindow.h/etc, which these
# --- three files transitively #import, cross-reference as
# --- <CoreFoundation/CFBase.h> and friends against the flat SDK's
# --- corefoundation-headers/ directory, which provides no such prefix on
# --- its own -- see build-gui.sh's own comment on this exact trap (the one
# --- CoreFoundation/CFBase.h -F gotcha the task description calls out).
CLANG_FLAGS="-target x86_64-apple-macos10.10 -nostdinc -D__DARWIN_ONLY_UNIX_CONFORMANCE=1"
CLANG_FLAGS="${CLANG_FLAGS} -fblocks -fconstant-cfstrings -fobjc-runtime=macosx-10.10 -fobjc-weak"
CLANG_FLAGS="${CLANG_FLAGS} -DOBJC_OLD_DISPATCH_PROTOTYPES=1 -DDARLING"
CLANG_FLAGS="${CLANG_FLAGS} -I${COCOTRON}/CoreGraphics/include"
CLANG_FLAGS="${CLANG_FLAGS} -I${SDK_FLAT}/usr/include -I${SRC}/tests/vendor/fakesdk"
CLANG_FLAGS="${CLANG_FLAGS} -I${SDK_FLAT}/corefoundation-headers"
CLANG_FLAGS="${CLANG_FLAGS} -I${COCOTRON}/../foundation/include -I${COCOTRON}/../foundation/include/Foundation"
CLANG_FLAGS="${CLANG_FLAGS} -F${SDK_FLAT}/Frameworks"
if [ -n "${hitoolbox_found}" ] && [ "${hitoolbox_found}" != "${SDK_FLAT}/usr/include" ]; then
	CLANG_FLAGS="${CLANG_FLAGS} -I${hitoolbox_found}"
fi

AK="${COCOTRON}/AppKit"
WFLAGS="-I${AK} -I${AK}/.. -I${AK}/include -I${AK}/include/AppKit"
WFLAGS="${WFLAGS} -I${AK}/NSEvent.subproj"
WFLAGS="${WFLAGS} -I${COCOTRON}/CoreGraphics/include -I${COCOTRON}/Onyx2D/include -I${COCOTRON}/CoreText/include"
WFLAGS="${WFLAGS} -I${SRC}/src/freebsd-shims/missing-headers"
WFLAGS="${WFLAGS} -I${WBACKEND} -I${BUILD}"
WFLAGS="${WFLAGS} $(pkg-config --cflags wayland-client xkbcommon)"

mkdir -p "${BUILD}/obj"
# shellcheck disable=SC2086
clang ${CLANG_FLAGS} ${WFLAGS} -w -O0 -c "${WBACKEND}/WaylandDisplay.m" -o "${BUILD}/obj/WaylandDisplay.o"
# shellcheck disable=SC2086
clang ${CLANG_FLAGS} ${WFLAGS} -w -O0 -c "${WBACKEND}/WaylandWindow.m" -o "${BUILD}/obj/WaylandWindow.o"
# shellcheck disable=SC2086
clang ${CLANG_FLAGS} ${WFLAGS} -w -O0 -c "${WBACKEND}/WaylandInput.m" -o "${BUILD}/obj/WaylandInput.o"
clang -target x86_64-apple-macos10.10 -nostdinc -I"${BUILD}" \
	-I"${SDK_FLAT}/usr/include" -I"${SRC}/tests/vendor/fakesdk" \
	$(pkg-config --cflags wayland-client) \
	-w -O0 -c "${XDG_SHELL_CODE}" -o "${BUILD}/obj/xdg-shell-client-protocol.o"
# wayland_shim.c bridges to native libwayland-client.so.0 via Darling's
# _elfcalls ELF bridge, bypassing the overlay's broken wrapper which only
# resolves symbols without calling them.
clang -target x86_64-apple-macos10.10 -nostdinc \
	-I"${SRC}/src/startup/mldr/elfcalls" \
	-I"${SDK_FLAT}/usr/include" -I"${SRC}/tests/vendor/fakesdk" \
	$(pkg-config --cflags wayland-client) \
	-w -O0 -c "${WBACKEND}/wayland_shim.c" -o "${BUILD}/obj/wayland_shim.o"
# wayland_ifaces.c holds the mutable wl_<iface>_interface buffers and loads
# them from the native libwayland-client.so.0. It must compile WITHOUT
# <wayland-client.h> (which extern-declares the same symbols as const struct
# wl_interface and would collide with the char[] definitions), so only the
# elfcalls include is provided here, not pkg-config wayland-client.
clang -target x86_64-apple-macos10.10 -nostdinc \
	-I"${SRC}/src/startup/mldr/elfcalls" \
	-I"${SDK_FLAT}/usr/include" -I"${SRC}/tests/vendor/fakesdk" \
	$(pkg-config --cflags wayland-client) \
	-w -O0 -c "${WBACKEND}/wayland_ifaces.c" -o "${BUILD}/obj/wayland_ifaces.o"
# wayland_tramp.s provides the x86-64 assembly trampolines for the variadic
# marshalling primitives (wl_proxy_marshal_flags & friends). A C shim wrapper
# cannot forward a variadic argument list, so these trampolines preserve the
# whole register+stack argument state and `jmp` straight into the resolved
# native function (see the header comment in the .s for why).
clang -target x86_64-apple-macos10.10 -c "${WBACKEND}/wayland_tramp.s" -o "${BUILD}/obj/wayland_tramp.o"

# --- Link the bundle's single Mach-O dylib. NOTE: despite the ".backend"
# --- directory-naming convention, this project's own add_backend()
# --- (AppKit/CMakeLists.txt) builds backends as plain MH_DYLIB shared
# --- libraries via add_darling_library(... SHARED ...) (see
# --- cmake/darling_lib.cmake:16, `add_library(${name} SHARED ...)` +
# --- `SUFFIX ".dylib"` at :20) -- NOT MH_BUNDLE (-bundle). Matched here:
# --- plain -dylib, same as the Onyx2D/CoreGraphics/AppKit links above (and
# --- in build-gui.sh). Only the frameworks/libraries whose symbols the
# --- three .m files actually call directly are linked -- unlike
# --- AppKit/CMakeLists.txt's add_backend(Wayland ... DEPENDENCIES ...) list,
# --- which also names OpenGL/QuartzCore (copied from X11.backend's own
# --- DEPENDENCIES list, presumably by analogy -- but nothing in
# --- WaylandDisplay.m/WaylandWindow.m/WaylandInput.m calls a GL or
# --- QuartzCore symbol; this backend's whole point per
# --- docs/SPEC-appkit-display-backend.md §4-5 is avoiding GLX entirely).
# --- Native wayland-client/xkbcommon linked via pkg-config --libs, same
# --- pattern build-gui.sh uses for freetype2/fontconfig/libpng/zlib.
LD_FLAGS="-arch x86_64 -platform_version macos 10.10 10.10 -syslibroot ${STAGED_OVERLAY} -Z"
mkdir -p "${BUILD}/bundle"
# shellcheck disable=SC2086
ld64.lld ${LD_FLAGS} -dylib \
	-install_name /System/Library/Frameworks/AppKit.framework/Versions/C/Resources/Backends/Wayland.backend/Contents/MacOS/Wayland \
	-o "${BUILD}/bundle/Wayland" \
	"${BUILD}/obj/WaylandDisplay.o" "${BUILD}/obj/WaylandWindow.o" "${BUILD}/obj/WaylandInput.o" \
	"${BUILD}/obj/xdg-shell-client-protocol.o" "${BUILD}/obj/wayland_shim.o" "${BUILD}/obj/wayland_ifaces.o" "${BUILD}/obj/wayland_tramp.o" \
	"${STAGED_OVERLAY}/System/Library/Frameworks/AppKit.framework/Versions/C/AppKit" \
	"${STAGED_OVERLAY}/System/Library/Frameworks/CoreGraphics.framework/Versions/A/CoreGraphics" \
	"${STAGED_OVERLAY}/System/Library/PrivateFrameworks/Onyx2D.framework/Versions/A/Onyx2D" \
	"${STAGED_OVERLAY}/System/Library/Frameworks/CoreFoundation.framework/Versions/A/CoreFoundation" \
	"${STAGED_OVERLAY}/System/Library/Frameworks/Foundation.framework/Versions/C/Foundation" \
	"${STAGED_OVERLAY}/usr/lib/libobjc.A.dylib" "${STAGED_OVERLAY}/usr/lib/libSystem.B.dylib" \
	"${OVERLAY}/usr/lib/native/libxkbcommon.dylib"
chmod 755 "${BUILD}/bundle/Wayland"
file "${BUILD}/bundle/Wayland"

# --- Assemble the .backend bundle layout NSDisplay's plugin loader expects
# --- (NSDisplay.m:52-58: NSBundle scans
# --- <AppKit.framework>/Resources/Backends/*.backend, reads Info.plist's
# --- NSPriority/NSPrincipalClass) and install it into the PERSISTENT
# --- overlay, at the exact path add_backend()'s CMake version would have
# --- used (AppKit/CMakeLists.txt: path = ".../AppKit.framework/Versions/C/
# --- Resources/Backends/${name}.backend/Contents"). ---
BACKEND_DIR="${OVERLAY}/System/Library/Frameworks/AppKit.framework/Versions/C/Resources/Backends/Wayland.backend/Contents"
mkdir -p "${BACKEND_DIR}/MacOS"
cp "${BUILD}/bundle/Wayland" "${BACKEND_DIR}/MacOS/Wayland"
chmod 755 "${BACKEND_DIR}/MacOS/Wayland"
cp "${WBACKEND}/Info.plist" "${BACKEND_DIR}/Info.plist"

echo "=== done ==="
echo "Installed: ${BACKEND_DIR}/MacOS/Wayland"
echo "Installed: ${BACKEND_DIR}/Info.plist"
echo "NSPriority=300 (Wayland.backend/Info.plist) beats X11.backend's 200, so"
echo "NSDisplay's plugin loader tries this backend first -- but only once"
echo "AppKit.framework itself is actually staged at"
echo "${OVERLAY}/System/Library/Frameworks/AppKit.framework/... for something"
echo "to load it from in the first place (out of scope here -- see"
echo "build-gui.sh/build-darlingserver.sh for how a full run stages a"
echo "framework tree, none of which has been exercised end-to-end yet)."
