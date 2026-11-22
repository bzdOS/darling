#!/bin/sh
# throw-trace-hook.sh - build the env-gated throw logger (throw-trace-hook.c) into $DARLING_BUILD_DIR/throw-trace/throw-trace.dylib.
# Use it for one run with DYLD_INSERT_LIBRARIES=<guest path of the dylib> DARLING_THROW_TRACE=1; the guest then prints a
# "THROW-TRACE objc|cxx ..." line (class, reason, return address) for each throw. For Chrome:
#   CHROME_SR_COPY=<host dylib>=/throwtrace/throw-trace.dylib CHROME_SR_EXTRA="DYLD_INSERT_LIBRARIES=/throwtrace/throw-trace.dylib DARLING_THROW_TRACE=1"
#   sh build-freebsd/chrome-sr-batch.sh <prefix> 1
# Usage: sh build-freebsd/throw-trace-hook.sh      Env: DARLING_SRC_DIR, DARLING_BUILD_DIR, DARLING_OVERLAY
set -e
SRC="${DARLING_SRC_DIR:?}"; BD="${DARLING_BUILD_DIR:?}"; OD="${DARLING_OVERLAY:?}"
export PATH="/usr/local/bin:/usr/local/sbin:/usr/bin:/bin:/sbin:/usr/sbin"
SDK_FLAT="$BD/bundle-principal-class-test/sdk-flat"; W="$BD/throw-trace"; mkdir -p "$W"
B=$(mktemp -d "$BD/throw-trace.XXXXXX") || { echo "FATAL: mktemp workdir failed for throw-trace" >&2; exit 1; }
cleanup() { rm -rf "$B"; }
trap cleanup EXIT INT TERM HUP
clang -target x86_64-apple-macos10.12 -nostdinc -D__DARWIN_ONLY_UNIX_CONFORMANCE=1 -O1 -w \
	-I"$SRC/tests/vendor/fakesdk" -I"$SDK_FLAT/usr/include" -c "$SRC/tests/src/throw-trace-hook.c" -o "$B/hook.o"
ld64.lld -arch x86_64 -platform_version macos 10.12 10.12 -syslibroot "$OD" -dylib -install_name /throwtrace/throw-trace.dylib \
	-o "$W/throw-trace.dylib" "$B/hook.o" "$OD/usr/lib/libobjc.A.dylib" "$OD/usr/lib/libc++abi.dylib" "$OD/usr/lib/libSystem.B.dylib" 2>"$B/link.err" || { tail -3 "$B/link.err"; exit 1; }
echo "$W/throw-trace.dylib"
