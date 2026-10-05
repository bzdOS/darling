#!/bin/sh
# Rebuild libcorecrypto.dylib with ccchacha20 and poly1305-трио exported.
# Usage: sh build-freebsd/build-libcorecrypto-export.sh
set -e

export PATH=/usr/local/bin:/usr/local/sbin:/usr/bin:/bin:/sbin:/usr/sbin

: ${DARLING_BUILD_DIR:?}
: ${DARLING_OVERLAY:?}
BUILD_DIR=$DARLING_BUILD_DIR
OVERLAY=$DARLING_OVERLAY

STATIC_LIB="$BUILD_DIR/dyld-only/src/external/corecrypto/libcorecrypto_static.a"
if [ ! -f "$STATIC_LIB" ]; then
    echo "Error: $STATIC_LIB not found. Build corecrypto first."
    exit 1
fi

# Generate export list from static library
llvm-nm -g "$STATIC_LIB" 2>/dev/null | awk '{print $3}' | sort > /tmp/corecrypto_all_syms.txt

# Build dylib with all symbols exported
ld64.lld -dylib -arch x86_64 -platform_version macos 10.12 10.12 \
    -install_name /usr/lib/system/libcorecrypto.dylib \
    -current_version 1.0.0 -compatibility_version 1.0.0 \
    -exported_symbols_list /tmp/corecrypto_all_syms.txt \
    -undefined dynamic_lookup \
    -o /tmp/libcorecrypto_export.dylib "$STATIC_LIB"

# Verify exports
llvm-nm -gU /tmp/libcorecrypto_export.dylib | grep ccchacha

# Install to overlay
cp /tmp/libcorecrypto_export.dylib "$OVERLAY/usr/lib/system/libcorecrypto.dylib"
echo "Installed to $OVERLAY/usr/lib/system/libcorecrypto.dylib"
