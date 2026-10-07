#!/bin/sh
# Control #120: close the non-type link conflicts of libsystem_kernel in one pass.
#
# Inventory (narrow-build-kernel.sh, full log):
#   conflicting types:
#     memcpy/strlen/strncpy  SDK string.h:72/86/89 (size_t) vs
#                            wrappers bind.c:11 / connect.c:12 / statfs.c:20-21 /
#                            readlink.c:9 and dserver-rpc-defs.h:35 (__SIZE_TYPE__)
#     proc_regionfilename    xnu libproc.h:100 (Apple: uint64_t/void*/uint32_t) vs
#                            the shim freebsd_mig_compat.h:141
#     abort_with_payload     SDK sys/reason.h:183 vs the shim freebsd_mig_compat.h
#   missing header:          IOKit/IOReturn.h (err_iokit.sub:31)
#
# Root of the size_t conflict: the shim defines __darwin_size_t as a fixed
# unsigned long/int, while i386/_types.h (blocked) defines it as __SIZE_TYPE__.
# For i386 clang's __SIZE_TYPE__ is `long unsigned int`, so the shim's `unsigned
# int` diverges from the wrappers' __SIZE_TYPE__ parameter. Same class for
# __darwin_ptrdiff_t/__darwin_wchar_t. Align the shim to the builtins.
#
# proc_regionfilename/abort_with_payload: the SDK declares them; the shim's own
# declarations (needed only for the host tools that do not see the SDK) clash.
# They are removed from the shim.
#
# IOKit/IOReturn.h: minimal, only the three kIOReturn* constants err_iokit.sub
# uses; added under build-host-tools and put on the include path by
# narrow-build-kernel.sh.
set -e
SRC="${DARLING_SRC_DIR:?set DARLING_SRC_DIR}"
SHIM="$SRC/build-host-tools/freebsd_mig_compat.h"

python3 - "$SHIM" << 'PY'
import sys, re
p = sys.argv[1]
s = open(p).read()

def repl(old, new, label):
    global s
    if old not in s:
        print("WARN: pattern not found:", label)
        return
    s = s.replace(old, new, 1)
    print("patched:", label)

repl("""#ifndef __darwin_ptrdiff_t
#ifdef __LP64__
typedef long                __darwin_ptrdiff_t;
#else
typedef int                 __darwin_ptrdiff_t;
#endif
#endif""",
"""#ifndef __darwin_ptrdiff_t
typedef __PTRDIFF_TYPE__    __darwin_ptrdiff_t;
#endif""", "__darwin_ptrdiff_t -> __PTRDIFF_TYPE__")

repl("""#ifndef __darwin_size_t
#ifdef __LP64__
typedef unsigned long       __darwin_size_t;
#else
typedef unsigned int        __darwin_size_t;
#endif
#endif""",
"""#ifndef __darwin_size_t
typedef __SIZE_TYPE__       __darwin_size_t;
#endif""", "__darwin_size_t -> __SIZE_TYPE__")

repl("""#ifndef __darwin_wchar_t
typedef int                 __darwin_wchar_t;
#endif""",
"""#ifndef __darwin_wchar_t
typedef __WCHAR_TYPE__      __darwin_wchar_t;
#endif""", "__darwin_wchar_t -> __WCHAR_TYPE__")

# drop the shim's own proc_regionfilename/abort_with_payload declarations
m = re.search(r"/\* proc_regionfilename and abort_with_payload.*?\n#endif\n", s, re.S)
if m:
    s = s[:m.start()] + s[m.end():]
    print("removed: proc_regionfilename/abort_with_payload block")
else:
    print("WARN: proc_regionfilename block not found")

open(p, "w").write(s)
PY

# minimal IOKit headers for err_iokit.sub — the exact constants it uses
mkdir -p "$SRC/build-host-tools/IOKit/usb" "$SRC/build-host-tools/IOKit/firewire"
SUB="$SRC/src/external/xnu/darling/src/libsystem_kernel/libsyscall/mach/err_iokit.sub"
python3 - "$SUB" "$SRC/build-host-tools/IOKit/IOReturn.h" << 'PY'
import re, sys
s = open(sys.argv[1]).read()
ids = sorted(set(re.findall(r'\b(kIO[A-Za-z0-9]+|sub_iokit_[a-z_]+)\b', s)))
real = {'kIOReturnInvalid':'0xe0000001','kIOReturnError':'0xe00002bc',
        'kIOReturnNotFound':'0xe00002ed'}
out = ["/* Control #120: minimal IOKit constants for err_iokit.sub (generated). */",
       "#ifndef _IOKIT_IORETURN_H", "#define _IOKIT_IORETURN_H"]
for i in ids:
    out.append("#define %-45s %s" % (i, real.get(i, '0')))
out.append("#endif")
open(sys.argv[2], "w").write("\n".join(out) + "\n")
print("IOReturn.h constants:", len(ids))
PY
: > "$SRC/build-host-tools/IOKit/usb/USB.h"
: > "$SRC/build-host-tools/IOKit/firewire/IOFireWireLib.h"
echo "wrote IOKit/IOReturn.h (+ empty usb/firewire)"
