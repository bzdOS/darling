#!/bin/sh
# Control #113: stop libsystem_kernel from exporting its empty
# _os_unfair_lock_unlock / _os_unfair_lock_lock.
#
# The Chrome stub 0xdbbeffc (symbol stub for _os_unfair_lock_unlock) bound its
# lazy pointer to libsystem_kernel.dylib+0x4af70 (nm: _os_unfair_lock_unlock),
# which is an empty weak stub — so the release at 0x8eee7b cleared nothing and
# the second trylock recursed into the abort (Control #112). The empty copy is
# defined weak in two files under xnu's libsystem_kernel emulation; as exported
# weak symbols they win the bind over libsystem_platform's real implementation.
#
# Marking both hidden keeps them for intra-dylib linking but removes them from
# the export trie, so the stub binds to libsystem_platform instead.
#
# The two files live in the src/external/xnu submodule (so the change cannot be
# committed from the parent tree); this script applies it idempotently.
set -e
SRC="${DARLING_SRC_DIR:?set DARLING_SRC_DIR to the tree root}"
F1="$SRC/src/external/xnu/darling/src/libsystem_kernel/emulation/src/linux_premigration/ext/file_handle.c"
F2="$SRC/src/external/xnu/darling/src/libsystem_kernel/emulation/src/xnu_syscall/bsd/impl/bsdthread/workq_kernreturn.c"

for f in "$F1" "$F2"; do
    [ -f "$f" ] || { echo "missing: $f" >&2; exit 1; }
    sed -i.bak \
        's/__attribute__((weak)) os_unfair_lock_unlock/__attribute__((weak, visibility("hidden"))) os_unfair_lock_unlock/; s/__attribute__((weak)) os_unfair_lock_lock/__attribute__((weak, visibility("hidden"))) os_unfair_lock_lock/' \
        "$f"
    rm -f "$f.bak"
    echo "patched: $f"
done
echo "done — rebuild with: ninja src/external/xnu/darling/src/libsystem_kernel/libsystem_kernel.dylib"
