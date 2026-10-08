#!/bin/sh
# Control #121: narrow per-slice build of an arbitrary ninja target, without a
# full tree build. Same method as narrow-build-kernel.sh (Control #120-2):
# multi-arch clang hides per-arch objects in a temp dir, so compile each object
# per slice with an explicit -o and lipo the two thin objects into the fat .o;
# then link (thin ld per slice + lipo) or archive.
#
# usage: sh build-freebsd/narrow-build-target.sh <ninja-target>
set -e
TARGET="${1:?usage: narrow-build-target.sh <ninja-target>}"
BD="${DARLING_BUILD_DIR:?set DARLING_BUILD_DIR}"
SRC="${DARLING_SRC_DIR:?set DARLING_SRC_DIR}"
cd "$BD"
CCTOOLS_MISC="${BD}/src/external/cctools-port/cctools/misc"
PATH="${CCTOOLS_MISC}:${PATH}"; export PATH
LD64=""
for c in "${SRC}/build-host-tools/ld64/x86_64-apple-darwin20-ld" \
         "${BD}/dyld-only/src/external/cctools-port/cctools/ld64/src/x86_64-apple-darwin20-ld" \
         "${BD}/src/external/cctools-port/cctools/ld64/src/x86_64-apple-darwin20-ld"; do
    [ -x "$c" ] && { LD64="$c"; break; }
done

ninja -t commands "$TARGET" | grep -- " -c " > /tmp/nbt-cmds.sh
# Control #122: the SDK's usr/include/dyld/VersionMap.h is a symlink whose target
# (AvailabilityVersions/gen/...) is not generated in this tree; the dyld-only
# build tree has a real copy. Add its SDK include dir LAST (lowest priority) so
# only genuinely missing headers (dyld/VersionMap.h) resolve from there.
DYLD_SDK="${BD}/dyld-only/Developer/Platforms/MacOSX.platform/Developer/SDKs/MacOSX.sdk/usr/include"
EXTRA="-I${SRC}/build-host-tools"
[ -d "$DYLD_SDK/dyld" ] && EXTRA="${EXTRA} -I${DYLD_SDK}"
sed -i.bak "s# -c # ${EXTRA} -c #" /tmp/nbt-cmds.sh; rm -f /tmp/nbt-cmds.sh.bak
echo "[$TARGET] object commands: $(wc -l < /tmp/nbt-cmds.sh)"

: > /tmp/nbt-perarch.sh; : > /tmp/nbt-lipo.sh
while IFS= read -r cmd; do
    obj=$(printf '%s\n' "$cmd" | sed -E 's/.* -o ([^ ]+) .*/\1/')
    src=$(printf '%s\n' "$cmd" | sed -E 's/.* -c (.*)/\1/')
    base=$(printf '%s\n' "$cmd" | sed -E 's/ -arch [^ ]+//g; s# -o [^ ]+##; s# -c .*##')
    printf 'mkdir -p %s\n' "$(dirname "$obj")" >> /tmp/nbt-perarch.sh
    case "$src" in
        *platform_32bit*|*/i386/*)
            printf '%s -arch i386 -o %s.i386.o -c %s\n' "$base" "$obj" "$src" >> /tmp/nbt-perarch.sh
            printf 'cp %s.i386.o %s\n' "$obj" "$obj" >> /tmp/nbt-lipo.sh
            ;;
        *platform_64bit*|*/x86_64/*)
            printf '%s -arch x86_64 -o %s.x86_64.o -c %s\n' "$base" "$obj" "$src" >> /tmp/nbt-perarch.sh
            printf 'cp %s.x86_64.o %s\n' "$obj" "$obj" >> /tmp/nbt-lipo.sh
            ;;
        *)
            printf '%s -arch i386 -o %s.i386.o -c %s\n' "$base" "$obj" "$src" >> /tmp/nbt-perarch.sh
            printf '%s -arch x86_64 -o %s.x86_64.o -c %s\n' "$base" "$obj" "$src" >> /tmp/nbt-perarch.sh
            printf 'llvm-lipo -create -output %s %s.i386.o %s.x86_64.o\n' "$obj" "$obj" "$obj" >> /tmp/nbt-lipo.sh
            ;;
    esac
done < /tmp/nbt-cmds.sh
sh /tmp/nbt-perarch.sh
sh /tmp/nbt-lipo.sh
echo "[$TARGET] objects done"

# link/archive command: the target's OWN link — the line with -o <target path>.
# (Control #139: tail -1 of the whole closure grabs an aggregate link, not the
# target's own; the #138 list of 85 "missing" was dominated by -dylib_file
# redirects of that aggregate, which are not inputs of libsystem_kernel)
ninja -t commands "$TARGET" | grep -F -- "-o $TARGET" > /tmp/nbt-link.sh
# cctools ar/ranlib are shell wrappers that produce no file here; llvm-ar/ranlib
# build the Mach-O archive. (Control #121)
sed -i.bak -E "s#${BD}/src/external/cctools-port/cctools/ar/x86_64-apple-darwin20-ar#llvm-ar#g; s#${BD}/src/external/cctools-port/cctools/ar/x86_64-apple-darwin20-ranlib#llvm-ranlib#g" /tmp/nbt-link.sh
rm -f /tmp/nbt-link.sh.bak
if [ -n "$LD64" ]; then
    sed -i.bak "s#${BD}/src/external/cctools-port/cctools/ld64/src/x86_64-apple-darwin20-ld#${LD64}#g" /tmp/nbt-link.sh
    rm -f /tmp/nbt-link.sh.bak
fi
# Control #138: resolve dylib link inputs against main and dyld-only trees.
# Control #139: count only DIRECT dylib inputs — strip -Wl,-dylib_file redirects
# first (a search-path table, not link inputs). The #138 "85 missing" were
# mostly such redirects of the aggregate link.
out=$(grep -oE '\-o [^ ]+' /tmp/nbt-link.sh | tail -1 | sed 's/-o //')
sed 's#-Wl,-dylib_file,[^ ]*##g' /tmp/nbt-link.sh > /tmp/nbt-link-nodf.sh
: > /tmp/nbt-missing-dylibs.txt; : > /tmp/nbt-dylib-rewrite.sed
resolve_dylib() {
    p="$1"; rel="${p#"$BD"/}"
    [ "$rel" = "$out" ] && return 0
    [ -f "$p" ] && return 0
    if [ -f "$BD/dyld-only/$rel" ]; then
        printf 's#%s#%s#g\n' "$p" "$BD/dyld-only/$rel" >> /tmp/nbt-dylib-rewrite.sed
        printf 's#%s%s#%s%s#g\n' '\([[:space:]]\)' "$rel" '\1' "$BD/dyld-only/$rel" >> /tmp/nbt-dylib-rewrite.sed
        return 0
    fi
    echo "$rel" >> /tmp/nbt-missing-dylibs.txt
}
grep -oE "${BD}/src/[^ :\"]+\.dylib" /tmp/nbt-link-nodf.sh | sort -u | while read -r p; do resolve_dylib "$p"; done
grep -oE '(^|[[:space:]])src/[^ ]+\.dylib' /tmp/nbt-link-nodf.sh | sed 's/^[[:space:]]*//' | sort -u | while read -r rp; do resolve_dylib "$BD/$rp"; done
sort -u /tmp/nbt-missing-dylibs.txt -o /tmp/nbt-missing-dylibs.txt
if [ -s /tmp/nbt-missing-dylibs.txt ]; then
    echo "[$TARGET] MISSING dylib inputs ($(wc -l < /tmp/nbt-missing-dylibs.txt)):" >&2
    cat /tmp/nbt-missing-dylibs.txt >&2
    exit 1
fi
if [ -s /tmp/nbt-dylib-rewrite.sed ]; then
    sed -f /tmp/nbt-dylib-rewrite.sed /tmp/nbt-link.sh > /tmp/nbt-link.sh.rw && mv /tmp/nbt-link.sh.rw /tmp/nbt-link.sh
    echo "[$TARGET] dylib inputs redirected to dyld-only: $(wc -l < /tmp/nbt-dylib-rewrite.sed)"
fi
# Control #142 step 4: libsystem_kernel references libsimple's lock symbols
# (src/libsimple/src/lock.c: _libsimple_lock_lock/_unlock and the four
# _libsimple_rwlock_*). The link carries src/libsimple-darling/liblibsimple_darling.a,
# but that archive is built by the cctools ar wrapper and its symbols do not
# resolve, so add the prebuilt fat lock.o object directly to the link.
LIBSIMPLE_O="${BD}/src/libsimple-darling/CMakeFiles/libsimple_darling.dir/src/lock.c.o"
if [ -f "$LIBSIMPLE_O" ] && ! grep -q -- "$LIBSIMPLE_O" /tmp/nbt-link.sh; then
    sed "s#src/libsimple-darling/liblibsimple_darling.a#${LIBSIMPLE_O} src/libsimple-darling/liblibsimple_darling.a#" /tmp/nbt-link.sh > /tmp/nbt-link.sh.ls && mv /tmp/nbt-link.sh.ls /tmp/nbt-link.sh
    echo "[$TARGET] added libsimple lock.o to the link ($(llvm-nm -gU "$LIBSIMPLE_O" | grep -c ' T _libsimple') libsimple symbols)"
fi
if grep -q -- "-arch i386 -arch x86_64" /tmp/nbt-link.sh; then
    # fat link: two thin links + lipo
    out=$(grep -oE '\-o [^ ]+' /tmp/nbt-link.sh | tail -1 | sed 's/-o //')
    sed -E "s/-arch i386 -arch x86_64/-arch i386/; s#-o ${out}#-o ${out}.i386#" /tmp/nbt-link.sh > /tmp/nbt-li.sh
    sed -E "s/-arch i386 -arch x86_64/-arch x86_64/; s#-o ${out}#-o ${out}.x86_64#" /tmp/nbt-link.sh > /tmp/nbt-lx.sh
    sh /tmp/nbt-li.sh
    sh /tmp/nbt-lx.sh
    llvm-lipo -create -output "$out" "${out}.i386" "${out}.x86_64"
    echo "[$TARGET] fat dylib: $out"
else
    sh /tmp/nbt-link.sh
    echo "[$TARGET] linked/archived"
fi
