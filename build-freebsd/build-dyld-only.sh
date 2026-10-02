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
BUILDDIR="${BD}/dyld-only"

echo "== applying salvage files to ${DST}"
cp -p "${SALV}/src/dyld2.cpp"            "${DST}/src/dyld2.cpp"
cp -p "${SALV}/src/dyldFreeBSDRebase.c"  "${DST}/src/dyldFreeBSDRebase.c"
cp -p "${SALV}/src/dyldInitialization.cpp" "${DST}/src/dyldInitialization.cpp"
cp -p "${SALV}/darling/src/sandbox-dummy.c" "${DST}/darling/src/sandbox-dummy.c"
cp -p "${SALV}/CMakeLists.txt"           "${DST}/CMakeLists.txt"

echo "== cmake configure"
cmake -G Ninja -B "${BUILDDIR}" "${DST}" || exit 2

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
