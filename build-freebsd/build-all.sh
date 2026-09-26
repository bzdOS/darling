#!/bin/sh
# build-all.sh — one-shot rebuild + smoke for the entire darlinsg-FreeBSD stack.
# Handles: CF (+ stubs), mldr, launch-dynamic, Foundation, test binary, overlay, smoke.
#
# Usage: sh build-freebsd/build-all.sh [test-binary]
# Flags: set CFDBG=1 MLDRTACE=1 etc as env vars (forwarded to smoke).

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
SRC="${DARLING_SRC_DIR:-${SCRIPT_DIR}}"
BD="${DARLING_BUILD_DIR:?set DARLING_BUILD_DIR to your build dir}"
OD="${DARLING_OVERLAY:?set DARLING_OVERLAY to your overlay dir}"
TEST="${1:-load-wayland-backend-macho}"
# Test source file: strip "-macho" suffix, use .m extension
TEST_M="${TEST%-macho}.m"
export PATH="/usr/local/bin:/usr/local/sbin:/usr/bin:/bin:/sbin:/usr/sbin"

B="${BD}/real-macho"
SF="${B}/sdk-flat"
SO="${B}/staged-overlay"
CF="${SRC}/src/external/corefoundation"
FND="${SRC}/src/external/foundation"

log() { printf '\033[1;32m=== %s ===\033[0m\n' "$1"; }

# ── 1. CoreFoundation ──────────────────────────────────────────────────────
log "CF"
rm -rf "${B}/cf-build"
sh "${SRC}/build-freebsd/build-cf-only.sh" 2>&1 | tail -3
echo "CF OK"

# ── 2. mldr (macOS→FreeBSD open-flags translation, MLDRTACE, open_nocancel) ─
log "mldr"
sudo DARLING_BUILD_DIR="${BD}" sh "${SRC}/build-freebsd/build-mldr-only.sh" 2>&1 | tail -2
echo "mldr OK"

# ── 3. Foundation ──────────────────────────────────────────────────────────
log "Foundation"
sh "${SRC}/build-freebsd/build-foundation-only.sh" 2>&1 | tail -2
echo "Foundation OK"

# ── 4. launch-dynamic (rebuild from source) ────────────────────────────────
log "launch-dynamic"
cc -o "${BD}/launch-dynamic" "${SRC}/tests/launch-dynamic-smoke.c" -lpthread
echo "launch-dynamic OK"

# ── 5. Overlay: create /proc symlink for fstatfs64's /proc/self/mounts ────
log "overlay/proc"
rm -rf "${OD}/proc" 2>/dev/null || true
ln -sfn /proc "${OD}/proc" 2>/dev/null || true
ls -la "${OD}/proc" | grep -q "proc -> /proc" && echo "proc symlink OK" || echo "WARN: proc symlink failed"

# ── 6. Test binary ─────────────────────────────────────────────────────────
log "test"
SDK_FLAT="${SF}" STAGED_OVERLAY="${SO}" \
  clang -target x86_64-apple-macos10.12 -nostdinc -D__DARWIN_ONLY_UNIX_CONFORMANCE=1 \
    -I"${FND}/include" -I"${SF}/usr/include" -I"${SRC}/tests/vendor/fakesdk" \
    -fblocks -fconstant-cfstrings -fobjc-runtime=macosx-10.12 -DDARLING \
    -F"${SF}/Frameworks" -x objective-c -O1 -w \
    -c "${SRC}/tests/${TEST_M}" -o "${B}/${TEST_M%.m}.o"
ld64.lld -arch x86_64 -platform_version macos 10.12 10.12 \
  -syslibroot "${SO}" -Z \
  -o "${SRC}/tests/${TEST}" \
  "${B}/${TEST_M%.m}.o" \
  "${SO}/System/Library/Frameworks/Foundation.framework/Versions/C/Foundation" \
  "${SO}/System/Library/Frameworks/CoreFoundation.framework/Versions/A/CoreFoundation" \
  "${SO}/usr/lib/libobjc.A.dylib" \
  "${SO}/usr/lib/libicucore.A.dylib" \
  "${SO}/usr/lib/libc++.1.dylib" \
  "${SO}/usr/lib/libc++abi.dylib" \
  "${SO}/usr/lib/libSystem.B.dylib"
echo "test OK"

# ── 7. Smoke test ──────────────────────────────────────────────────────────
log "smoke"
sh "${SRC}/build-freebsd/run-smoke.sh" "${TEST}"
