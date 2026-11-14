#!/bin/sh
# compare-dylib.sh -- compare two Mach-O dylibs by exports, binds and normalised
# disassembly (addresses, symbol names and file names removed). A byte compare is
# meaningless between two links: the debug map carries object paths, which shift
# every later offset.
# Usage: sh build-freebsd/compare-dylib.sh <a.dylib> <b.dylib> <scratch dir>
set -e
A="${1:?}"; B="${2:?}"; W="${3:?}"
mkdir -p "$W"
one() { # tag file
  llvm-nm -gU "$2" | awk '{print $2,$3}' | sort > "$W/$1.exports"
  llvm-objdump --macho --bind --lazy-bind --weak-bind "$2" 2>/dev/null | grep -v "^$2" | sed -E 's/^0x[0-9A-Fa-f]+ +//' | sort > "$W/$1.binds"
  llvm-objdump -d --no-show-raw-insn "$2" 2>/dev/null | grep -v "file format" | sed -E 's/^ *[0-9a-f]+:\t?//; s/0x[0-9a-f]+//g; s/<[^>]*>//g; s/^[0-9a-f]+ //' > "$W/$1.text"
}
one a "$A"
one b "$B"
echo "exports diff lines: $(diff "$W/a.exports" "$W/b.exports" | wc -l)"
echo "binds   diff lines: $(diff "$W/a.binds" "$W/b.binds" | wc -l)"
echo "text    diff lines: $(diff "$W/a.text" "$W/b.text" | wc -l)"
