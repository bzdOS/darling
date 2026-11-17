#!/bin/sh
# relink-repro-check.sh - relink libsystem_kernel twice (relink-libsystem-kernel-dirty.sh, no install, two
# different outdirs) and compare. Prints one line: RELINK-REPRO pass|fail diff=<none|what differs>; rc 0/1.
# Equal inputs must give a byte-identical dylib (the link strips debug stabs, whose object path/mtime and the
# content-hash UUID otherwise differ per run). Expensive (two compiles + links): opt-in.
# Static mode (used by stage-gate.sh): relink-repro-check.sh --static  -> checks the -Wl,-S reproducibility flag is present.
# Env: DARLING_SRC_DIR DARLING_BUILD_DIR DARLING_OVERLAY. Optional REPRO_KEEP=1 keeps the outdirs.
HERE=$(cd "$(dirname "$0")" && pwd)
if [ "$1" = --static ]; then
	grep -q -- "-Wl,-S " "$HERE/relink-libsystem-kernel-objs.sh" && exit 0
	echo "RELINK-REPRO fail diff=static (-Wl,-S missing in relink-libsystem-kernel-objs.sh)"; exit 1
fi
: "${DARLING_SRC_DIR:=$(cd "$HERE/.." && pwd)}"; export DARLING_SRC_DIR
: "${DARLING_BUILD_DIR:?}"; : "${DARLING_OVERLAY:?}"; export DARLING_BUILD_DIR DARLING_OVERLAY
T=$(mktemp -d "${TMPDIR:-/tmp}/relink-repro.XXXXXX") || exit 1
for i in a b; do
	INSTALL=0 sh "$HERE/relink-libsystem-kernel-dirty.sh" "$T/$i" > "$T/$i.log" 2>&1 || { echo "RELINK-REPRO fail diff=relink-$i-failed"; tail -3 "$T/$i.log" >&2; exit 1; }
done
A=$T/a/libsystem_kernel.dylib; B=$T/b/libsystem_kernel.dylib
if cmp -s "$A" "$B"; then r=pass; d=none
else
	r=fail; d=""
	[ "$(wc -c < "$A")" = "$(wc -c < "$B")" ] || d="$d size"
	cmp -s "$T/a/objects.txt" "$T/b/objects.txt" || d="$d objectset"
	llvm-objdump --macho --private-headers "$A" | grep -v uuid > "$T/ha"; llvm-objdump --macho --private-headers "$B" | grep -v uuid > "$T/hb"
	cmp -s "$T/ha" "$T/hb" || d="$d loadcmds"
	llvm-objdump --macho -d "$A" | sed 1d > "$T/da"; llvm-objdump --macho -d "$B" | sed 1d > "$T/db"
	cmp -s "$T/da" "$T/db" || d="$d code"
	llvm-objdump --macho --private-headers "$A" | grep uuid > "$T/ua"; llvm-objdump --macho --private-headers "$B" | grep uuid > "$T/ub"
	cmp -s "$T/ua" "$T/ub" || d="$d uuid"
	llvm-nm -n "$A" | grep -v relink-repro > "$T/na"; llvm-nm -n "$B" | grep -v relink-repro > "$T/nb"
	cmp -s "$T/na" "$T/nb" || d="$d symbols"
	d=${d# }; [ -n "$d" ] || d="other(linkedit/debug)"
fi
echo "RELINK-REPRO $r diff=$d"
[ "${REPRO_KEEP:-0}" = 1 ] && echo "kept $T" >&2 || rm -rf "$T"
[ $r = pass ]
