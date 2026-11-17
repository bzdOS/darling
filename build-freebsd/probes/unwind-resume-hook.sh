#!/bin/sh
# unwind-resume-hook.sh - build probes/unwind-resume-hook.c into $DARLING_BUILD_DIR/unwind-resume/unwind-resume.dylib (prints the path).
# Env: DARLING_SRC_DIR, DARLING_BUILD_DIR, DARLING_OVERLAY
set -e
SRC="${DARLING_SRC_DIR:?}"; BD="${DARLING_BUILD_DIR:?}"; OD="${DARLING_OVERLAY:?}"
export PATH="/usr/local/bin:/usr/local/sbin:/usr/bin:/bin:/sbin:/usr/sbin"
SDK_FLAT="$BD/bundle-principal-class-test/sdk-flat"; W="$BD/unwind-resume"; mkdir -p "$W"
clang -target x86_64-apple-macos10.12 -nostdinc -D__DARWIN_ONLY_UNIX_CONFORMANCE=1 -O1 -w \
	-I"$SRC/tests/vendor/fakesdk" -I"$SDK_FLAT/usr/include" -c "$SRC/build-freebsd/probes/unwind-resume-hook.c" -o "$W/hook.o"
ld64.lld -arch x86_64 -platform_version macos 10.12 10.12 -syslibroot "$OD" -dylib -install_name /unwindresume/unwind-resume.dylib \
	-o "$W/unwind-resume.dylib" "$W/hook.o" "$OD/usr/lib/libc++abi.dylib" "$OD/usr/lib/libSystem.B.dylib" 2>"$W/link.err" || { tail -5 "$W/link.err"; exit 1; }
echo "$W/unwind-resume.dylib"
