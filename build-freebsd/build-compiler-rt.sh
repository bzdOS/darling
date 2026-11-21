#!/bin/sh
# Relink usr/lib/system/libcompiler_rt.dylib (x86_64) from the compiler_rt_64bit objects with ld64.lld and stage it
# into the overlay. Used because the CMake link rule for this target runs the host cc. The object list includes
# atomic.c (generic __atomic_* libcalls, see patches/compiler-rt-builtins-atomic.patch).
# Env: DARLING_BUILD_DIR, DARLING_OVERLAY.
set -eu
BUILD="${DARLING_BUILD_DIR:?set DARLING_BUILD_DIR}"
OVERLAY="${DARLING_OVERLAY:?set DARLING_OVERLAY}"
OBJDIR="${BUILD}/src/external/compiler-rt/lib/builtins/CMakeFiles/compiler_rt_64bit.dir"
DEST="${OVERLAY}/usr/lib/system/libcompiler_rt.dylib"
SYS="${OVERLAY}/usr/lib/system"

for tool in ld64.lld llvm-nm md5 ninja; do
	command -v "${tool}" >/dev/null 2>&1 || { echo "FATAL: required tool '${tool}' not found in PATH." >&2; exit 1; }
done
[ -f "${DEST}" ] || { echo "FATAL: no current ${DEST} (refusing to create a new library)." >&2; exit 1; }

# Compile only the atomic object through ninja; nothing else of the tree is rebuilt here.
(cd "${BUILD}" && ninja src/external/compiler-rt/lib/builtins/CMakeFiles/compiler_rt_64bit.dir/atomic.c.o)

# Per source the x86_64 slice (<src>.c.o.x86_64.o) when the object is universal, else the thin <src>.c.o.
objs=""
for o in "${OBJDIR}"/*.c.o "${OBJDIR}"/x86_64/*.c.o "${OBJDIR}"/x86_64/*.S.o; do
	if [ -f "${o}.x86_64.o" ]; then objs="${objs} ${o}.x86_64.o"; else objs="${objs} ${o}"; fi
done

NEW="${BUILD}/libcompiler_rt.new"
# shellcheck disable=SC2086
ld64.lld -arch x86_64 -platform_version macos 10.10 10.10 -syslibroot "${OVERLAY}" -Z -dylib \
	-install_name /usr/lib/system/libcompiler_rt.dylib \
	-compatibility_version 1.0.0 -current_version 1.0.0 \
	-o "${NEW}" ${objs} \
	"${SYS}/libunwind.dylib" "${SYS}/libsystem_pthread.dylib" "${SYS}/libsystem_m.dylib" \
	"${SYS}/libsystem_malloc.dylib" "${SYS}/libsystem_c.dylib" "${SYS}/libsystem_kernel.dylib" \
	"${SYS}/libdyld.dylib" "${SYS}/libsystem_platform.dylib"

# Every export of the current library must survive the relink.
cmp_syms() {
	{ llvm-nm -gUj "${DEST}" | sed 's/^/O /'; llvm-nm -gUj "${NEW}" | sed 's/^/N /'; } |
		awk -v want="$1" '{ s[$2] = s[$2] $1 } END { n = 0; for (k in s) if (s[k] == want) n++; print n }'
}
lost=$(cmp_syms O)
added=$(cmp_syms N)
echo "EXPORTS lost=${lost} added=${added}"
[ "${lost}" = 0 ] || { echo "FATAL: relinked library drops ${lost} exports (left at ${NEW})." >&2; exit 1; }

BUILT_MD5=$(md5 -q "${NEW}")
echo "BUILT libcompiler_rt md5=${BUILT_MD5}"
mv -f "${NEW}" "${DEST}"
STAGED_MD5=$(md5 -q "${DEST}")
echo "STAGED ${DEST} md5=${STAGED_MD5}"
[ "${BUILT_MD5}" = "${STAGED_MD5}" ] || { echo "FATAL: staged md5 differs from the built one." >&2; exit 1; }
echo "BUILD-COMPILER-RT done md5=${STAGED_MD5}"
