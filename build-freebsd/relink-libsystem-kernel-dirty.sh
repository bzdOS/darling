#!/bin/sh
# relink-libsystem-kernel-dirty.sh -- rebuild libsystem_kernel.dylib from EVERY source the xnu tree changes
# (modified or added .c/.S under libsystem_kernel, libsyscall and gen/bsdsyscalls in the xnu submodule, plus the generated
# bsdsyscalls stubs that build-freebsd/patches touch), via relink-libsystem-kernel-objs.sh. libsyscall is scanned because
# libsyscall/mach/mach_init.c was once missed: _mach_fork_parent kept its stale body (no failed-fork check) and a failed
# fork blocked the parent in fork_wait_for_child forever. Relinking only the object of the
# latest edit leaves every earlier fix at the tree's stale object (an installed dylib silently lost the
# sysctl string-length fix that way). Writes <outdir>/libsystem_kernel.dylib; with INSTALL=1 copies it over
# $DARLING_OVERLAY/usr/lib/system/libsystem_kernel.dylib (previous copy kept as <outdir>/libsystem_kernel.dylib.prev).
# Usage: DARLING_SRC_DIR=... DARLING_BUILD_DIR=... DARLING_OVERLAY=... [INSTALL=1] sh build-freebsd/relink-libsystem-kernel-dirty.sh <outdir>
set -e
SRC="${DARLING_SRC_DIR:?}"; BD="${DARLING_BUILD_DIR:?}"; OD="${DARLING_OVERLAY:?}"
OUT="${1:?usage: relink-libsystem-kernel-dirty.sh <outdir>}"
export PATH="${BD}/src/external/cctools-port/cctools/misc:/usr/local/bin:${PATH}"
X="${SRC}/src/external/xnu"
TARGET=src/external/xnu/darling/src/libsystem_kernel/libsystem_kernel.dylib
mkdir -p "$OUT"
( cd "$BD" && ninja -t commands "$TARGET" ) > "$OUT/all-commands.txt"
{ ( cd "$X" && git status --short -- darling/src/libsystem_kernel libsyscall gen/bsdsyscalls | sed -n 's/^.. //p' )
  grep -h '^+++ b/.*bsdsyscalls/.*\.S$' "${SRC}/build-freebsd/patches/"xnu-*.patch 2>/dev/null | sed 's/^+++ b\///'; } |
while read -r f; do
	case "$f" in *.c|*.S) ;; *) continue;; esac
	b=$(basename "$f")
	# the build command whose source file has this name and path ending (any dir under libsystem_kernel)
	o=$(grep -o -- "-o [^ ]*\.o -c [^ ]*/${b}\$" "$OUT/all-commands.txt" | sed -n 's/^-o \([^ ]*\) .*/\1/p' | head -1)
	if [ -n "$o" ]; then echo "$o"; else
		# a source added by a patch has no build command: clone one from a sibling .c in the same directory
		case "$f" in *.c) d=$(dirname "$f"); sib=$(grep -o -- "-o [^ ]*\.o -c [^ ]*/${d#darling/src/libsystem_kernel/}/[^/ ]*\.c\$" "$OUT/all-commands.txt" | sed -n 's/^-o \([^ ]*\) .*/\1/p' | head -1)
			[ -n "$sib" ] && echo "NEW:$(dirname "$sib")/${b}.o:$sib";; esac
	fi
done | sort -u > "$OUT/objects.txt"
[ -s "$OUT/objects.txt" ] || { echo "FATAL: no changed libsystem_kernel objects found" >&2; exit 1; }
echo "objects: $(wc -l < "$OUT/objects.txt")"
# shellcheck disable=SC2046
sh "${SRC}/build-freebsd/relink-libsystem-kernel-objs.sh" "$OUT" $(cat "$OUT/objects.txt")
if [ "${INSTALL:-0}" = 1 ]; then
	D="${OD}/usr/lib/system/libsystem_kernel.dylib"
	cp -p "$D" "$OUT/libsystem_kernel.dylib.prev"
	cp "$OUT/libsystem_kernel.dylib" "$D"
	echo "installed $D"
fi
