#!/bin/sh
# v5store-hook.sh - build probes/v5store-hook.c into $DARLING_BUILD_DIR/v5store/v5store.dylib (prints the path).
# Env: DARLING_SRC_DIR, DARLING_BUILD_DIR, DARLING_OVERLAY
set -e
SRC="${DARLING_SRC_DIR:?}"; BD="${DARLING_BUILD_DIR:?}"; OD="${DARLING_OVERLAY:?}"
export PATH="/usr/local/bin:/usr/local/sbin:/usr/bin:/bin:/sbin:/usr/sbin"
SDK_FLAT="$BD/bundle-principal-class-test/sdk-flat"; W="$BD/v5store"; mkdir -p "$W"
B=$(mktemp -d "$BD/v5store.XXXXXX") || { echo "FATAL: mktemp workdir failed for v5store" >&2; exit 1; }
cleanup() { rm -rf "$B"; }
trap cleanup EXIT INT TERM HUP
clang -target x86_64-apple-macos10.12 -nostdinc -D__DARWIN_ONLY_UNIX_CONFORMANCE=1 -O1 -w \
	-I"$SRC/tests/vendor/fakesdk" -I"$SDK_FLAT/usr/include" -c "$SRC/build-freebsd/probes/v5store-hook.c" -o "$B/hook.o"
ld64.lld -arch x86_64 -platform_version macos 10.12 10.12 -syslibroot "$OD" -dylib -install_name /v5store/v5store.dylib \
	-o "$W/v5store.dylib" "$B/hook.o" "$OD/usr/lib/libc++abi.dylib" "$OD/usr/lib/libSystem.B.dylib" 2>"$B/link.err" || { tail -5 "$B/link.err"; exit 1; }
echo "$W/v5store.dylib"
