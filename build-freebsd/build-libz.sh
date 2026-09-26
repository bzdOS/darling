#!/bin/sh
# Compile the real, unmodified vendored zlib sources (src/external/zlib/zlib/
# *.c) into a guest Mach-O libz.dylib, the same way build-real-macho-tests.sh
# builds Foundation.dylib from source: raw clang -target x86_64-apple-macos +
# ld64.lld against the staged overlay, NOT CMake's src/external/zlib/
# CMakeLists.txt tree (that tree has never been exercised on this port --
# same reasoning as build-gui.sh's header comment for Onyx2D/AppKit).
#
# THIS SCRIPT HAS NEVER BEEN RUN. Written by reading src/external/zlib/
# CMakeLists.txt, cmake/use_ld64.cmake, and the existing build-freebsd/*.sh
# scripts, without a FreeBSD box to test on. Read docs/SPEC-native-wrappers.md
# section 2 first -- it explains why zlib is NOT one of the wrap_elf() ELF
# wrappers under usr/lib/native/ (there is no wrap_elf(z ...) entry in
# src/native/CMakeLists.txt) and instead is a real from-source Mach-O dylib,
# per cmake/use_ld64.cmake:135:
#   -Wl,-dylib_file,/usr/lib/libz.1.dylib:${CMAKE_BINARY_DIR}/src/external/zlib/libz.1.dylib
#
# Why this is needed: build-gui.sh's first stage (Onyx2D) fails linking with
#   ld64.lld: error: cannot open .../staged-overlay/usr/lib/libz.dylib
# because build-gui.sh copies ${DARLING_OVERLAY}/usr/lib/libz.dylib into its
# own staged-overlay (build-gui.sh:143-144) but nothing has ever produced
# that file in ${DARLING_OVERLAY} -- this script is that missing producer.
#
# What this script deliberately does NOT do: wrap the host FreeBSD libz.so
# the way build-native-wrappers.sh wraps libjpeg/libpng/etc. zlib is excluded
# from that mechanism by the upstream CMake tree itself (no wrap_elf(z ...)),
# so wrapping it here would silently substitute a different, unintended
# mechanism for the one the tree actually specifies -- exactly what this
# repo's CLAUDE.md forbids ("не подменять тихо").
#
# Produces (persistent side effect, like build-real-macho-tests.sh's
# Foundation.dylib install -- NOT confined to a scratch dir):
#   ${DARLING_OVERLAY}/usr/lib/libz.1.dylib   -- the real dylib, install_name
#                                                 /usr/lib/libz.1.dylib
#   ${DARLING_OVERLAY}/usr/lib/libz.dylib     -- symlink -> libz.1.dylib,
#                                                 the unversioned name
#                                                 build-gui.sh's staging step
#                                                 (build-gui.sh:143) and its
#                                                 Onyx2D link line
#                                                 (build-gui.sh:263) look for
#
# Usage: sh build-freebsd/build-libz.sh
# Run on the FreeBSD dev VM (185) -- never tested there (see above).
# Run BEFORE build-gui.sh in the build order (see docs/SPEC-native-wrappers.md
# section 4) -- build-gui.sh only COPIES an existing libz.dylib out of the
# overlay, it does not build one.
#
# Environment:
#   DARLING_BUILD_DIR — scratch build dir        (default: /var/darling-build)
#   DARLING_SRC_DIR    — root of this repository  (default: directory of this script/..)
#   DARLING_OVERLAY    — darling overlay dir      (required)
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
SRC="${DARLING_SRC_DIR:-${SCRIPT_DIR}}"
BUILD="${DARLING_BUILD_DIR:-/var/darling-build}/libz"
OVERLAY="${DARLING_OVERLAY:?set DARLING_OVERLAY to your overlay dir}"

SDK_FLAT="${BUILD}/sdk-flat"
STAGED_OVERLAY="${BUILD}/staged-overlay"
ZLIB="${SRC}/src/external/zlib/zlib"

# --- 0. Preflight, same style as build-gui.sh: fail loudly before touching
# --- anything rather than limp on to a confusing mid-build error. ---
for tool in clang ld64.lld; do
	if ! command -v "${tool}" >/dev/null 2>&1; then
		echo "FATAL: required tool '${tool}' not found in PATH." >&2
		echo "  pkg install llvm (same as the other build-freebsd/*.sh scripts)" >&2
		exit 1
	fi
done

if [ ! -f "${SRC}/tests/vendor/macosx-sdk-flat.tar.gz" ]; then
	echo "FATAL: ${SRC}/tests/vendor/macosx-sdk-flat.tar.gz missing." >&2
	echo "  See tests/vendor/README.md to regenerate it, or run" >&2
	echo "  build-real-macho-tests.sh first (it unpacks the same tarball)." >&2
	exit 1
fi

if [ ! -f "${OVERLAY}/usr/lib/libSystem.B.dylib" ]; then
	echo "FATAL: no libSystem.B.dylib in ${OVERLAY}." >&2
	echo "  Run build-freebsd/build-real-macho-tests.sh first -- it stages" >&2
	echo "  libSystem.B.dylib (and its usr/lib/system/ dependency closure)" >&2
	echo "  into \$DARLING_OVERLAY, which this script links libz against." >&2
	exit 1
fi

if [ ! -d "${ZLIB}" ]; then
	echo "FATAL: ${ZLIB} missing -- src/external/zlib submodule not checked out?" >&2
	exit 1
fi

rm -rf "${BUILD}"
mkdir -p "${SDK_FLAT}" "${STAGED_OVERLAY}/usr/lib/system"

# --- Stage the flat SDK + libSystem.B.dylib closure, same reasoning as
# --- build-real-macho-tests.sh / build-gui.sh: virtiofs can't resolve the
# --- SDK's symlinks or readlink() through 9p reliably (see
# --- tests/vendor/README.md), so everything here is real files copied out
# --- of the live overlay, never traversed in place off the 9p mount. ---
tar xzf "${SRC}/tests/vendor/macosx-sdk-flat.tar.gz" -C "${SDK_FLAT}"
cp "${OVERLAY}/usr/lib/libSystem.B.dylib" "${STAGED_OVERLAY}/usr/lib/libSystem.B.dylib"
(cd "${OVERLAY}/usr/lib/system" && find . -maxdepth 1 -type f | pax -rw "${STAGED_OVERLAY}/usr/lib/system")

# zlib is plain C with no CoreFoundation-style <Framework/Header.h>
# cross-includes (checked: only zlib.h -> <Availability.h>, gzguts.h ->
# <stdio.h>/<fcntl.h>, zutil.h -> <stddef.h>/<string.h>/<stdlib.h>, all of
# which the flat usr/include/ tree provides directly) -- so unlike
# build-gui.sh's Onyx2D this does NOT need -F<SDK>/Frameworks. The two
# `#if defined __arm__ / #include <arm/arch.h>` lines in adler32.c and
# inffast.c are dead code on this x86_64 target and never trigger.
CLANG_FLAGS="-target x86_64-apple-macos10.12 -nostdinc -D__DARWIN_ONLY_UNIX_CONFORMANCE=1"
CLANG_FLAGS="${CLANG_FLAGS} -I${SDK_FLAT}/usr/include"
# stdarg.h (zconf.h:437) and friends are compiler-provided, not part of the SDK,
# and -nostdinc drops clang's own resource include dir along with the host's.
# tests/vendor/fakesdk supplies them -- same pair of -I flags as
# build-real-macho-tests.sh:76, the guest build known to work.
CLANG_FLAGS="${CLANG_FLAGS} -I${SRC}/tests/vendor/fakesdk"
# Matches src/external/zlib/CMakeLists.txt:17-24's add_definitions() (minus
# -DVEC_OPTIMIZE, an Apple-fork-specific vectorized-adler32 knob with no
# corresponding source guard reachable from plain autoconf-style zlib.c
# without also wiring up the matching .S/.c variant files this script isn't
# attempting to locate -- omitted rather than guessed at).
CLANG_FLAGS="${CLANG_FLAGS} -D_LARGEFILE64_SOURCE=1 -DUSE_MMAP"
LD_FLAGS="-arch x86_64 -platform_version macos 10.12 10.12 -syslibroot ${STAGED_OVERLAY} -Z"

# Source list: exact match of src/external/zlib/CMakeLists.txt:26-42's
# zlib_sources (real, unmodified upstream zlib C files -- adler32/compress/
# crc32/deflate/infback/inffast/inflate/inftrees/trees/uncompr/zutil/
# gzclose/gzlib/gzread/gzwrite).
zlib_sources="adler32.c compress.c crc32.c deflate.c infback.c inffast.c \
inflate.c inftrees.c trees.c uncompr.c zutil.c gzclose.c gzlib.c gzread.c gzwrite.c"

echo "=== libz.1.dylib ==="
mkdir -p "${BUILD}/objs"
zlib_objs=""
for f in ${zlib_sources}; do
	obj="${BUILD}/objs/${f%.c}.o"
	extra=""
	# zutil.c needs -Dfdopen=fdopen -- see src/external/zlib/CMakeLists.txt:
	# 45-47's set_source_files_properties() and zlib/zutil.h:133-144's own
	# comment on why (guards against a redefinition clash under some SDKs'
	# stdio.h; harmless no-op macro otherwise, kept for parity with the
	# CMake build so this doesn't silently diverge from it).
	if [ "${f}" = "zutil.c" ]; then
		extra="-Dfdopen=fdopen"
	fi
	# shellcheck disable=SC2086
	clang ${CLANG_FLAGS} ${extra} -w -O2 -c "${ZLIB}/${f}" -o "${obj}"
	zlib_objs="${zlib_objs} ${obj}"
done

# install_name = /usr/lib/libz.1.dylib, matching src/external/zlib/
# CMakeLists.txt:49 (DYLIB_INSTALL_NAME) and cmake/use_ld64.cmake:135's
# -dylib_file mapping exactly -- this is the path the loader resolves at
# runtime, independent of where the file is staged for THIS link (see
# docs/SPEC-native-wrappers.md section 1.4 on install-name vs link-time path
# for the same -dylib_file pattern applied to the native/ ELF wrappers).
mkdir -p "${STAGED_OVERLAY}/usr/lib"
# shellcheck disable=SC2086
ld64.lld ${LD_FLAGS} -dylib \
	-install_name /usr/lib/libz.1.dylib \
	-o "${STAGED_OVERLAY}/usr/lib/libz.1.dylib" \
	${zlib_objs} \
	"${STAGED_OVERLAY}/usr/lib/libSystem.B.dylib"
ln -sf libz.1.dylib "${STAGED_OVERLAY}/usr/lib/libz.dylib"
file "${STAGED_OVERLAY}/usr/lib/libz.1.dylib"

# --- Install into the persistent overlay -- same persistent-side-effect
# --- pattern build-real-macho-tests.sh uses for Foundation.dylib, needed so
# --- build-gui.sh's own staging step (build-gui.sh:143, `cp
# --- ${OVERLAY}/usr/lib/libz.dylib ...`) finds it on its next run. ---
mkdir -p "${OVERLAY}/usr/lib"
cp "${STAGED_OVERLAY}/usr/lib/libz.1.dylib" "${OVERLAY}/usr/lib/libz.1.dylib"
# A real copy, NOT `ln -sf libz.1.dylib` -- the overlay is reached over
# virtiofs, where readlink() is broken (see tests/vendor/README.md and the
# find|pax staging in build-real-macho-tests.sh:65). A symlink here is
# invisible to build-gui.sh's `[ -f ... ]` check, which then prints
# "WARN: no libz.dylib in overlay" and links without it. Costs ~100 KB of
# duplication and removes a whole class of silent failure.
cp "${OVERLAY}/usr/lib/libz.1.dylib" "${OVERLAY}/usr/lib/libz.dylib"

echo "=== done ==="
echo "Installed: ${OVERLAY}/usr/lib/libz.1.dylib"
echo "Installed: ${OVERLAY}/usr/lib/libz.dylib -> libz.1.dylib"
echo "Next: build-freebsd/build-gui.sh should now find usr/lib/libz.dylib" \
	"when it stages \$DARLING_OVERLAY (build-gui.sh:143)."
