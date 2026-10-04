#!/bin/sh
# build-libmalloc-pin.sh — rebuild libsystem_malloc with the PIN layout
# (reserved1/reserved2 leading) and the FULL original export set.
#
# Control #18 showed the stock overlay uses the pin-era layout:
#   vtable[0..7] = NULL, NULL, size, malloc, calloc, valloc, free, realloc
# The previous rebuild removed the reserved fields (macOS-13 layout) and
# lost 51 zone-management exports. This script keeps the pin sources
# unmodified and exports everything the original exports.
#
# Usage: sh build-freebsd/zone-contract/build-libmalloc-pin.sh
# Env: DARLING_SRC_DIR, DARLING_OVERLAY, DARLING_BUILD_DIR
set -e
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT="${DARLING_SRC_DIR:-$(cd "${SCRIPT_DIR}/../.." && pwd)}"
OVERLAY="${DARLING_OVERLAY:?set DARLING_OVERLAY to your overlay dir}"
BUILD="${DARLING_BUILD_DIR:?set DARLING_BUILD_DIR to your build dir}/zone-pin"
M="${ROOT}/src/external/libmalloc"
PATH="/usr/local/bin:/usr/local/sbin:/usr/bin:/bin:/sbin:/usr/sbin"
export PATH

mkdir -p "${BUILD}/obj" "${BUILD}/src-pin" "${BUILD}/staged"

# 1. SDK flat tarball
SDK="${BUILD}/sdk"
if [ ! -d "${SDK}/usr/include/malloc" ]; then
    mkdir -p "${SDK}"
    tar xzf "${ROOT}/tests/vendor/macosx-sdk-flat.tar.gz" -C "${SDK}"
fi
if [ -f "${SDK}/usr/include/stdatomic.h" ]; then
    mv "${SDK}/usr/include/stdatomic.h" "${SDK}/usr/include/stdatomic.h.disabled"
fi

# 2. staged overlay for the link
mkdir -p "${BUILD}/staged/usr/lib/system"
(cd "${OVERLAY}/usr/lib" && find . -maxdepth 1 -type f -o -maxdepth 1 -type l | pax -rw "${BUILD}/staged/usr/lib")
if [ -d "${OVERLAY}/usr/lib/system" ]; then
    (cd "${OVERLAY}/usr/lib/system" && find . -maxdepth 1 -type f -o -maxdepth 1 -type l | pax -rw "${BUILD}/staged/usr/lib/system")
fi

# 3. src copy: UNCHANGED pin sources (keep reserved1/reserved2)
rm -rf "${BUILD}/src-pin"
cp -R "${M}/src" "${BUILD}/src-pin"
echo "pin sources copied: $(ls "${BUILD}/src-pin"/*.c | wc -l) .c files"

# 3b. optional once-only init markers (diagnosis builds only; the sources
#     touched are the BUILD COPY — the submodule itself is never edited)
if [ -n "${MSL_MARKERS:-}" ]; then
    python3 "${SCRIPT_DIR}/inject-init-markers.py" "${BUILD}/src-pin"
fi

# 4. include alias for <System/...>
ln -sfn "${SDK}/usr/include" "${BUILD}/System"

# 5. compile every source against the PIN header (no field removal)
CLANG_RES=$(clang -print-resource-dir)
EXTRA_INC="${ROOT}/src/external"
FAKESDK="${ROOT}/tests/vendor/fakesdk"
: > "${BUILD}/objs.txt"
for f in "${BUILD}"/src-pin/*.c; do
    base=$(basename "$f" .c)
    clang -target x86_64-apple-macos10.12 -nostdinc -w -fblocks \
        -DPRIVATE -DOS_UNFAIR_LOCK_INLINE=1 \
        -DOS_VARIANT_NOTRESOLVED=1 -DOS_VARIANT_RESOLVED=1 \
        -include stdatomic.h -include os/atomic.h -include i386/cpu_capabilities.h \
        -I"${BUILD}" -I"${M}/include" -I"${CLANG_RES}/include" \
        -I"${SDK}/usr/include" -I"${BUILD}/src-pin" \
        -I"${EXTRA_INC}" -I"${FAKESDK}" -I"${M}/resolver" \
        -c "$f" -o "${BUILD}/obj/${base}.o"
    echo "${BUILD}/obj/${base}.o" >> "${BUILD}/objs.txt"
done
echo "compiled $(wc -l < "${BUILD}/objs.txt") objects"

# 6. export set = the original's EXPORT TRIE names (95), not nm's full
#    list (283 includes 181 MALLOC_NOEXPORT-hidden symbols; passing those
#    to -exported_symbols_list makes ld64.lld produce a broken trie with
#    only 44 entries — measured in control #19)
nm -gU "${OVERLAY}/usr/lib/system/libsystem_malloc.dylib" 2>/dev/null \
    | awk 'NF >= 3 { print $3 }' | sort -u > "${BUILD}/exports-nm.txt"
# Extract the original's export TRIE names (the real contract)
# (__file__ is not defined in heredoc stdin — pass the dir explicitly)
python3 - "${OVERLAY}/usr/lib/system/libsystem_malloc.dylib" "${BUILD}/exports-trie.txt" "${SCRIPT_DIR}/.." <<'PY'
import importlib.util, sys, os
msldiff_path = os.path.join(sys.argv[3], "msl-diff.py")
spec = importlib.util.spec_from_file_location("msldiff", msldiff_path)
m = importlib.util.module_from_spec(spec)
spec.loader.exec_module(m)
od, _ = m.load_slice(sys.argv[1])
_, odi = m.parse(od)
oexp = m.export_set(od, *odi["export"])
with open(sys.argv[2], "w") as f:
    for s in sorted(oexp):
        f.write(s + "\n")
print(f"original export trie: {len(oexp)} names")
PY
echo "trie exports: $(wc -l < "${BUILD}/exports-trie.txt")"
# Verify all trie names are visible in our objects
nm "${BUILD}"/obj/*.o 2>/dev/null | awk '$2 ~ /^[TDBR]$/ { print $3 }' | sort -u > "${BUILD}/exports-visible.txt"
comm -12 "${BUILD}/exports-trie.txt" "${BUILD}/exports-visible.txt" > "${BUILD}/exports.txt"
echo "final exports (trie ∩ visible): $(wc -l < "${BUILD}/exports.txt")"
# Zone-management check
grep -cE "malloc_default_zone|malloc_get_all_zones|malloc_create_zone|malloc_destroy_zone|malloc_get_zone_name|malloc_num_zones|malloc_zones" "${BUILD}/exports.txt"

# 7. link with the original's export trie names
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
    -o "${BUILD}/libsystem_malloc.dylib" $(cat "${BUILD}/objs.txt") 2>&1 | tee "${BUILD}/link.log"

# 8. post-link: add the upward LC (byte-clone from original)
python3 "${SCRIPT_DIR}/add-upward-lc.py" \
    "${BUILD}/libsystem_malloc.dylib" \
    "${OVERLAY}/usr/lib/system/libsystem_malloc.dylib" \
    "/usr/lib/system/libsystem_c.dylib"

# 8b. post-link: add a __DATA,__mod_init_func entry for ___malloc_init.
#      dyld forbids -init (LC_ROUTINES_64) in any image that does not link
#      with libSystem.dylib (ImageLoaderMachO.cpp:2261-2263: requires
#      libSystemInitialized=true, only true after libSystem.B's own
#      initializer).  The stock overlay members (libSystem.B.dylib,
#      libc++.1.dylib) register initializers through __mod_init_func
#      instead — a section of function pointers dyld calls after load,
#      without the -init gate.  This script repeats that mechanism.
python3 "${SCRIPT_DIR}/add-mod-init-func.py" \
    "${BUILD}/libsystem_malloc.dylib" \
    "___malloc_init"

# 8d. post-link: segment vm geometry (__TEXT vmsize rounded to page,
#      __DATA fileoff moved, all __LINKEDIT offsets shifted)
python3 "${SCRIPT_DIR}/fixup-segment-vm.py" \
    "${BUILD}/libsystem_malloc.dylib"

# 9. verify
echo "=== built dylib ==="
ls -la "${BUILD}/libsystem_malloc.dylib"
echo "exports now: $(nm -gU "${BUILD}/libsystem_malloc.dylib" | wc -l)"
echo "--- zone-management symbols ---"
nm -gU "${BUILD}/libsystem_malloc.dylib" | grep -E "malloc_default_zone|malloc_get_all_zones|malloc_create_zone|malloc_destroy_zone|malloc_get_zone_name|malloc_num_zones|malloc_zones"
echo "--- __v_zone first 6 qwords ---"
python3 - "${BUILD}/libsystem_malloc.dylib" <<'PY'
import struct, sys
d = open(sys.argv[1], "rb").read()
# Find __DATA segment and __v_zone section
ncmds = struct.unpack_from("<I", d, 16)[0]
o = 32
for _ in range(ncmds):
    cmd, cmdsize = struct.unpack_from("<II", d, o)
    if cmd == 0x19:  # LC_SEGMENT_64
        name = d[o+8:o+24].split(b"\0")[0].decode()
        if name == "__DATA":
            vmaddr = struct.unpack_from("<Q", d, o+24)[0]
            fileoff = struct.unpack_from("<Q", d, o+40)[0]
            nsects = struct.unpack_from("<I", d, o+64)[0]
            so = o + 72
            for _ in range(nsects):
                sn = d[so:so+16].split(b"\0")[0].decode()
                sg = d[so+16:so+32].split(b"\0")[0].decode()
                saddr = struct.unpack_from("<Q", d, so+32)[0]
                ssize = struct.unpack_from("<Q", d, so+40)[0]
                if sn == "__v_zone":
                    fo = fileoff + (saddr - vmaddr)
                    blob = d[fo:fo+min(ssize, 64)]
                    print(f"  __v_zone vmaddr=0x{saddr:x} size=0x{ssize:x}")
                    print(f"  hex: {blob.hex()}")
                    qwords = [struct.unpack_from("<Q", blob, i)[0] for i in range(0, min(len(blob), 64), 8)]
                    for i, q in enumerate(qwords):
                        print(f"    [{i}] = 0x{q:x}")
                so += 80
    o += cmdsize
PY
echo "BUILD_OK"
