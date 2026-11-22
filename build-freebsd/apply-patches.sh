#!/bin/sh
# apply-patches.sh - apply build-freebsd/patches/series to the submodule working trees, idempotently.
# One line per patch: APPLIED (applied now), ALREADY (was applied), FAIL (neither applies nor reverses).
# REPLACED <patch> <commit>: the tree held an earlier committed version of an edited patch (one of the last 5
# commits touching it); that version was reversed and the current one applied. Nothing is committed anywhere.
# Exit 1 on any FAIL. Env: DARLING_SRC_DIR (default: repository root), PATCH_EXT_DIR
# (directory holding the submodules, default $DARLING_SRC_DIR/src/external).
SRC="${DARLING_SRC_DIR:-$(cd "$(dirname "$0")/.." && pwd)}"
PDIR="$SRC/build-freebsd/patches"
EXT="${PATCH_EXT_DIR:-$SRC/src/external}"
rc=0
# snapshot-only submodules (file, glut): restore when empty, silent when present and matching
QUIET=1 DARLING_SRC_DIR="$SRC" PATCH_EXT_DIR="$EXT" sh "$SRC/build-freebsd/restore-submodule-snapshot.sh" || rc=1
while read -r sub patch; do
	case "$sub" in ''|'#'*) continue ;; esac
	d="$EXT/$sub"; p="$PDIR/$patch"
	if [ ! -f "$p" ] || [ ! -d "$d" ]; then echo "FAIL $patch (missing patch or $sub)"; rc=1; continue; fi
	if git -C "$d" apply --reverse --check "$p" >/dev/null 2>&1; then
		echo "ALREADY $patch"
	elif git -C "$d" apply --check "$p" >/dev/null 2>&1 && git -C "$d" apply "$p"; then
		echo "APPLIED $patch"
	else
		done_=
		for c in $(git -C "$SRC" log -n 5 --format=%h -- "build-freebsd/patches/$patch" 2>/dev/null); do
			old=$(mktemp) || break
			if git -C "$SRC" show "$c:build-freebsd/patches/$patch" > "$old" 2>/dev/null && ! cmp -s "$old" "$p" \
			   && git -C "$d" apply --reverse --check "$old" >/dev/null 2>&1; then
				git -C "$d" apply --reverse "$old"
				if git -C "$d" apply --check "$p" >/dev/null 2>&1 && git -C "$d" apply "$p"; then
					echo "REPLACED $patch $c"; done_=1
				else
					git -C "$d" apply "$old"; echo "FAIL $patch (old $c reversed, new does not apply; old restored)"; done_=2
				fi
			fi
			rm -f "$old"
			[ -n "$done_" ] && break
		done
		[ -z "$done_" ] && echo "FAIL $patch"
		[ "$done_" = 1 ] || rc=1
	fi
done < "$PDIR/series"
exit $rc
