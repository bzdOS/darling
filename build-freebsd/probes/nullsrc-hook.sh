#!/bin/sh
# nullsrc-hook.sh - build probes/nullsrc-hook.c into $DARLING_BUILD_DIR/nullsrc/nullsrc.dylib (prints the path).
# Env: DARLING_SRC_DIR, DARLING_BUILD_DIR, DARLING_OVERLAY
set -e
SRC="${DARLING_SRC_DIR:?}"; BD="${DARLING_BUILD_DIR:?}"; OD="${DARLING_OVERLAY:?}"
export PATH="/usr/local/bin:/usr/local/sbin:/usr/bin:/bin:/sbin:/usr/sbin"
SDK_FLAT="$BD/bundle-principal-class-test/sdk-flat"; W="$BD/nullsrc"; mkdir -p "$W"
B=$(mktemp -d "$BD/nullsrc.XXXXXX") || { echo "FATAL: mktemp workdir failed for nullsrc" >&2; exit 1; }
cleanup() { rm -rf "$B"; }
trap cleanup EXIT INT TERM HUP
clang -target x86_64-apple-macos10.12 -nostdinc -D__DARWIN_ONLY_UNIX_CONFORMANCE=1 -O1 -w \
	-I"$SRC/tests/vendor/fakesdk" -I"$SDK_FLAT/usr/include" -c "$SRC/build-freebsd/probes/nullsrc-hook.c" -o "$B/hook.o"
ld64.lld -arch x86_64 -platform_version macos 10.12 10.12 -syslibroot "$OD" -dylib -install_name /nullsrc/nullsrc.dylib \
	-o "$W/nullsrc.dylib" "$B/hook.o" "$OD/usr/lib/libc++abi.dylib" "$OD/usr/lib/libSystem.B.dylib" 2>"$B/link.err" || { tail -5 "$B/link.err"; exit 1; }
echo "$W/nullsrc.dylib"
