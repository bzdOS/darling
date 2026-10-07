#!/bin/sh
# Control #115: narrow libsystem_kernel build — only its own object commands.
#
# The full ninja target pulls in 1924 steps and dies in libc before reaching
# libsystem_kernel (#114). This script takes ONLY the compile commands whose
# output lives under src/external/xnu/darling/src/libsystem_kernel, straight
# from `ninja -t commands`, and runs them — flags are not invented.
#
# Stop condition (Control #115): the object commands themselves fail on the SDK
# header sys/_types.h (`unknown type name '__int64_t'`) — a different root than
# the missing Libinfo submodule; see CFT-DLOPEN.md Control #115.
set -e
BD="${DARLING_BUILD_DIR:?set DARLING_BUILD_DIR}"
cd "$BD"
ninja -t commands src/external/xnu/darling/src/libsystem_kernel/libsystem_kernel.dylib \
  | grep "xnu/darling/src/libsystem_kernel" \
  | grep -- "-c " > /tmp/kernel-object-cmds.sh
echo "object commands: $(wc -l < /tmp/kernel-object-cmds.sh)"
sh /tmp/kernel-object-cmds.sh
