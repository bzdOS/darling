#!/bin/sh
# build-opendirectory-stub.sh - rebuild OpenDirectory.framework's binary with real
# ObjC root classes (ODSession, ODNode, ODQuery, ODRecord) and nil constants.
# The previous stand-in exported the class symbols as 16-byte code stubs; a
# message to ODSession then ran through libobjc's cache lookup on code bytes and
# crashed in objc_msgSend.
#
# Usage: DARLING_SRC_DIR=... DARLING_OVERLAY=... DARLING_BUILD_DIR=... sh build-freebsd/build-opendirectory-stub.sh
# The old binary is kept as OpenDirectory.pre-odsession next to the new one.
set -e
SRC="${DARLING_SRC_DIR:?}"; OD="${DARLING_OVERLAY:?}"; BD="${DARLING_BUILD_DIR:?}"
W="$BD/opendirectory-stub"
DIR="$OD/System/Library/Frameworks/OpenDirectory.framework/Versions/A"
mkdir -p "$W"
cat > "$W/empty_cache.s" <<'ASM'
	.section	__DATA,__data
	.globl	__objc_empty_cache
	.p2align	3
__objc_empty_cache:
	.quad	0
	.quad	0
ASM
clang -target x86_64-apple-macos10.12 -c "$W/empty_cache.s" -o "$W/empty_cache.o"
clang -target x86_64-apple-macos10.12 -fobjc-runtime=macosx-10.12 -c \
  "$SRC/build-freebsd/stub-frameworks/OpenDirectory-stub.m" -o "$W/od.m.o"
clang -target x86_64-apple-macos10.12 -c \
  "$SRC/build-freebsd/stub-frameworks/OpenDirectory-stub-data.c" -o "$W/od.c.o"
ld64.lld -dylib -arch x86_64 -platform_version macos 10.12 10.12 \
  -compatibility_version 1.0.0 -current_version 1.0.0 \
  -install_name /System/Library/Frameworks/OpenDirectory.framework/Versions/A/OpenDirectory \
  -o "$W/OpenDirectory" "$W/od.m.o" "$W/od.c.o" "$W/empty_cache.o"
llvm-nm -m "$W/OpenDirectory" | grep -E "OBJC_CLASS|kOD"
[ -e "$DIR/OpenDirectory.pre-odsession" ] || cp -p "$DIR/OpenDirectory" "$DIR/OpenDirectory.pre-odsession"
cp "$W/OpenDirectory" "$DIR/OpenDirectory"
md5 "$DIR/OpenDirectory" "$DIR/OpenDirectory.pre-odsession"
