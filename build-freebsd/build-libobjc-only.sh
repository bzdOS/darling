#!/bin/sh
# build-libobjc-only.sh - narrow x86_64 rebuild of libobjc.A.dylib: compile the objc4
# runtime objects and link them against the installed overlay dylibs (the tree's
# firstpass dylibs are not kept). The i386 slice is not rebuilt; the installed fat
# file's i386 slice is carried over.
#
# Usage: sh build-freebsd/build-libobjc-only.sh
#   Env: DARLING_SRC_DIR, DARLING_BUILD_DIR, DARLING_OVERLAY; OBJC_WORK (default
#   $DARLING_BUILD_DIR/objc-only). Output: $OBJC_WORK/libobjc.A.dylib (fat).
set -e
SRC="${DARLING_SRC_DIR:?}"
BD="${DARLING_BUILD_DIR:?}"
OD="${DARLING_OVERLAY:?}"
W="${OBJC_WORK:-$BD/objc-only}"
export PATH="/usr/local/bin:/usr/local/sbin:/usr/bin:/bin:/sbin:/usr/sbin"
export DARLING_BUILD_DIR="$BD"
mkdir -p "$W"
( cd "$BD" && ninja -t commands src/external/objc4/runtime/libobjc.A.dylib > "$W/all-cmds.txt" )
LD64=""
for c in "$SRC/build-host-tools/ld64/x86_64-apple-darwin20-ld" "$BD/dyld-only/src/external/cctools-port/cctools/ld64/src/x86_64-apple-darwin20-ld" "$BD/src/external/cctools-port/cctools/ld64/src/x86_64-apple-darwin20-ld"; do
	[ -x "$c" ] && { LD64="$c"; break; }
done
export LD64
python3 "$SRC/build-freebsd/objc-narrow.py" "$W/all-cmds.txt" "$W" "$OD"
[ -n "$OBJC_SKIP_COMPILE" ] || sh "$W/compile.sh"
sh "$W/link.sh"
llvm-lipo "$OD/usr/lib/libobjc.A.dylib" -thin i386 -output "$W/libobjc.A.dylib.i386"
llvm-lipo -create -output "$W/libobjc.A.dylib" "$W/libobjc.A.dylib.x86_64" "$W/libobjc.A.dylib.i386"
llvm-lipo -info "$W/libobjc.A.dylib"
