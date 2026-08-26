#!/bin/sh
# Build the Darling "native ELF wrapper" Mach-O dylibs that let guest Mach-O
# code (Onyx2D/CoreGraphics/AppKit, built by build-gui.sh) call straight
# into host FreeBSD ELF libraries (freetype/fontconfig/jpeg/png/tiff)
# without those libraries ever being reimplemented or recompiled as Mach-O.
#
# THIS SCRIPT HAS NEVER BEEN RUN. Written by reading:
#   - src/native/CMakeLists.txt          (which libs get wrap_elf()'d, under
#                                          what output name, for COMPONENT_gui)
#   - cmake/wrap_elf.cmake                (the CMake function: generates a .c
#                                          via wrapgen, compiles+links it as a
#                                          real add_darling_library())
#   - src/libelfloader/wrapgen/wrapgen.cpp (the actual generator: reads the
#                                          host ELF's DT_SONAME + exported
#                                          dynsym, emits one resolver-stub
#                                          function per symbol)
#   - cmake/use_ld64.cmake:165-176        (how the *built* full tree points
#                                          ld64 at these wrappers at
#                                          /usr/lib/native/libX.dylib during
#                                          *guest app* link time)
#   - src/startup/mldr/elfcalls/elfcalls.c (what actually answers
#                                          dlopen_fatal() at *runtime*: the
#                                          host libc's own dlopen(), called
#                                          from mldr, not from inside the
#                                          wrapper itself)
# No FreeBSD box was available to compile or run any of this. Read
# docs/SPEC-native-wrappers.md's "risks" section before trusting any of it.
#
# ---------------------------------------------------------------------------
# THE MECHANISM, end to end (see docs/SPEC-native-wrappers.md for the full
# writeup with file:line citations):
#
#   1. wrapgen (a plain HOST tool -- compiles as ordinary FreeBSD ELF, not
#      Mach-O; see build_wrapgen() below) mmaps the *host* ELF .so, walks its
#      PT_DYNAMIC segment for DT_SONAME and its .dynsym for exported
#      STB_GLOBAL/STT_FUNC symbols (wrapgen.cpp:102-252).
#   2. For each such library it emits ONE small Mach-O-side .c file
#      (wrapgen.cpp:254-279): a constructor that calls
#      _elfcalls->dlopen_fatal(<soname>), a destructor that dlcloses it, and
#      for every exported ELF function a same-named Mach-O function that is
#      declared `.symbol_resolver` -- i.e. it is NOT the real function, it's
#      an ifunc-style stub that, the first time dyld needs the real address,
#      calls _elfcalls->dlsym_fatal(handle, "symbol") and returns *that*
#      address for dyld to bind directly. No wrapped code ever runs the
#      wrapper body more than once per symbol.
#   3. That generated .c is compiled+linked as an ordinary (tiny) Mach-O
#      dylib -- this script does that part with plain clang+ld64.lld,
#      mirroring what cmake/wrap_elf.cmake's add_darling_library() would do
#      inside the full tree, but linking only against libSystem.B.dylib
#      (already built, staged in $DARLING_OVERLAY) instead of triggering a
#      full-tree rebuild of the giant `system` CMake target that
#      add_darling_library() would normally pull in via use_ld64() -- see
#      docs/SPEC-native-wrappers.md item R1 for why that shortcut is safe
#      here (the wrapper needs exactly one exported symbol from libSystem:
#      the `_elfcalls` global) and where it could bite later.
#   4. At GUI-app link time (cmake/use_ld64.cmake:165-176, NOT yet wired into
#      build-gui.sh -- see docs/SPEC-native-wrappers.md item R5), ld64 is
#      told `-Wl,-dylib_file,/usr/lib/native/libjpeg.dylib:<path to the file
#      this script just built>`: the load command baked into the *app*
#      records the runtime path /usr/lib/native/libjpeg.dylib, but ld64
#      reads *this* file right now to resolve undefined symbols against.
#      That's the whole trick: the linker's "what symbols exist" question
#      and the app's "where do I dlopen this at runtime" question are
#      answered by two different files.
#   5. At guest runtime, mldr loads the real /usr/lib/native/libjpeg.dylib
#      (this script's output, installed into $DARLING_OVERLAY). Its
#      constructor fires, calls _elfcalls->dlopen_fatal("libjpeg.so.8" or
#      whatever DT_SONAME wrapgen captured) -- and _elfcalls->dlopen_fatal is
#      just the HOST libc's dlopen(), called from mldr's own process (see
#      src/startup/mldr/elfcalls/elfcalls.c:17-31). So the real libjpeg.so
#      that satisfies it is resolved by the ordinary FreeBSD dynamic linker,
#      using its normal search rules, at that moment -- nothing in the
#      wrapper dylib itself contains a single line of libjpeg code.
# ---------------------------------------------------------------------------
#
# What this script builds (5 wrappers -- the minimum src/native/CMakeLists.txt
# actually defines for these libraries under COMPONENT_gui, see that file's
# lines 1-26):
#   name       host ELF soname    output
#   FreeType   libfreetype.so     usr/lib/native/libFreeType.dylib
#   jpeg       libjpeg.so         usr/lib/native/libjpeg.dylib
#   png        libpng.so          usr/lib/native/libpng.dylib
#   tiff       libtiff.so         usr/lib/native/libtiff.dylib
#   fontconfig libfontconfig.so   usr/lib/native/libfontconfig.dylib
#
# WHY ZLIB IS DELIBERATELY NOT HERE, even though the task that produced this
# script named it: src/native/CMakeLists.txt has NO wrap_elf(z ...) /
# wrap_elf(zlib ...) line -- zlib is not part of this mechanism at all in
# this tree. Instead cmake/use_ld64.cmake:135 points straight at a REAL,
# from-source-compiled Mach-O libz.dylib:
#   -Wl,-dylib_file,/usr/lib/libz.1.dylib:${CMAKE_BINARY_DIR}/src/external/zlib/libz.1.dylib
# built from the vendored src/external/zlib/zlib/*.c sources (real zlib C,
# compiled -target x86_64-apple-macos, not a wrapper around the host .so).
# That matches the ORIGINAL failure the task quotes almost exactly:
#   ld64.lld: error: cannot open .../staged-overlay/usr/lib/libz.dylib
# -- note the path: /usr/lib/libz.dylib, NOT /usr/lib/native/libz.dylib.
# build-gui.sh already looks for exactly that real dylib (see its "WARN: no
# libz.dylib in overlay" line) and just doesn't find it, because nothing has
# ever compiled src/external/zlib from source into the overlay. That is a
# SEPARATE, not-yet-written task (a sixth build-freebsd/*.sh script that
# compiles src/external/zlib the way build-real-macho-tests.sh compiles
# Foundation), not something this script can produce by wrapping the host
# libz.so -- doing that would silently diverge from how the rest of the tree
# (and every future full-tree rebuild) expects libz to be provided, which is
# exactly the "не подменять тихо" trap this repo's CLAUDE.md calls out.
#
# Usage: sh build-freebsd/build-native-wrappers.sh
# Run on the FreeBSD dev VM (185) -- never tested there. Requires
# build-real-macho-tests.sh to have already run once (needs libSystem.B.dylib
# staged in $DARLING_OVERLAY; same precondition build-gui.sh has).
#
# Environment:
#   DARLING_BUILD_DIR — scratch build dir        (default: /var/darling-build)
#   DARLING_SRC_DIR    — root of this repository  (default: directory of this script/..)
#   DARLING_OVERLAY    — darling overlay dir      (default: /path/to/darling-overlay)
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
SRC="${DARLING_SRC_DIR:-${SCRIPT_DIR}}"
BUILD="${DARLING_BUILD_DIR:-/var/darling-build}/native-wrappers"
OVERLAY="${DARLING_OVERLAY:-/path/to/darling-overlay}"

WRAPGEN_SRC="${SRC}/src/libelfloader/wrapgen/wrapgen.cpp"
ELFCALLS_DIR="${SRC}/src/startup/mldr/elfcalls"

STAGED_OVERLAY="${BUILD}/staged-overlay"
NATIVE_OUT="${BUILD}/native-out"

# --- 0. Preflight: fail before touching anything if the basics aren't here. ---
for tool in clang clang++ ld64.lld; do
	if ! command -v "${tool}" >/dev/null 2>&1; then
		echo "FATAL: required tool '${tool}' not found in PATH." >&2
		echo "  clang/clang++/ld64.lld: pkg install llvm (same as the other build-freebsd/*.sh scripts)" >&2
		exit 1
	fi
done

if [ ! -f "${WRAPGEN_SRC}" ]; then
	echo "FATAL: ${WRAPGEN_SRC} missing -- has src/libelfloader/wrapgen/ moved?" >&2
	exit 1
fi
if [ ! -f "${ELFCALLS_DIR}/elfcalls.h" ]; then
	echo "FATAL: ${ELFCALLS_DIR}/elfcalls.h missing." >&2
	exit 1
fi
if [ ! -f "${OVERLAY}/usr/lib/libSystem.B.dylib" ]; then
	echo "FATAL: no libSystem.B.dylib in ${OVERLAY}." >&2
	echo "  Run build-freebsd/build-real-macho-tests.sh first -- it stages" >&2
	echo "  libSystem.B.dylib into the overlay, and the wrapper dylibs built here" >&2
	echo "  link against exactly that file to resolve the _elfcalls global." >&2
	exit 1
fi

rm -rf "${BUILD}"
mkdir -p "${STAGED_OVERLAY}/usr/lib" "${NATIVE_OUT}" "${BUILD}/gen"

# --- Stage libSystem.B.dylib as a regular file (same virtiofs-symlink
# --- reasoning as build-real-macho-tests.sh / build-gui.sh: readlink() over
# --- the 9p/virtiofs mount is broken, so copy with cp, never rely on a live
# --- symlink chain through the mount). ---
cp "${OVERLAY}/usr/lib/libSystem.B.dylib" "${STAGED_OVERLAY}/usr/lib/libSystem.B.dylib"
# libSystem.B.dylib is an umbrella: it re-exports ~20 usr/lib/system/ dylibs and
# ld64 resolves every one of those LC_REEXPORT_DYLIB entries at link time, so
# staging the umbrella alone fails with "unable to locate re-export with install
# name ..." for each. Copy the closure with find|pax rather than cp -a for the
# same reason as above -- readlink() over virtiofs is broken. Mirrors
# build-real-macho-tests.sh:65.
mkdir -p "${STAGED_OVERLAY}/usr/lib/system"
(cd "${OVERLAY}/usr/lib/system" && find . -maxdepth 1 -type f | pax -rw "${STAGED_OVERLAY}/usr/lib/system")

# --- 1. Build wrapgen -- a plain HOST tool (ordinary FreeBSD ELF binary,
# --- compiled with the *host* clang++, NOT the Mach-O target). It parses
# --- ELF .so files directly (wrapgen.cpp:102-252) and, for anything it can't
# --- open as a bare path, falls back to dlopen()+dlinfo(RTLD_DI_LINKMAP) to
# --- ask the host loader where it actually lives (wrapgen.cpp:52-79) -- so
# --- passing it a bare soname like "libjpeg.so" is intentional, not
# --- laziness: that's the same bare name src/native/CMakeLists.txt itself
# --- passes to wrap_elf(), and it's how the tool is designed to be used. ---
echo "=== Building wrapgen (host tool) ==="
clang++ -O2 -std=c++14 -w -o "${BUILD}/wrapgen" "${WRAPGEN_SRC}" -ldl \
	|| clang++ -O2 -std=c++14 -w -o "${BUILD}/wrapgen" "${WRAPGEN_SRC}"
# (FreeBSD's libc already contains dlopen/dlsym/dlinfo -- there may be no
# separate libdl to link against, hence the -ldl-less fallback above.)

# name        host ELF soname (bare -- wrapgen resolves it via dlopen/dlinfo)
WRAP_NAMES="FreeType jpeg png tiff fontconfig gif"
soname_for() {
	case "$1" in
		FreeType)   echo "libfreetype.so" ;;
		jpeg)       echo "libjpeg.so" ;;
		png)        echo "libpng.so" ;;
		tiff)       echo "libtiff.so" ;;
		fontconfig) echo "libfontconfig.so" ;;
		# O2ImageSource_GIF.m calls DGifSlurp/DGifOpen/DGifCloseFile/
		# DGifSavedExtensionToGCB -- Onyx2D does need giflib, this was
		# wrongly assumed dead weight when the Onyx2D link was fixed to
		# use wrapper dylibs (see build-gui.sh's Onyx2D link comment).
		gif)        echo "libgif.so" ;;
		*) echo "FATAL: no soname mapping for '$1'" >&2; exit 1 ;;
	esac
}

# Match build-gui.sh's own -target/-mmacosx-version-min choice (10.10) since
# these wrappers exist to satisfy Onyx2D's link step there -- see
# docs/SPEC-native-wrappers.md item R4 for the version-mismatch risk this
# carries against the 10.12 Foundation.dylib build-real-macho-tests.sh
# produces (already flagged, unresolved, in docs/SPEC-gui-build.md #6.6).
# -nostdinc drops clang's own include path too, so <stdint.h> -- pulled in by
# elfcalls.h:4 -- has to come from the flat SDK like every other guest build
# here. Without this the wrapper C files fail to compile before the assembler
# is ever reached, which makes the '.symbol_resolver' diagnostic below fire on
# a failure that has nothing to do with it. Mirrors
# build-real-macho-tests.sh:75-76, the one guest build known to work.
SDK_FLAT="${DARLING_SDK_FLAT:-}"
if [ -z "${SDK_FLAT}" ]; then
	for cand in "${DARLING_BUILD_DIR:-/var/darling-build}/gui/sdk-flat" \
	            "${DARLING_BUILD_DIR:-/var/darling-build}/real-macho/sdk-flat"; do
		[ -d "${cand}/usr/include" ] && { SDK_FLAT="${cand}"; break; }
	done
fi
if [ -z "${SDK_FLAT}" ]; then
	echo "FATAL: no flat SDK found. Run build-freebsd/sync-flat-sdk.sh, or set" >&2
	echo "  DARLING_SDK_FLAT to a directory containing usr/include." >&2
	exit 1
fi

CLANG_FLAGS="-target x86_64-apple-macos10.10 -nostdinc -w"
CLANG_FLAGS="${CLANG_FLAGS} -I${ELFCALLS_DIR}"
CLANG_FLAGS="${CLANG_FLAGS} -I${SDK_FLAT}/usr/include -I${SRC}/tests/vendor/fakesdk"
LD_FLAGS="-arch x86_64 -platform_version macos 10.10 10.10 -syslibroot ${STAGED_OVERLAY} -Z"

built=""
failed=""

for name in ${WRAP_NAMES}; do
	elfname="$(soname_for "${name}")"
	echo "=== ${name} (host ${elfname}) ==="

	gen_c="${BUILD}/gen/${name}.c"
	gen_h="${BUILD}/gen/${name}_vars.h"

	if ! "${BUILD}/wrapgen" "${elfname}" "${gen_c}" "${gen_h}"; then
		echo "FATAL: wrapgen could not read/locate ${elfname} for '${name}'." >&2
		echo "  This almost always means the host .so isn't installed, or isn't" >&2
		echo "  reachable under that bare soname via dlopen()'s search path (e.g." >&2
		echo "  only a versioned libfoo.so.N exists, with no unversioned -dev symlink)." >&2
		echo "  Check: pkg info -l <port> | grep '\\.so\$', and whether /usr/local/lib" >&2
		echo "  or /etc/ld-elf.so.conf.d/ actually expose ${elfname} unversioned." >&2
		failed="${failed} ${name}"
		continue
	fi

	obj="${BUILD}/gen/${name}.o"
	if ! clang ${CLANG_FLAGS} -c "${gen_c}" -o "${obj}"; then
		echo "FATAL: clang failed to compile the generated wrapper for '${name}'." >&2
		echo "  Prime suspect: the '.symbol_resolver' inline-asm directive" >&2
		echo "  (wrapgen.cpp:272) that marks each stub as an ifunc-style resolver." >&2
		echo "  Upstream Darling normally assembles this with cctools-port's own" >&2
		echo "  patched 'as' (src/external/cctools-port/cctools/as/read.c has the" >&2
		echo "  '.symbol_resolver' handling) -- but src/CMakeLists.txt COMMENTS OUT" >&2
		echo "  add_subdirectory(external/cctools-port/cctools/as) and never sets" >&2
		echo "  CMAKE_ASM_COMPILER to it either, so the real tree relies on clang's" >&2
		echo "  own integrated Mach-O assembler understanding the directive. If it" >&2
		echo "  doesn't on this LLVM version, there is no wired-up fallback assembler" >&2
		echo "  in this repo to switch to -- see docs/SPEC-native-wrappers.md item R2." >&2
		failed="${failed} ${name}"
		continue
	fi

	dylib_name="lib${name}.dylib"
	out="${NATIVE_OUT}/${dylib_name}"
	if ! ld64.lld ${LD_FLAGS} -dylib \
		-install_name "/usr/lib/native/${dylib_name}" \
		-o "${out}" \
		"${obj}" "${STAGED_OVERLAY}/usr/lib/libSystem.B.dylib"; then
		echo "FATAL: ld64.lld failed to link ${dylib_name}." >&2
		echo "  Prime suspect: the generated wrapper references the '_elfcalls' global" >&2
		echo "  (wrapgen.cpp:257), which is exported from libsystem_kernel's" >&2
		echo "  emulation/src/other/mach/lkm.c and re-exported through libSystem.B.dylib" >&2
		echo "  -- if that symbol isn't actually present/exported in the libSystem.B.dylib" >&2
		echo "  staged in \$DARLING_OVERLAY, this link fails. See" >&2
		echo "  docs/SPEC-native-wrappers.md item R1." >&2
		failed="${failed} ${name}"
		continue
	fi

	chmod 755 "${out}"
	file "${out}" 2>/dev/null || true
	built="${built} ${name}"
done

echo "=== Summary ==="
echo "Built:  ${built:-<none>}"
if [ -n "${failed}" ]; then
	echo "FAILED: ${failed}" >&2
	echo "Refusing to install a partial set into \$DARLING_OVERLAY -- fix the" >&2
	echo "failures above and rerun. (Per this repo's CLAUDE.md: 'не собирать" >&2
	echo "половину молча'.)" >&2
	exit 1
fi

# --- Only install once every requested wrapper actually built. ---
mkdir -p "${OVERLAY}/usr/lib/native"
for name in ${built}; do
	dylib_name="lib${name}.dylib"
	cp "${NATIVE_OUT}/${dylib_name}" "${OVERLAY}/usr/lib/native/${dylib_name}"
done

echo "=== done ==="
echo "Installed into ${OVERLAY}/usr/lib/native/: ${built}"
echo "NOTE: build-gui.sh does not yet reference these wrappers -- it currently"
echo "links Onyx2D with bare -ljpeg -lpng -ltiff -lgif, which is exactly the"
echo "'unhandled file type' / 'missing LC_ID_DYLIB' failure this script exists"
echo "to fix. Someone still needs to change build-gui.sh's Onyx2D ld64.lld"
echo "invocation to pass these dylibs instead (see docs/SPEC-native-wrappers.md"
echo "item R5) -- this script was asked not to edit build-gui.sh itself."
