#!/bin/sh
# build-dyld-only.sh: build the diagnostic dyld from the salvage copy.
#
# The dyld submodule's pinned base commit is not available offline, so its
# worktree is not the September state; the LOG PATCH lives only in
# build-freebsd/dyld-salvage/. This script applies those files to the
# submodule worktree (the build input), configures and builds the
# system_loader target with cmake/ninja, and runs fixup-dylinker.sh to
# turn the linked MH_EXECUTE into an MH_DYLINKER image.
#
# Output: $DARLING_BUILD_DIR/dyld-only/src/external/dyld/dyld.patched
#   (raw linked image: .../dyld)
# Reproduce:
#   DARLING_SRC_DIR=... DARLING_BUILD_DIR=... sh build-freebsd/build-dyld-only.sh
BD=${DARLING_BUILD_DIR:-/tmp/darling-build}
SRC=${DARLING_SRC_DIR:-$(cd "$(dirname "$0")/.." && pwd)}
SALV="${SRC}/build-freebsd/dyld-salvage/src/external/dyld"
DST="${SRC}/src/external/dyld"
# Reuse the existing configured cache: a fresh top-level configure stops
# in src/external/darling-dmg on a missing `fuse` pkg-config module.
BUILDDIR="${BD}/dyld-only"

# Diagnostic rebuild: tolerate the glue.c fallbacks duplicating the static
# libs. The dyld CMakeLists overwrites CMAKE_EXE_LINKER_FLAGS with
# "${CMAKE_EXE_LINKER_FLAGS_SAVED} -nostdlib", so the flag must be seeded
# into _SAVED to survive. The first definition in the link line (glue.c.o)
# wins; acceptable for a diagnostic dyld — the live overlay is not touched.
MULDEFS="-Wl,--allow-multiple-definition"

echo "== applying salvage files to ${DST}"
cp -p "${SALV}/src/dyld2.cpp"            "${DST}/src/dyld2.cpp"
cp -p "${SALV}/src/dyldFreeBSDRebase.c"  "${DST}/src/dyldFreeBSDRebase.c"
cp -p "${SALV}/src/dyldInitialization.cpp" "${DST}/src/dyldInitialization.cpp"
cp -p "${SALV}/darling/src/sandbox-dummy.c" "${DST}/darling/src/sandbox-dummy.c"
cp -p "${SALV}/CMakeLists.txt"           "${DST}/CMakeLists.txt"

echo "== cmake configure"
cmake -G Ninja -B "${BUILDDIR}" \
	-DCMAKE_EXE_LINKER_FLAGS="${MULDEFS}" \
	-DCMAKE_EXE_LINKER_FLAGS_SAVED="${MULDEFS}" \
	"${SRC}" || exit 2

echo "== ninja system_loader"
ninja -C "${BUILDDIR}" system_loader || exit 3

RAW="${BUILDDIR}/src/external/dyld/dyld"
PATCHED="${BUILDDIR}/src/external/dyld/dyld.patched"
echo "== raw: $(file -b "${RAW}" 2>/dev/null)"

echo "== fixup-dylinker"
sh "${SRC}/build-freebsd/fixup-dylinker.sh" "${RAW}" "${PATCHED}" || exit 4
echo "== patched: $(file -b "${PATCHED}" 2>/dev/null)"
echo "== LOG PATCH strings present:"
strings "${PATCHED}" | grep -E 'sNotifyObjCMapped|registerObjCNotifiers|LOG PATCH' | head
echo "DONE patched=${PATCHED}"
