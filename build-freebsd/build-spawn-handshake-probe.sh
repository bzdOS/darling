#!/bin/sh
# build-spawn-handshake-probe.sh: tests/spawn-handshake-probe-macho (spawn + bootstrap handshake, EINTR count per call in the child).
# Uses the sdk-flat and staged-overlay left by build-real-macho-tests.sh / build-foundation-only.sh.
set -e
SRC="${DARLING_SRC_DIR:?}"; B="${DARLING_BUILD_DIR:?}/real-macho"
[ -f "$B/staged-overlay/usr/lib/libSystem.B.dylib" ] || { echo "FATAL: no $B/staged-overlay (run build-real-macho-tests.sh)"; exit 1; }
clang -target x86_64-apple-macos10.12 -nostdinc -D__DARWIN_ONLY_UNIX_CONFORMANCE=1 -I"$B/sdk-flat/usr/include" \
	-I"$SRC/tests/vendor/fakesdk" -O1 -w -c "$SRC/tests/spawn-handshake-probe.c" -o "$B/spawn-handshake-probe.o"
ld64.lld -arch x86_64 -platform_version macos 10.12 10.12 -syslibroot "$B/staged-overlay" -Z \
	-o "$SRC/tests/spawn-handshake-probe-macho" "$B/spawn-handshake-probe.o" "$B/staged-overlay/usr/lib/libSystem.B.dylib"
chmod 755 "$SRC/tests/spawn-handshake-probe-macho"
echo "built $SRC/tests/spawn-handshake-probe-macho"
