#!/bin/sh
# restore-submodule-snapshot.sh - restore snapshot-only submodules (see submodule-snapshots.lock).
# For each lock entry: if the submodule directory is empty (no files besides .git), verify the archive
# sha256 against the lock and unpack it; if it already has files, only compare with <name>.files.sha256
# and report; existing copies are never overwritten. Env: SUBMODULE_SNAPSHOT_DIR (directory holding
# <name>.tar, required), DARLING_SRC_DIR, PATCH_EXT_DIR. Exit 1 on any mismatch/error.
SRC="${DARLING_SRC_DIR:-$(cd "$(dirname "$0")/.." && pwd)}"
BF="$SRC/build-freebsd"
EXT="${PATCH_EXT_DIR:-$SRC/src/external}"
LOCK="$BF/submodule-snapshots.lock"
sha256_file() { if command -v sha256 >/dev/null 2>&1; then sha256 -q "$1"; else sha256sum "$1" | awk '{print $1}'; fi; }
sha256_stdin() { if command -v sha256 >/dev/null 2>&1; then sha256 -q; else sha256sum | awk '{print $1}'; fi; }
sha() { if [ -L "$1" ]; then printf 'L:%s' "$(readlink "$1")" | sha256_stdin; else sha256_file "$1"; fi; }

tree_sums() { # prints "<sha256> <path>" sorted for all files of dir $1 (without .git)
	( cd "$1" && find . -path ./.git -prune -o \( -type f -o -type l \) -print | LC_ALL=C sort | sed 's|^\./||' | while IFS= read -r f; do
		printf '%s %s\n' "$(sha "$f")" "$f"; done )
}
rc=0
while read -r name gitlink asha nfiles; do
	case "$name" in ''|'#'*) continue ;; esac
	d="$EXT/$name"
	[ -d "$d" ] || mkdir -p "$d" || { echo "FAIL $name (cannot create $d)"; rc=1; continue; }
	if [ -z "$(find "$d" -mindepth 1 -maxdepth 1 ! -name .git | head -1)" ]; then
		a="${SUBMODULE_SNAPSHOT_DIR:?set SUBMODULE_SNAPSHOT_DIR}/$name.tar"
		[ -f "$a" ] || { echo "FAIL $name (no $a)"; rc=1; continue; }
		[ "$(sha "$a")" = "$asha" ] || { echo "FAIL $name (archive sha256 differs from lock)"; rc=1; continue; }
		tar -xf "$a" -C "$d" || { echo "FAIL $name (unpack)"; rc=1; continue; }
		echo "RESTORED $name ($nfiles files, gitlink $gitlink)"
	fi
	if tree_sums "$d" | cmp -s - "$BF/$name.files.sha256"; then echo "OK $name"; else echo "DIFF $name (tree differs from $name.files.sha256; nothing touched)"; rc=1; fi
done < "$LOCK"
exit $rc
