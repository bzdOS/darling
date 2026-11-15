#!/bin/sh
# rebuild-dtape-object.sh - recompile ONE duct-tape source into the existing
# libdtape.a (build-dtape.sh recompiles all of it, which takes much longer), using
# the compile flags that script defines.
#
# Usage: sh build-freebsd/rebuild-dtape-object.sh <path under duct-tape/src, e.g. src/memory.c>
#   Env: DARLING_SRC_DIR, DARLING_BUILD_DIR
# The old archive is kept once as libdtape.a.pre-unwind. Run build-darlingserver.sh
# afterwards to relink the server.
set -e
SRCDIR="${DARLING_SRC_DIR:?}"
BD="${DARLING_BUILD_DIR:?}"
export PATH="/usr/local/bin:/usr/local/sbin:/usr/bin:/bin:/sbin:/usr/sbin"
ROOT="${SRCDIR}/src"
DS=${ROOT}/external/darlingserver
DT=${DS}/duct-tape
COMPAT=${ROOT}/startup/compat-includes
BUILD="${BD}/dtape"
DTBUILD=${BUILD}/dtape-build
SCRIPT="${SRCDIR}/build-freebsd/build-dtape.sh"
# the flag block of build-dtape.sh, evaluated with the same variables
eval "$(sed -n '/^DTFLAGS="/,/^DTCXXFLAGS=/p' "${SCRIPT}" | sed '$d')"
SRC="${DT}/$1"
[ -f "${SRC}" ] || { echo "missing ${SRC}" >&2; exit 1; }
OBJ="${DTBUILD}/objs/$(echo "${SRC}" | tr '/' '_' | tr ' ' '_').o"
[ -e "${OBJ}" ] || { echo "no existing object ${OBJ}: not a source of this archive" >&2; exit 1; }
# shellcheck disable=SC2086
clang ${DTFLAGS} -c "${SRC}" -o "${OBJ}.new"
[ -e "${DTBUILD}/libdtape.a.pre-unwind" ] || cp -p "${DTBUILD}/libdtape.a" "${DTBUILD}/libdtape.a.pre-unwind"
mv "${OBJ}.new" "${OBJ}"
ar r "${DTBUILD}/libdtape.a" "${OBJ}"
ranlib "${DTBUILD}/libdtape.a"
echo "replaced $(basename "${OBJ}") in ${DTBUILD}/libdtape.a"
