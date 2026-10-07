#!/bin/sh
# Control #120-2: per-arch object build (multi-arch clang hides its per-arch .o
# in a temp dir and needs `lipo`, which never produced them here). Compile each
# object explicitly per slice, then lipo the two thin objects into the fat .o
# the link expects.
set -e
BD="${DARLING_BUILD_DIR:?set DARLING_BUILD_DIR}"
SRC="${DARLING_SRC_DIR:?set DARLING_SRC_DIR}"
cd "$BD"
CCTOOLS_MISC="${BD}/src/external/cctools-port/cctools/misc"
PATH="${CCTOOLS_MISC}:${PATH}"; export PATH

ninja -t commands src/external/xnu/darling/src/libsystem_kernel/libsystem_kernel.dylib \
  | grep "xnu/darling/src/libsystem_kernel" \
  | grep -- "-c " > /tmp/kc-cmds.sh
sed -i.bak "s# -c # -I${SRC}/build-host-tools -c #" /tmp/kc-cmds.sh; rm -f /tmp/kc-cmds.sh.bak
echo "object commands: $(wc -l < /tmp/kc-cmds.sh)"

: > /tmp/kc-perarch.sh
: > /tmp/kc-lipo.sh
while IFS= read -r cmd; do
    obj=$(printf '%s\n' "$cmd" | sed -E 's/.* -o ([^ ]+) .*/\1/')
    src=$(printf '%s\n' "$cmd" | sed -E 's/.* -c (.*)/\1/')
    base=$(printf '%s\n' "$cmd" | sed -E 's/ -arch [^ ]+//g; s# -o [^ ]+##; s# -c .*##')
    printf 'mkdir -p %s\n' "$(dirname "$obj")" >> /tmp/kc-perarch.sh
    printf '%s -arch i386 -o %s.i386.o -c %s\n' "$base" "$obj" "$src" >> /tmp/kc-perarch.sh
    printf '%s -arch x86_64 -o %s.x86_64.o -c %s\n' "$base" "$obj" "$src" >> /tmp/kc-perarch.sh
    printf 'lipo -create -output %s %s.i386.o %s.x86_64.o\n' "$obj" "$obj" "$obj" >> /tmp/kc-lipo.sh
done < /tmp/kc-cmds.sh

echo "per-arch compile commands: $(wc -l < /tmp/kc-perarch.sh)"
sh /tmp/kc-perarch.sh
echo "per-arch compile done; lipo..."
sh /tmp/kc-lipo.sh
echo "lipo done; fat objects built"

# ── link: two thin ld -dylib (one per slice) then lipo into the fat dylib ──
LD64=""
for c in "${SRC}/build-host-tools/ld64/x86_64-apple-darwin20-ld" \
         "${BD}/dyld-only/src/external/cctools-port/cctools/ld64/src/x86_64-apple-darwin20-ld" \
         "${BD}/src/external/cctools-port/cctools/ld64/src/x86_64-apple-darwin20-ld"; do
    [ -x "$c" ] && { LD64="$c"; break; }
done
DYL=src/external/xnu/darling/src/libsystem_kernel/libsystem_kernel.dylib
ninja -t commands "$DYL" | grep -v -- '-c ' | grep -- "$DYL" | tail -1 > /tmp/kc-link.sh
if [ -n "$LD64" ]; then
    sed -i.bak "s#${BD}/src/external/cctools-port/cctools/ld64/src/x86_64-apple-darwin20-ld#${LD64}#g" /tmp/kc-link.sh
    rm -f /tmp/kc-link.sh.bak
fi
sed -E "s/-arch i386 -arch x86_64/-arch i386/; s#-o ${DYL}#-o ${DYL}.i386.dylib#" /tmp/kc-link.sh > /tmp/kc-li.sh
sed -E "s/-arch i386 -arch x86_64/-arch x86_64/; s#-o ${DYL}#-o ${DYL}.x86_64.dylib#" /tmp/kc-link.sh > /tmp/kc-lx.sh
echo "=== thin ld i386 ==="; sh /tmp/kc-li.sh
echo "=== thin ld x86_64 ==="; sh /tmp/kc-lx.sh
echo "=== lipo -create ==="; lipo -create -output "$DYL" "${DYL}.i386.dylib" "${DYL}.x86_64.dylib"
lipo -info "$DYL"
