#!/bin/sh
# build-suppl-stubs.sh — regenerate the two supplement stub dylibs
# (suppl.dylib and cg-supl.dylib) from their current export lists with
# build-freebsd/gen-cg-supl.py.
#
# The export list of the installed dylib is the generator's input, so a rebuild
# keeps every symbol it had and changes only how the generator types them (the
# forwarding table: ___bzero -> _bzero, _bzero -> __platform_bzero).
#
# Install names are the ones the installed dylibs carry: suppl.dylib calls
# itself /usr/lib/supl.dylib, cg-supl.dylib calls itself /usr/lib/suppl.dylib.
# -undefined dynamic_lookup is required: the forwarding stubs jump to symbols
# that a real image (libsystem_c/libsystem_platform) must provide.
#
# Usage: sh build-freebsd/build-suppl-stubs.sh [outdir]
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
SRC="${DARLING_SRC_DIR:-${SCRIPT_DIR}}"
OVERLAY="${DARLING_OVERLAY:?set DARLING_OVERLAY to your overlay dir}"
BUILD="${DARLING_BUILD_DIR:?set DARLING_BUILD_DIR to your build dir}"
OUT="${1:-${BUILD}/suppl-stubs}"

PATH="/usr/local/bin:/usr/local/sbin:/usr/bin:/bin:/sbin:/usr/sbin"
export PATH

mkdir -p "$OUT"
for pair in "suppl:/usr/lib/supl.dylib" "cg-supl:/usr/lib/suppl.dylib"; do
    name="${pair%%:*}"
    install="${pair#*:}"
    src_dylib="${OVERLAY}/usr/lib/${name}.dylib"
    [ -f "$src_dylib" ] || { echo "FATAL: ${src_dylib} missing" >&2; exit 1; }

    llvm-objdump --macho --exports-trie "$src_dylib" 2>/dev/null \
        | awk '$1 ~ /^0x/ {print $2}' > "${OUT}/${name}.names.txt"

    echo "=== ${name} (install_name ${install}, $(wc -l < "${OUT}/${name}.names.txt") symbols)"
    python3 "${SRC}/build-freebsd/gen-cg-supl.py" \
        "${OUT}/${name}.names.txt" "${OUT}/${name}.s" "$install" "${OVERLAY}"
    clang -target x86_64-apple-macos10.12 -c "${OUT}/${name}.s" -o "${OUT}/${name}.o"
    # The four providers are re-exported so that the names SKIP_NAMES omits are
    # still found in this image: the framework's imports are two-level and name
    # suppl, so dyld looks them up here and follows the re-exports (step 43/2).
    ld64.lld -dylib -arch x86_64 -platform_version macos 10.12 10.12 \
        -undefined dynamic_lookup \
        -reexport_library "${OVERLAY}/usr/lib/system/libsystem_c.dylib" \
        -reexport_library "${OVERLAY}/usr/lib/system/libsystem_kernel.dylib" \
        -reexport_library "${OVERLAY}/usr/lib/system/libsystem_m.dylib" \
        -reexport_library "${OVERLAY}/usr/lib/system/libsystem_platform.dylib" \
        -install_name "$install" -current_version 0.0.0 \
        -compatibility_version 0.0.0 \
        -o "${OUT}/${name}.dylib" "${OUT}/${name}.o"
    file "${OUT}/${name}.dylib"
done
