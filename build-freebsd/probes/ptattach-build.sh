#!/bin/sh
# ptattach-build.sh - build probes/ptattach-probe.c into tests/ptattach-probe-macho.
# Usage: sh build-freebsd/probes/ptattach-build.sh   Env: DARLING_SRC_DIR, DARLING_BUILD_DIR, DARLING_OVERLAY.
set -e
SRC="${DARLING_SRC_DIR:?}"; BD="${DARLING_BUILD_DIR:?}"; OD="${DARLING_OVERLAY:?}"
export PATH="/usr/local/bin:/usr/local/sbin:/usr/bin:/bin:/sbin:/usr/sbin"
OUT="${SRC}/tests/ptattach-probe-macho"; W="${BD}/ptattach-probe"; SDK_FLAT="${BD}/bundle-principal-class-test/sdk-flat"
[ -d "${SDK_FLAT}/usr/include" ] || { echo "FATAL: no flattened SDK at ${SDK_FLAT}" >&2; exit 1; }
mkdir -p "$W"
nice -n 19 clang -target x86_64-apple-macos10.12 -nostdinc -D__DARWIN_ONLY_UNIX_CONFORMANCE=1 -O1 -w \
	-I"${SRC}/tests/vendor/fakesdk" -I"${SDK_FLAT}/usr/include" -c "${SRC}/build-freebsd/probes/ptattach-probe.c" -o "$W/probe.o"
ld64.lld -arch x86_64 -platform_version macos 10.12 10.12 -syslibroot "${OD}" -Z -o "${OUT}" "$W/probe.o" "${OD}/usr/lib/libSystem.B.dylib"
chmod 755 "${OUT}"
echo "binary: ${OUT}"
