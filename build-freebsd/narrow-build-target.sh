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
sed -i.bak "s# -c # -I${SRC}/build-host-tools -c #" /tmp/nbt-cmds.sh; rm -f /tmp/nbt-cmds.sh.bak
echo "[$TARGET] object commands: $(wc -l < /tmp/nbt-cmds.sh)"

: > /tmp/nbt-perarch.sh; : > /tmp/nbt-lipo.sh
while IFS= read -r cmd; do
    obj=$(printf '%s\n' "$cmd" | sed -E 's/.* -o ([^ ]+) .*/\1/')
    src=$(printf '%s\n' "$cmd" | sed -E 's/.* -c (.*)/\1/')
    base=$(printf '%s\n' "$cmd" | sed -E 's/ -arch [^ ]+//g; s# -o [^ ]+##; s# -c .*##')
    printf 'mkdir -p %s\n' "$(dirname "$obj")" >> /tmp/nbt-perarch.sh
    printf '%s -arch i386 -o %s.i386.o -c %s\n' "$base" "$obj" "$src" >> /tmp/nbt-perarch.sh
    printf '%s -arch x86_64 -o %s.x86_64.o -c %s\n' "$base" "$obj" "$src" >> /tmp/nbt-perarch.sh
    printf 'llvm-lipo -create -output %s %s.i386.o %s.x86_64.o\n' "$obj" "$obj" "$obj" >> /tmp/nbt-lipo.sh
done < /tmp/nbt-cmds.sh
sh /tmp/nbt-perarch.sh
sh /tmp/nbt-lipo.sh
echo "[$TARGET] objects done"

# link/archive command: the last non-compile command
ninja -t commands "$TARGET" | grep -v -- " -c " | grep -v '^cd ' | tail -1 > /tmp/nbt-link.sh
# cctools ar/ranlib are shell wrappers that produce no file here; llvm-ar/ranlib
# build the Mach-O archive. (Control #121)
sed -i.bak -E "s#${BD}/src/external/cctools-port/cctools/ar/x86_64-apple-darwin20-ar#llvm-ar#g; s#${BD}/src/external/cctools-port/cctools/ar/x86_64-apple-darwin20-ranlib#llvm-ranlib#g" /tmp/nbt-link.sh
rm -f /tmp/nbt-link.sh.bak
if [ -n "$LD64" ]; then
    sed -i.bak "s#${BD}/src/external/cctools-port/cctools/ld64/src/x86_64-apple-darwin20-ld#${LD64}#g" /tmp/nbt-link.sh
    rm -f /tmp/nbt-link.sh.bak
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
