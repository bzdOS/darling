#!/bin/sh
# Control #114: stand-in for the absent src/external/Libinfo submodule.
#
# The Libinfo submodule is declared in .gitmodules with the relative URL
# ../darling-Libinfo.git, but this checkout has no sibling repo and the remote
# git@github.com:bzdOS/darling-Libinfo.git answers "Repository not found"; the
# submodule's .git/modules tree is empty, so `git submodule update --init` fails
# with "Unable to find current revision". CMake regeneration then dies at
# src/CMakeLists.txt:202 (add_subdirectory(external/Libinfo)).
#
# Libinfo is not needed for the libsystem_kernel build under test, so this
# script writes a minimal CMake target (system_info) so regeneration can pass.
# It does NOT make the tree fully buildable — the full build then fails on
# libc's SDK headers (see Control #114).
set -e
SRC="${DARLING_SRC_DIR:?set DARLING_SRC_DIR to the tree root}"
D="$SRC/src/external/Libinfo"
mkdir -p "$D"
cat > "$D/empty.c" << 'EOF'
void __libinfo_placeholder(void) {}
EOF
cat > "$D/CMakeLists.txt" << 'EOF'
add_library(system_info SHARED empty.c)
EOF
echo "wrote $D/CMakeLists.txt and empty.c"
