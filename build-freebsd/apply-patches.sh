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
# A patch may be applied while a LATER patch of the same submodule shifted its context, so its own
# reverse --check fails. Detect that: copy the working-tree files touched by the submodule's patches into a
# scratch dir and reverse the series from the end; every patch that reverses in that chain is ALREADY applied.
# ../../ patches (mldr, etc.) touch the main tree, not a submodule — handle them as .._root with d=$SRC.
ALR=$(mktemp -d "${TMPDIR:-/tmp}/apply-already.XXXXXX") || exit 1
trap 'rm -rf "$ALR"' EXIT INT TERM
: > "$ALR/list"
for sub in $(grep -v '^#' "$PDIR/series" | awk 'NF{print $1}' | sort -u); do
	case "$sub" in '#'*|'') continue;; esac
	if [ "$sub" = "../.." ]; then d_base="$SRC"; else
		[ -d "$EXT/$sub" ] || continue; d_base="$EXT/$sub"
	fi
	pl=$(grep -v '^#' "$PDIR/series" | awk -v s="$sub" '$1==s{print $2}')
	mkdir -p "$ALR/w" || break
	for pt in $pl; do [ -f "$PDIR/$pt" ] && { sed -n 's|^diff --git a/\(.*\) b/.*|\1|p' "$PDIR/$pt" | cut -f1; grep '^+++ b/' "$PDIR/$pt" | sed 's|^+++ b/||' | cut -f1; }; done | sort -u | while IFS= read -r f; do
		if [ -e "$d_base/$f" ] || [ -L "$d_base/$f" ]; then mkdir -p "$ALR/w/$(dirname "$f")"; cp -P "$d_base/$f" "$ALR/w/$f"; fi
	done
	for pt in $(echo "$pl" | awk '{a[NR]=$0} END{for(i=NR;i>0;i--)print a[i]}'); do
		git -C "$ALR/w" apply --reverse "$PDIR/$pt" >/dev/null 2>&1 || break
		echo "$sub $pt" >> "$ALR/list"
	done
	mv "$ALR/w" "$ALR/w.$(echo "$sub" | tr '/' '_')"
done
while read -r sub patch; do
	case "$sub" in ''|'#'*) continue ;; esac
	if [ "$sub" = "../.." ]; then d="$SRC"; else d="$EXT/$sub"; fi; p="$PDIR/$patch"
	if [ ! -f "$p" ] || [ ! -d "$d" ]; then echo "FAIL $patch (missing patch or $sub)"; rc=1; continue; fi
	if git -C "$d" apply --reverse --check "$p" >/dev/null 2>&1; then
		echo "ALREADY $patch"
	elif grep -qxF "$sub $patch" "$ALR/list"; then
		echo "ALREADY $patch"
	elif [ "$patch" = "mldr-sig65-unblock-count.patch" ] && grep -q 'sigaddset.*65' "$SRC/src/startup/mldr/freebsd_syscall_trap.c" 2>/dev/null && grep -q 'mldr_sig65_tramp_count' "$SRC/src/startup/mldr/sigaction_translate.c" 2>/dev/null; then
		echo "ALREADY $patch (folded into pr-arm64 0ffc7e19c)"
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
