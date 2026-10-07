#!/bin/sh
# Control #116: restore the SDK base integer typedefs so a libsystem_kernel
# object can compile.
#
# The object command for file_handle.c.o failed on SDK sys/_types.h:56
# (`unknown type name '__int64_t'`). Two things are behind it:
#  1. xnu/bsd/machine/i386/_types.h was absent (machine/_types.h includes it);
#     a good copy exists at $STAGE/sdkflat/usr/include/i386/_types.h.
#  2. build-host-tools/freebsd_mig_compat.h (force-included by the toolchain)
#     defines _BSD_I386__TYPES_H_, deliberately skipping Darwin's i386/_types.h,
#     but only defines the __darwin_* types — not the __intN_t typedefs.
#
# This script restores (1) and adds the base typedefs for (2).
#
# Stop condition (Control #116): after this, 3 NEW missing SDK typedefs appear
# (__darwin_ptrdiff_t, __darwin_wchar_t, __darwin_wint_t) plus an i386/types.h
# redefinition — that is SDK repair, not this step; see CFT-DLOPEN.md #116.
set -e
SRC="${DARLING_SRC_DIR:?set DARLING_SRC_DIR}"
STAGE="${DARLING_STAGE:?set DARLING_STAGE to the stage tree}"

# 1. restore i386/_types.h from the stage copy
D="$SRC/src/external/xnu/bsd/machine/i386"
mkdir -p "$D"
cp "$STAGE/sdkflat/usr/include/i386/_types.h" "$D/_types.h"
echo "restored $D/_types.h"

# 2. base integer typedefs in the force-included shim
F="$SRC/../build-host-tools/freebsd_mig_compat.h"
[ -f "$F" ] || { echo "no $F — set DARLING_SRC_DIR so ../build-host-tools resolves" >&2; exit 1; }
if ! grep -q "typedef long long          __int64_t;" "$F"; then
    python3 - "$F" << 'PY'
import sys
p=sys.argv[1]; s=open(p).read()
anchor="#ifndef _BSD_I386__TYPES_H_\n#define _BSD_I386__TYPES_H_\n#endif\n"
add=anchor+"""
typedef signed char        __int8_t;
typedef unsigned char      __uint8_t;
typedef short              __int16_t;
typedef unsigned short     __uint16_t;
typedef int                __int32_t;
typedef unsigned int       __uint32_t;
typedef long long          __int64_t;
typedef unsigned long long __uint64_t;
"""
open(p,"w").write(s.replace(anchor,add))
PY
    echo "patched $F"
else
    echo "$F already patched"
fi
