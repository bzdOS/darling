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

echo "== applying salvage files to ${DST}"
cp -p "${SALV}/src/dyld2.cpp"            "${DST}/src/dyld2.cpp"
cp -p "${SALV}/src/dyldFreeBSDRebase.c"  "${DST}/src/dyldFreeBSDRebase.c"
cp -p "${SALV}/src/dyldInitialization.cpp" "${DST}/src/dyldInitialization.cpp"
cp -p "${SALV}/darling/src/sandbox-dummy.c" "${DST}/darling/src/sandbox-dummy.c"
cp -p "${SALV}/CMakeLists.txt"           "${DST}/CMakeLists.txt"

echo "== dedup: strip glue.c fallbacks that duplicate the static libs"
# The worktree copy only (the salvage and the superproject are untouched).
# The list is the ld64.lld duplicate set; extend it per iteration.
python3 - "${DST}/src/glue.c" \
	memset __stderrp uuid_unparse_upper \
	_Block_object_assign _Block_object_dispose \
	_NSConcreteGlobalBlock _NSConcreteStackBlock <<'PY'
import re, sys
path = sys.argv[1]
syms = sys.argv[2:]
src = open(path).read()
lines = src.split("\n")
out = []
i = 0
removed = []
def is_def(l, s):
    return re.match(r'^(?:[A-Za-z_][\w \t\*]*\s+)?%s\s*[\(\[]' % re.escape(s), l) or \
           re.match(r'^[A-Za-z_][\w \t\*]*\s+%s\s*(=|\[)' % re.escape(s), l)
while i < len(lines):
    l = lines[i]
    hit = next((s for s in syms if is_def(l, s)), None)
    if not hit:
        out.append(l); i += 1; continue
    start = i
    # include a directly-preceding preprocessor guard line
    if out and re.match(r'^\s*#\s*(ifdef|ifndef|else)\b', out[-1]):
        out.pop(); start -= 0
    j = i
    if '(' in l:
        depth = 0
        while j < len(lines):
            depth += lines[j].count('{') - lines[j].count('}')
            if lines[j].rstrip() == '}' and depth == 0:
                j += 1; break
            j += 1
    else:
        while j < len(lines) and not lines[j].rstrip().endswith(';'):
            j += 1
        j += 1
    removed.append(hit)
    i = j
open(path, "w").write("\n".join(out))
print("removed:", " ".join(removed) if removed else "(none)")
PY

echo "== cmake configure"
# -U clears any cached linker flags from an earlier muldefs attempt.
cmake -G Ninja -B "${BUILDDIR}" \
	-UCMAKE_EXE_LINKER_FLAGS -UCMAKE_EXE_LINKER_FLAGS_SAVED \
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
