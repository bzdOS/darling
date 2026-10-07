#!/bin/sh
# Control #118: align the os_unfair_lock_lock/unlock DECLARATION visibility with
# the #113 hidden definition.
#
# The #113 fix marks the weak empty definitions in libsystem_kernel's emulation
# (file_handle.c, workq_kernreturn.c) hidden so they are not exported and the
# Chrome stub binds to libsystem_platform. But their declaration in
# os/lock.h carries OS_EXPORT = visibility("default"), so clang rejects the
# mismatch ("visibility does not match previous declaration", file_handle.c:36/37
# and workq_kernreturn.c:87).
#
# This patches os/lock.h (libplatform submodule, reached through the SDK symlink)
# so those two declarations are hidden. The hidden must stay on the definition
# (#113): without it the empty copy wins the bind again (#112).
#
# Note: libplatform's own lock.c defines them OS_ATOMIC_EXPORT (default); that
# dylib is built separately and is not touched here.
set -e
SRC="${DARLING_SRC_DIR:?set DARLING_SRC_DIR}"
F="$SRC/src/external/libplatform/include/os/lock.h"
[ -f "$F" ] || { echo "missing $F" >&2; exit 1; }
python3 - "$F" << 'PY'
import sys
p = sys.argv[1]
lines = open(p).read().split("\n")
targets = ("void os_unfair_lock_lock(os_unfair_lock_t lock);",
           "void os_unfair_lock_unlock(os_unfair_lock_t lock);")
changed = 0
for i in range(1, len(lines)):
    if lines[i] in targets and "OS_EXPORT" in lines[i-1]:
        lines[i-1] = lines[i-1].replace("OS_EXPORT",
            'extern __attribute__((__visibility__("hidden")))')
        changed += 1
open(p, "w").write("\n".join(lines))
print("patched declarations:", changed)
PY
