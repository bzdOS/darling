#!/bin/sh
# patches-clean-check.sh - verify the patch series applies on the committed submodule HEADs and
# that the result equals the working-tree content of the patched files.
# Exports each affected submodule's HEAD with git archive into a temp dir (working trees are
# only read). Completeness step: for every submodule in the series, each tracked change and each
# untracked file must belong to a file touched by a patch, unless listed in patches-artifacts.list.
# Prints one summary line; exit 1 on any failure.
SRC="${DARLING_SRC_DIR:-$(cd "$(dirname "$0")/.." && pwd)}"
PDIR="$SRC/build-freebsd/patches"
EXT="${PATCH_EXT_DIR:-$SRC/src/external}"
ALLOW="${PATCH_ARTIFACTS_LIST:-$SRC/build-freebsd/patches-artifacts.list}"
T=$(mktemp -d "${TMPDIR:-/tmp}/patches-clean.XXXXXX") || exit 1
trap 'rm -rf "$T"' EXIT INT TERM
n=0; bad=0; msg=""; : > "$T/files"
for sub in $(grep -v '^#' "$PDIR/series" | awk 'NF{print $1}' | grep -v '^[.][.]/[.][.]$' | sort -u); do
	mkdir "$T/$sub" || { bad=$((bad+1)); msg="$msg mkdir:$sub"; continue; }
	git -C "$EXT/$sub" rev-parse --verify -q HEAD >/dev/null || continue  # unborn submodule: empty base tree
	git -C "$EXT/$sub" archive HEAD | tar -xf - -C "$T/$sub" || { bad=$((bad+1)); msg="$msg archive:$sub"; continue; }
done
while read -r sub patch; do
	case "$sub" in ''|'#'*|../..) continue ;; esac  # ../.. = main-tree patch (applies at the repo root), not checked here
	n=$((n+1)); p="$PDIR/$patch"
	if ! git -C "$T/$sub" apply "$p" >/dev/null 2>&1; then
		# git apply outside a repo works on plain dirs; failure = not clean on HEAD
		bad=$((bad+1)); msg="$msg apply:$patch"; continue
	fi
	{ sed -n 's|^diff --git a/\(.*\) b/.*|\1|p' "$p"; grep '^+++ b/' "$p" | sed 's|^+++ b/||'; } | sort -u | sed "s|^|$sub |" >> "$T/files"
done < "$PDIR/series"
# compare only after the whole series is applied (a file can be touched by several patches)
while read -r sub f; do
	cmp -s "$T/$sub/$f" "$EXT/$sub/$f" || { bad=$((bad+1)); msg="$msg diff:$sub/$f"; }
done < "$T/files"
# completeness: changes in the working tree not covered by any patch
for sub in $(grep -v '^#' "$PDIR/series" | awk 'NF{print $1}' | grep -v '^[.][.]/[.][.]$' | sort -u); do
	{ git -C "$EXT/$sub" diff --name-only; git -C "$EXT/$sub" ls-files -o --exclude-standard; } | sort -u | while read -r f; do
		grep -qxF "$sub $f" "$T/files" && continue
		[ -f "$ALLOW" ] && awk -v s="$sub" -v f="$f" '$1==s && ($2=="*" || $2==f || (substr($2,length($2))=="/" && index(f,$2)==1)) {ok=1} END{exit !ok}' "$ALLOW" && continue
		echo "$sub $f" >> "$T/extra"
	done
done
# submodules outside the series: same check (an unlisted dirty submodule is a failure unless allowed)
if [ -f "$T/extra" ]; then
	k=$(wc -l < "$T/extra" | tr -d ' '); bad=$((bad+k)); msg="$msg uncovered:$(head -5 "$T/extra" | tr ' \n' ': ' )($k)"
fi
if [ $bad -eq 0 ]; then echo "PATCHES-CLEAN ok ($n patches apply on HEAD, match working tree, no uncovered changes)"; exit 0; fi
echo "PATCHES-CLEAN fail ($bad problems:$msg)"; exit 1
