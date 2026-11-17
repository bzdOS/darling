#!/bin/sh
# suspend-build.sh - build probes/suspend-<kind>-probe.c (SR_KIND = rwlock | mutex | cv) into tests/suspend-<kind>-probe-macho.
# Usage: SR_KIND=mutex sh build-freebsd/probes/suspend-build.sh   Env: DARLING_SRC_DIR, DARLING_BUILD_DIR, DARLING_OVERLAY.
set -e
SRC="${DARLING_SRC_DIR:?}"; BD="${DARLING_BUILD_DIR:?}"; OD="${DARLING_OVERLAY:?}"; K="${SR_KIND:?}"
export PATH="/usr/local/bin:/usr/local/sbin:/usr/bin:/bin:/sbin:/usr/sbin"
OUT="${SRC}/tests/suspend-${K}-probe-macho"; W="${BD}/suspend-${K}-probe"; SDK_FLAT="${BD}/bundle-principal-class-test/sdk-flat"
[ -d "${SDK_FLAT}/usr/include" ] || { echo "FATAL: no flattened SDK at ${SDK_FLAT}" >&2; exit 1; }
mkdir -p "$W"
nice -n 19 clang -target x86_64-apple-macos10.12 -nostdinc -D__DARWIN_ONLY_UNIX_CONFORMANCE=1 -O1 -w \
	-I"${SRC}/tests/vendor/fakesdk" -I"${SDK_FLAT}/usr/include" -c "${SRC}/build-freebsd/probes/suspend-${K}-probe.c" -o "$W/probe.o"
ld64.lld -arch x86_64 -platform_version macos 10.12 10.12 -syslibroot "${OD}" -Z -o "${OUT}" "$W/probe.o" "${OD}/usr/lib/libSystem.B.dylib"
chmod 755 "${OUT}"
echo "binary: ${OUT}"
