#!/bin/sh
# build-bootstrap-shim.sh - build usr/lib/darling-bootstrap.dylib from stub-frameworks/BootstrapShim.c (in-process
# bootstrap server for guests that start without launchd) and install it into the overlay and the staged copy.
# launch-dynamic inserts it (DYLD_INSERT_LIBRARIES) for chrome-macho and bootstrap-probe-macho.
# Usage: DARLING_SRC_DIR= DARLING_OVERLAY= DARLING_BUILD_DIR= sh build-freebsd/build-bootstrap-shim.sh
set -e
SRC="${DARLING_SRC_DIR:?}"; OD="${DARLING_OVERLAY:?}"; BD="${DARLING_BUILD_DIR:?}"
export PATH="/usr/local/bin:/usr/local/sbin:/usr/bin:/bin:/sbin:/usr/sbin"
SDK_FLAT="$BD/bundle-principal-class-test/sdk-flat"; W="$BD/bootstrap-shim"; mkdir -p "$W"
clang -target x86_64-apple-macos10.12 -nostdinc -D__DARWIN_ONLY_UNIX_CONFORMANCE=1 -O1 -w \
	-I"$SRC/tests/vendor/fakesdk" -I"$SDK_FLAT/usr/include" -c "$SRC/build-freebsd/stub-frameworks/BootstrapShim.c" -o "$W/shim.o"
ld64.lld -dylib -arch x86_64 -platform_version macos 10.12 10.12 -syslibroot "$OD" \
	-install_name /usr/lib/darling-bootstrap.dylib -current_version 1.0.0 -compatibility_version 1.0.0 \
	-o "$W/darling-bootstrap.dylib" "$W/shim.o" "$OD/usr/lib/libSystem.B.dylib" 2>/dev/null
cp "$W/darling-bootstrap.dylib" "$OD/usr/lib/darling-bootstrap.dylib"
sudo -n cp -p "$OD/usr/lib/darling-bootstrap.dylib" /tmp/darling-local-overlay/usr/lib/darling-bootstrap.dylib 2>/dev/null || true
echo "installed darling-bootstrap.dylib into $OD/usr/lib"
