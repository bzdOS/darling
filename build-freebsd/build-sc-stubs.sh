#!/bin/sh
# build-sc-stubs.sh - build usr/lib/SystemConfigurationExtras.dylib from stub-frameworks/SystemConfigurationExtras.c
# (sane non-NULL SystemConfiguration answers without configd), install it into the overlay, then rebuild
# suppl.dylib so that it re-exports it instead of shadowing the same names with xor/ret stubs, and install that.
# Usage: DARLING_SRC_DIR= DARLING_OVERLAY= DARLING_BUILD_DIR= sh build-freebsd/build-sc-stubs.sh
set -e
SRC="${DARLING_SRC_DIR:?}"; OD="${DARLING_OVERLAY:?}"; BD="${DARLING_BUILD_DIR:?}"
export PATH="/usr/local/bin:/usr/local/sbin:/usr/bin:/bin:/sbin:/usr/sbin"
W="$BD/sc-stubs"; mkdir -p "$W"
clang -target x86_64-apple-macos10.12 -O1 -w -fconstant-cfstrings -c "$SRC/build-freebsd/stub-frameworks/SystemConfigurationExtras.c" -o "$W/sc.o"
ld64.lld -dylib -arch x86_64 -platform_version macos 10.12 10.12 -undefined dynamic_lookup -syslibroot "$OD" \
  -install_name /usr/lib/SystemConfigurationExtras.dylib -current_version 1.0.0 -compatibility_version 1.0.0 \
  -o "$W/SystemConfigurationExtras.dylib" "$W/sc.o" \
  "$OD/System/Library/Frameworks/CoreFoundation.framework/Versions/A/CoreFoundation" "$OD/usr/lib/libSystem.B.dylib" 2>/dev/null
cp "$W/SystemConfigurationExtras.dylib" "$OD/usr/lib/SystemConfigurationExtras.dylib"
mkdir -p "$BD/stub-data" && cp "$W/SystemConfigurationExtras.dylib" "$BD/stub-data/SystemConfigurationExtras_dylib"
GEN_EXTRA_PROVIDERS="$OD/usr/lib/SystemConfigurationExtras.dylib" sh "$SRC/build-freebsd/build-suppl-stubs.sh" "$BD/suppl-stubs"
cp "$BD/suppl-stubs/suppl.dylib" "$OD/usr/lib/suppl.dylib"
sudo -n cp -p "$OD/usr/lib/suppl.dylib" /tmp/darling-local-overlay/usr/lib/suppl.dylib 2>/dev/null || true
echo "installed SystemConfigurationExtras.dylib and suppl.dylib into $OD/usr/lib (restage via stage-gate.sh)"
