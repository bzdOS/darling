#!/bin/sh
# apply-patches.sh - apply build-freebsd/patches/series to the submodule working trees, idempotently.
# One line per patch: APPLIED (applied now), ALREADY (was applied), FAIL (neither applies nor reverses).
# Exit 1 on any FAIL. Env: DARLING_SRC_DIR (default: repository root), PATCH_EXT_DIR
# (directory holding the submodules, default $DARLING_SRC_DIR/src/external).
SRC="${DARLING_SRC_DIR:-$(cd "$(dirname "$0")/.." && pwd)}"
PDIR="$SRC/build-freebsd/patches"
EXT="${PATCH_EXT_DIR:-$SRC/src/external}"
rc=0
while read -r sub patch; do
	case "$sub" in ''|'#'*) continue ;; esac
	d="$EXT/$sub"; p="$PDIR/$patch"
	if [ ! -f "$p" ] || [ ! -d "$d" ]; then echo "FAIL $patch (missing patch or $sub)"; rc=1; continue; fi
	if git -C "$d" apply --reverse --check "$p" >/dev/null 2>&1; then
		echo "ALREADY $patch"
	elif git -C "$d" apply --check "$p" >/dev/null 2>&1 && git -C "$d" apply "$p"; then
		echo "APPLIED $patch"
	else
		echo "FAIL $patch"; rc=1
	fi
done < "$PDIR/series"
exit $rc
