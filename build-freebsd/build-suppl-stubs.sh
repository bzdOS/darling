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
# The CFString constants bind their isa two-level to CoreFoundation (-needed_library; -syslibroot
# lets the linker find what CoreFoundation re-exports by install name).
# -undefined dynamic_lookup is required: the forwarding stubs jump to symbols
# that a real image (libsystem_c/libsystem_platform) must provide.
#
# Every linked image is run through check-suppl-bodies.py; an outward pc-relative
# call/jump/RIP-relative reference in any body fails the build (set -e).
#
# Data is never emitted as code: check-data-as-code.py runs first over the installed
# overlay and writes the names it finds to be data (ObjC classes, block isa objects,
# constants bound through non-lazy pointers) to a list the generator reads; the linked
# images are then run through the same check, and a symbol exported as a code stub that
# is really data fails the build (set -e). ObjC classes come out of the generator as a
# real Objective-C source (<name>.s.m) linked into the image.
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
# classification pass over the installed state; its exit status is the old state's, not ours
python3 "${SRC}/build-freebsd/check-data-as-code.py" --overlay "$OVERLAY" \
    --emit-data "${OUT}/data-names.txt" > "${OUT}/check-before.txt" 2>&1 || true
tail -1 "${OUT}/check-before.txt"
for pair in "suppl:/usr/lib/supl.dylib" "cg-supl:/usr/lib/suppl.dylib"; do
    name="${pair%%:*}"
    install="${pair#*:}"
    src_dylib="${OVERLAY}/usr/lib/${name}.dylib"
    [ -f "$src_dylib" ] || { echo "FATAL: ${src_dylib} missing" >&2; exit 1; }

    llvm-objdump --macho --exports-trie "$src_dylib" 2>/dev/null \
        | awk '$1 ~ /^0x/ {print $2}' > "${OUT}/${name}.names.txt"

    echo "=== ${name} (install_name ${install}, $(wc -l < "${OUT}/${name}.names.txt") symbols)"
    CFW="${OVERLAY}/System/Library/Frameworks/CoreFoundation.framework/Versions/A/CoreFoundation"
    # cg-supl defines a few names CoreFoundation owns (_NSCocoaErrorDomain ...): it re-exports
    # CoreFoundation and drops them, so the real object is found instead of a shadow.
    EXTRA="${GEN_EXTRA_PROVIDERS:-}"
    [ "$name" = cg-supl ] && EXTRA="$CFW"
    GEN_EXTRA_PROVIDERS="$EXTRA" python3 "${SRC}/build-freebsd/gen-cg-supl.py" \
        "${OUT}/${name}.names.txt" "${OUT}/${name}.s" "$install" "${OVERLAY}" --data-list "${OUT}/data-names.txt"
    clang -target x86_64-apple-macos10.12 -c "${OUT}/${name}.s" -o "${OUT}/${name}.o"
    OBJS="${OUT}/${name}.o"
    if [ -f "${OUT}/${name}.s.m" ]; then
        clang -target x86_64-apple-macos10.12 -fobjc-runtime=macosx-10.12 \
            -c "${OUT}/${name}.s.m" -o "${OUT}/${name}.m.o"
        OBJS="${OBJS} ${OUT}/${name}.m.o"
    fi
    # The providers are re-exported so that the names the generator omits are
    # still found in this image: the framework's imports are two-level and name
    # suppl, so dyld looks them up here and follows the re-exports.
    # The re-exported set is every dylib libSystem.B.dylib re-exports: the
    # generator drops a stub for any name one of them defines, so each of them
    # must be reachable from here.
    REEXPORTS=""
    # cg-supl: CoreFoundation first, so the CFString isa binds to it (not to a provider that also lists it)
    [ "$name" = cg-supl ] && REEXPORTS=" -reexport_library $CFW"
    for prov in $(python3 "${SRC}/build-freebsd/gen-cg-supl.py" --providers "${OVERLAY}"); do
        REEXPORTS="${REEXPORTS} -reexport_library ${prov}"
    done
    [ "$name" = cg-supl ] || for x in $(echo "$EXTRA" | tr : " "); do REEXPORTS="${REEXPORTS} -reexport_library ${x}"; done
    NEEDCF="-needed_library $CFW"
    [ "$name" = cg-supl ] && NEEDCF=""
    ld64.lld -dylib -arch x86_64 -platform_version macos 10.12 10.12 \
        -undefined dynamic_lookup \
        ${NEEDCF} \
        -syslibroot "${OVERLAY}" \
        ${REEXPORTS} \
        -install_name "$install" -current_version 0.0.0 \
        -compatibility_version 0.0.0 \
        -o "${OUT}/${name}.dylib" ${OBJS}
    file "${OUT}/${name}.dylib"
    # Fail the build if any body has a pc-relative reference that leaves it.
    python3 "${SRC}/build-freebsd/check-suppl-bodies.py" "${OUT}/${name}.dylib"
    # Fail the build if data is exported as a code stub (symbols, consumers: see the check).
    python3 "${SRC}/build-freebsd/check-data-as-code.py" --overlay "$OVERLAY" --no-discover \
        --stub "${OUT}/${name}.dylib=/usr/lib/${name}.dylib"
done
