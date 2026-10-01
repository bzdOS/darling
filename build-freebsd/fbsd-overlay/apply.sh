#!/bin/sh
# Apply bsdOS FreeBSD compat overlay onto a Darling pr-arm64 clone.
#
# Usage:
#   git clone https://github.com/darlinghq/darling darling-freebsd
#   cd darling-freebsd && git checkout pr-arm64
#   sh /path/to/hal/darling-fbsd-overlay/apply.sh
#
set -e

OVERLAY="$(dirname "$(realpath "$0")")"
DARLING="${1:-$(pwd)}"

echo "[apply] Applying bsdOS FreeBSD overlay to: $DARLING"

# Init required submodules (shallow — faster)
cd "$DARLING"
git submodule update --init --depth=1 \
  src/external/cctools-port \
  src/external/darlingserver \
  src/external/bootstrap_cmds \
  src/external/libplatform \
  src/external/libpthread \
  src/external/libkqueue

# Force checkout content (shallow init may leave worktrees empty)
for mod in darlingserver cctools-port libplatform libpthread bootstrap_cmds libkqueue; do
  dir="src/external/$mod"
  if [ -f "$dir/.git" ] && [ -z "$(ls -A $dir/*.c $dir/CMakeLists.txt 2>/dev/null)" ]; then
    echo "[apply] Checking out $mod..."
    (cd "$dir" && git checkout HEAD -- . 2>/dev/null) || true
  fi
done

# Create xnu stub dirs
mkdir -p src/external/xnu/darling/src/libsystem_kernel
mkdir -p src/external/xnu/libkern/kxld

# Copy overlay files
cp -r "$OVERLAY"/cmake        "$DARLING/"
cp -r "$OVERLAY"/src          "$DARLING/"

echo "[apply] Done. Build with:"
echo "  mkdir build && cd build"
echo "  cmake .. -DBSDOS_STARTUP_ONLY=ON -DCMAKE_BUILD_TYPE=Debug"
echo "  make -j\$(nproc)"
