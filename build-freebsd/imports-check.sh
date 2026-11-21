#!/bin/sh
# usage: imports-check.sh <macho> <overlay> -- prints MISSING <lib> <symbol>; libSystem hits can be false (two reexport levels)
# imports-check.sh <macho> <overlay>: undefined symbols of <macho> not exported by their library (one reexport level)
B="$1"; O="$2"
llvm-objdump --macho --dylibs-used "$B" | sed -n 's/^[[:space:]]*\(\/[^ ]*\) (.*/\1/p' > ${TMPDIR:-/tmp}/ic-$$.libs
llvm-nm -m -u "$B" | sed -n 's/.*external \([^ ]*\) (from \([^)]*\)).*/\2 \1/p' > ${TMPDIR:-/tmp}/ic-$$.und
while read -r lib; do
  short=$(basename "$lib" | sed 's/\..*//')
  f="$O$lib"; [ -f "$f" ] || { echo "NOLIB $lib"; continue; }
  { llvm-nm -gU "$f"; llvm-objdump --macho --dylibs-used "$f" | grep reexport | sed -n 's/^[[:space:]]*\(\/[^ ]*\) (.*/\1/p' | while read -r r; do llvm-nm -gU "$O$r"; done; } 2>/dev/null | awk '{print $NF}' | sort -u > ${TMPDIR:-/tmp}/ic-$$.exp
  awk -v s="$short" '$1==s{print $2}' ${TMPDIR:-/tmp}/ic-$$.und | sort -u | comm -23 - ${TMPDIR:-/tmp}/ic-$$.exp | sed "s|^|MISSING $short |"
done < ${TMPDIR:-/tmp}/ic-$$.libs
