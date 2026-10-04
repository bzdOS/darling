#!/bin/sh
# build-libmalloc-zone.sh — rebuild the overlay's libsystem_malloc.dylib
# with the macOS-13 zone-slot contract (no leading reserved1/reserved2).
#
# The pin's struct _malloc_zone_t (src/external/libmalloc @4f2a808d,
# include/malloc/malloc.h:67-68) leads with reserved1/reserved2, which
# shifts every macOS-13 slot +2 (control #14: vtable[0]=NULL instead of
# size). This script derives a corrected header and a src copy from the
# pin (the submodule checkout is never edited), compiles the full MSL
# against the corrected header, and links a dylib with the ORIGINAL
# export set and LC_ID. Every slot write in the sources is by field name,
# so the layout fix is: drop the two reserved fields + drop the five
# reserved-referencing sites (self-checked by grep below).
#
# Usage: sh build-freebsd/zone-contract/build-libmalloc-zone.sh
# Env: DARLING_SRC_DIR, DARLING_OVERLAY, DARLING_BUILD_DIR
set -e
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT="${DARLING_SRC_DIR:-$(cd "${SCRIPT_DIR}/../.." && pwd)}"
OVERLAY="${DARLING_OVERLAY:?set DARLING_OVERLAY to your overlay dir}"
BUILD="${DARLING_BUILD_DIR:?set DARLING_BUILD_DIR to your build dir}/zone-contract"
M="${ROOT}/src/external/libmalloc"
PATH="/usr/local/bin:/usr/local/sbin:/usr/bin:/bin:/sbin:/usr/sbin"
export PATH

mkdir -p "${BUILD}/obj" "${BUILD}/inc-fixed/malloc" "${BUILD}/src-fixed" "${BUILD}/staged"

# 1. SDK flat tarball (committed; unpacked into scratch, never in place)
SDK="${BUILD}/sdk"
if [ ! -d "${SDK}/usr/include/malloc" ]; then
    mkdir -p "${SDK}"
    tar xzf "${ROOT}/tests/vendor/macosx-sdk-flat.tar.gz" -C "${SDK}"
fi
# clang's resource stdatomic.h defers to __has_include_next — the flat
# SDK's own stdatomic.h is empty under __clang__, which silently kills
# the memory_order typedef; move the scratch copy aside so the builtin
# fallback is used (the scratch unpack is never the in-place SDK).
if [ -f "${SDK}/usr/include/stdatomic.h" ]; then
    mv "${SDK}/usr/include/stdatomic.h" "${SDK}/usr/include/stdatomic.h.disabled"
fi

# 2. staged overlay for the link (find|pax, never cp -a — symlinks)
mkdir -p "${BUILD}/staged/usr/lib/system"
(cd "${OVERLAY}/usr/lib" && find . -maxdepth 1 -type f -o -maxdepth 1 -type l | pax -rw "${BUILD}/staged/usr/lib")
if [ -d "${OVERLAY}/usr/lib/system" ]; then
    (cd "${OVERLAY}/usr/lib/system" && find . -maxdepth 1 -type f -o -maxdepth 1 -type l | pax -rw "${BUILD}/staged/usr/lib/system")
fi

# 3. corrected header: the pin's malloc/malloc.h without the reserved pair
python3 - "${M}/include/malloc/malloc.h" "${BUILD}/inc-fixed/malloc/malloc.h" <<'PY'
import sys
src, dst = sys.argv[1], sys.argv[2]
out = []
for line in open(src):
    s = line.strip()
    if s.startswith("void") and ("*reserved1" in s or "*reserved2" in s):
        continue  # the macOS-13 contract has no reserved pair
    out.append(line)
open(dst, "w").writelines(out)
print(f"corrected header: {dst} ({len(out)} lines)")
PY

# 3b. <System/...> include alias: the flat tarball stores the SDK's
#     System/ subtree flattened under usr/include; tsd_private.h wants
#     <System/machine/...> — map it back with one symlink in the scratch
#     include dir (never in place).
ln -sfn "${SDK}/usr/include" "${BUILD}/inc-fixed/System"

# 4. src copy with the reserved-referencing sites removed (exact lines)
rm -rf "${BUILD}/src-fixed"
cp -R "${M}/src" "${BUILD}/src-fixed"
python3 - "${BUILD}/src-fixed" <<'PY'
import re, sys, os
root = sys.argv[1]
targets = {
    "pguard_malloc.c": re.compile(r"^\s*\.(reserved1|reserved2)\s*="),
    "magazine_malloc.c": re.compile(r"basic_zone\.(reserved1|reserved2)\s*="),
    "nano_malloc.c": re.compile(r"basic_zone\.(reserved1|reserved2)\s*="),
    "nanov2_malloc.c": re.compile(r"basic_zone\.(reserved1|reserved2)\s*="),
    "purgeable_malloc.c": re.compile(r"basic_zone\.(reserved1|reserved2)\s*="),
}
total = 0
for fname, pat in targets.items():
    path = os.path.join(root, fname)
    lines = open(path).readlines()
    kept = [l for l in lines if not pat.search(l)]
    removed = len(lines) - len(kept)
    open(path, "w").writelines(kept)
    print(f"{fname}: removed {removed} reserved lines")
    total += removed
# malloc.c: the positional virtual_default_zone initializer leads with the
# two reserved placeholders — drop them so the named args realign
path = os.path.join(root, "malloc.c")
lines = open(path).readlines()
i = next(idx for idx, l in enumerate(lines) if "virtual_default_zone" in l and "static" in l)
j = next(idx for idx in range(i, len(lines)) if lines[idx].rstrip().endswith("= {"))
assert lines[j + 1].strip() == "NULL," and lines[j + 2].strip() == "NULL,", \
    f"virtual_default_zone preamble unexpected: {lines[j+1]!r} {lines[j+2]!r}"
del lines[j + 1:j + 3]
open(path, "w").writelines(lines)
print("malloc.c: removed 2 positional reserved placeholders from virtual_default_zone")
total += 2
print(f"total removed: {total}")
PY

# 5. self-check: zero reserved references remain
if grep -rn "reserved1\|reserved2" "${BUILD}/src-fixed" "${BUILD}/inc-fixed" ; then
    echo "FATAL: reserved references remain" >&2
    exit 1
fi
echo "self-check: no reserved1/2 references remain"

# 6. compile every source against the corrected header
#    (-nostdinc also hides clang's builtin headers; add the resource dir
#    explicitly so stdarg.h/stddef.h resolve)
CLANG_RES=$(clang -print-resource-dir)
EXTRA_INC="${ROOT}/src/external"          # <architecture/byte_order.h> etc.
FAKESDK="${ROOT}/tests/vendor/fakesdk"    # framework stub headers
: > "${BUILD}/objs.txt"
for f in "${BUILD}"/src-fixed/*.c; do
    base=$(basename "$f" .c)
    clang -target x86_64-apple-macos10.12 -nostdinc -w -fblocks \
        -DPRIVATE -DOS_UNFAIR_LOCK_INLINE=1 \
        -DOS_VARIANT_NOTRESOLVED=1 -DOS_VARIANT_RESOLVED=1 \
        -include stdatomic.h -include os/atomic.h -include i386/cpu_capabilities.h \
        -I"${BUILD}/inc-fixed" -I"${CLANG_RES}/include" \
        -I"${SDK}/usr/include" -I"${BUILD}/src-fixed" \
        -I"${EXTRA_INC}" -I"${FAKESDK}" -I"${M}/resolver" \
        -c "$f" -o "${BUILD}/obj/${base}.o"
    echo "${BUILD}/obj/${base}.o" >> "${BUILD}/objs.txt"
done
echo "compiled $(wc -l < "${BUILD}/objs.txt") objects"

# 7. export set = the original overlay dylib's (same contract), filtered
#    to the symbols the objects actually export globally: ld64.lld cannot
#    force MALLOC_NOEXPORT-hidden symbols out (the unfiltered list produced
#    181 "cannot export hidden symbol" warnings and a malformed export
#    trie — measured: dyld faults while binding libobjc's malloc imports
#    against that trie)
nm -gU "${OVERLAY}/usr/lib/system/libsystem_malloc.dylib" 2>/dev/null \
    | awk 'NF >= 3 { print $3 }' | sort -u > "${BUILD}/exports-orig.txt"
nm "${BUILD}"/obj/*.o 2>/dev/null | awk '$2 ~ /^[TDBR]$/ { print $3 }' | sort -u > "${BUILD}/exports-visible.txt"
comm -12 "${BUILD}/exports-orig.txt" "${BUILD}/exports-visible.txt" > "${BUILD}/exports.txt"
echo "exports: original $(wc -l < "${BUILD}/exports-orig.txt"), visible $(wc -l < "${BUILD}/exports-visible.txt"), final $(wc -l < "${BUILD}/exports.txt")"

# 8. link: original install name + versions + export list; imports stay
#    undefined exactly like the original overlay dylib carries them
#    (the closure does not export the $UNIX2003 variants of mprotect/
#    write/sleep/kill — the original links them as imports)
# ld64.lld in this toolchain has no -segalign (silently ignored —
# measured: geometry unchanged) and does not round a segment's vmsize up
# to a page boundary, so __TEXT's raw end overlaps __DATA's vmaddr; the
# residual is fixed post-link by fixup-segment-vm.py (exact-length LC
# field surgery — vmaddrs only, file offsets untouched; the fixup
# streams in __LINKEDIT are segment-relative, so nothing is rewritten)
ld64.lld -dylib -arch x86_64 -platform_version macos 10.12 10.12 \
    -undefined dynamic_lookup \
    -install_name /usr/lib/system/libsystem_malloc.dylib \
    -current_version 0.0.0 -compatibility_version 1.0.0 \
    -exported_symbols_list "${BUILD}/exports.txt" \
    -L"${BUILD}/staged/usr/lib" -L"${BUILD}/staged/usr/lib/system" \
    -lsystem_kernel \
    -lsystem_platform \
    -ldyld \
    -lcompiler_rt \
    -upward-lsystem_c \
    -o "${BUILD}/libsystem_malloc.dylib" $(cat "${BUILD}/objs.txt")

# ld64.lld does not implement -upward-l / -upward_library -- the upward
# edge to libsystem_c is added post-link, byte-cloned from the original
# overlay dylib's own LC record (exact-length surgery, ncmds+sizeofcmds
# and every file offset shifted together)
python3 "${SCRIPT_DIR}/add-upward-lc.py" \
    "${BUILD}/libsystem_malloc.dylib" \
    "${DARLING_OVERLAY}/usr/lib/system/libsystem_malloc.dylib" \
    "/usr/lib/system/libsystem_c.dylib"

# __DATA,__mod_init_func for ___malloc_init is NOT applied.  Control #27
# step B verdict (CFT-DLOPEN.md #27Б): doModInitFunctions is gated on
# gProcessInfo->libSystemInitialized exactly like -init
# (ImageLoaderMachO.cpp:2315-2319), with the sole exception of
# installPath == /usr/lib/libSystem.B.dylib.  A __mod_init_func entry in
# libsystem_malloc.dylib would be throwf-rejected at load (MSL is a
# dependency of libSystem.B and initializes first, libSystemInitialized
# still false).  The early-malloc-init fix lives in the libSystem.B
# initializer source (src/external/libsystem/init.c).  add-mod-init-func.py
# stays in the tree as a tool, unwired.

# segment vm geometry: __DATA vmaddr moves to align_up(__TEXT end),
# every __DATA section addr follows, __LINKEDIT vmaddr stays contiguous
python3 "${SCRIPT_DIR}/fixup-segment-vm.py" \
    "${BUILD}/libsystem_malloc.dylib"

echo "=== built dylib ==="
ls -la "${BUILD}/libsystem_malloc.dylib"
echo "exports now: $(nm -gU "${BUILD}/libsystem_malloc.dylib" | wc -l)"
nm -gU "${BUILD}/libsystem_malloc.dylib" | grep -E "malloc_default_zone|malloc_get_all_zones" | head -4
echo "BUILD_OK"
