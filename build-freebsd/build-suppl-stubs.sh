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
# Every linked image is run through check-suppl-bodies.py; an outward pc-relative
# call/jump/RIP-relative reference in any body fails the build (set -e).
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
    # The providers are re-exported so that the names the generator omits are
    # still found in this image: the framework's imports are two-level and name
    # suppl, so dyld looks them up here and follows the re-exports.
    # The re-exported set is every dylib libSystem.B.dylib re-exports: the
    # generator drops a stub for any name one of them defines, so each of them
    # must be reachable from here.
    REEXPORTS=""
    for prov in $(python3 "${SRC}/build-freebsd/gen-cg-supl.py" --providers "${OVERLAY}"); do
        REEXPORTS="${REEXPORTS} -reexport_library ${prov}"
    done
    ld64.lld -dylib -arch x86_64 -platform_version macos 10.12 10.12 \
        -undefined dynamic_lookup \
        ${REEXPORTS} \
        -install_name "$install" -current_version 0.0.0 \
        -compatibility_version 0.0.0 \
        -o "${OUT}/${name}.dylib" "${OUT}/${name}.o"
    file "${OUT}/${name}.dylib"
    # Fail the build if any body has a pc-relative reference that leaves it.
    python3 "${SRC}/build-freebsd/check-suppl-bodies.py" "${OUT}/${name}.dylib"
done
