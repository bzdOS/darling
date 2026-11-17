#!/bin/sh
# stage-gate.sh - md5 gate: built vs overlay vs staged copy for mldr and every rebuilt stub image.
# usage: stage-gate.sh            Env: DARLING_BUILD_DIR DARLING_OVERLAY; STAGED_OVERLAY (default /tmp/darling-local-overlay)
# mldr is run straight from $DARLING_BUILD_DIR/dserver/mldr-real/mldr (no overlay or staged copy), so its line is
# the installed md5 only. Stub images: the list is stub-data/images.txt (written by build-stub-data.sh); the built
# file is stub-data/<basename with '.' -> '_'> where one exists. A staged copy that differs from the overlay is
# stale (launch runs with DARLING_KEEP_LOCAL=1 do not restage): it is refreshed from the overlay and re-checked.
# A built image that differs from the overlay (built, not installed) or a staged copy that stays different is a failure.
# The gated set is images.txt plus EXTRA_IMAGES (own-generator images that images.txt omits).
# Output: one "STAGECHK" line; exit 1 on any mismatch.
if [ -z "$DARLING_BUILD_DIR" ] || [ -z "$DARLING_OVERLAY" ]; then
	echo "STAGECHK FAIL missing env:$([ -z "$DARLING_BUILD_DIR" ] && printf ' DARLING_BUILD_DIR')$([ -z "$DARLING_OVERLAY" ] && printf ' DARLING_OVERLAY')"; exit 1
fi
SO="${STAGED_OVERLAY:-/tmp/darling-local-overlay}"; W="$DARLING_BUILD_DIR/stub-data"
[ -f "$DARLING_BUILD_DIR/stub-data/images.txt" ] || { echo "STAGECHK FAIL no $DARLING_BUILD_DIR/stub-data/images.txt (run build-stub-data.sh)"; exit 1; }
m() { md5 -q "$1" 2>/dev/null || echo none; }
ML=$(m "$DARLING_BUILD_DIR/dserver/mldr-real/mldr")
# images with their own generator that build-stub-data.sh leaves out of images.txt but whose md5 is still gated
# (SystemConfigurationExtras.dylib: build-sc-stubs.sh; it was counted until it was excluded from images.txt)
EXTRA_IMAGES="usr/lib/SystemConfigurationExtras.dylib"
n=0; bad=""; fixed=""; sel=""
while read -r p; do
	[ -n "$p" ] || continue
	r=${p#"$DARLING_OVERLAY"/}; base=$(basename "$p" | tr . _)
	o=$(m "$p"); s=$(m "$SO/$r"); b=$o
	[ -f "$W/$base" ] && b=$(m "$W/$base")
	if [ "$o" != "$s" ] && [ "$b" = "$o" ] && [ -f "$p" ]; then
		/usr/local/bin/sudo -n cp -p "$p" "$SO/$r" 2>/dev/null && { fixed="$fixed $r"; s=$(m "$SO/$r"); }
	fi
	n=$((n+1))
	if [ "$o" != "$s" ] || [ "$o" != "$b" ]; then bad="$bad $r(built=$b overlay=$o staged=$s)"; fi
	case "$r" in */Security.framework/*|usr/lib/AppKitExtras.dylib|usr/lib/FoundationExtras.dylib|usr/lib/IOKitExtras.dylib)
		sel="$sel $(basename "$r")=$o" ;; esac
done <<EOT
$(cat "$W/images.txt"; for x in $EXTRA_IMAGES; do grep -qx "$DARLING_OVERLAY/$x" "$W/images.txt" || echo "$DARLING_OVERLAY/$x"; done)
EOT
# CoreFoundation must carry its initializer (LC_ROUTINES or a __mod_init_func entry for ___CFInitialize): cfinit-check.sh --static.
# STAGE_GATE_CFINIT_RUN=1 also runs the guest probe (CFINIT-VERDICT line; it starts its own darlingserver, so it is off by default).
HERE=$(cd "$(dirname "$0")" && pwd); SRCD="${DARLING_SRC_DIR:-$(cd "$HERE/.." && pwd)}"
if ! DARLING_SRC_DIR="$SRCD" sh "$HERE/cfinit-check.sh" --static >/dev/null 2>&1; then
	echo "STAGECHK FAIL cfinit=FAIL (CoreFoundation has no LC_ROUTINES and no ___CFInitialize __mod_init_func entry; see build-freebsd/cfinit-check.sh)"; exit 1
fi
if [ "${STAGE_GATE_CFINIT_RUN:-0}" = 1 ]; then
	cfv=$(DARLING_SRC_DIR="$SRCD" sh "$HERE/cfinit-check.sh" 2>&1 | grep -a "^CFINIT-VERDICT" | tail -1); echo "$cfv"
	case "$cfv" in "CFINIT-VERDICT pass"*) ;; *) echo "STAGECHK FAIL cfinit=FAIL"; exit 1;; esac
fi
# The FSOPS probe (3 s) always runs; STAGE_GATE_FSOPS_RUN=0 skips it. It is the file system probe (FSOPS-VERDICT line: statfs/fstatfs, byte-range locks, fsync, mmap as a
# browser's SQLite databases use them; starts its own darlingserver). Needs tests/fsops-probe-macho (build-fsops-probe.sh).
if [ "${STAGE_GATE_FSOPS_RUN:-1}" = 1 ]; then
	fsv=$(DARLING_SRC_DIR="$SRCD" sh "$HERE/run-fsops-probe.sh" 2>&1 | grep -a "^FSOPS-VERDICT" | tail -1); echo "$fsv"
	case "$fsv" in "FSOPS-VERDICT pass"*) ;; *) echo "STAGECHK FAIL fsops=FAIL"; exit 1;; esac
fi
# STAGE_GATE_WINDOW_RUN=1 also runs the window draw probe (WINDOW-DRAW-VERDICT line: an NSWindow with a red background must
# map in an own headless sway and show that colour in a grim snapshot; own sway and darlingserver, off by default).
if [ "${STAGE_GATE_WINDOW_RUN:-0}" = 1 ]; then
	wdv=$(DARLING_SRC_DIR="$SRCD" sh "$HERE/window-draw-probe.sh" 2>&1 | grep -a "^WINDOW-DRAW-VERDICT"); echo "$wdv"
	# one line per window (plain, modal alert): both must pass
	[ "$(echo "$wdv" | grep -c "^WINDOW-DRAW-VERDICT pass")" = 2 ] && ! echo "$wdv" | grep -q "verdict fail\|VERDICT fail" || { echo "STAGECHK FAIL window=FAIL"; exit 1; }
fi
# CoreGraphics must carry the NULL-safe CGColor accessors (cg-color-nullsafe.py); overlay and staged copy are both checked.
cgp=ok; CGR=System/Library/Frameworks/CoreGraphics.framework/Versions/A/CoreGraphics
for f in "$DARLING_OVERLAY/$CGR" "$SO/$CGR"; do
	[ -f "$f" ] || continue
	python3 "$HERE/cg-color-nullsafe.py" --check "$f" >/dev/null 2>&1 || cgp=FAIL
done
[ -f "$DARLING_OVERLAY/$CGR" ] || cgp=FAIL
if [ "$cgp" = FAIL ]; then
	echo "STAGECHK FAIL cgpatch=FAIL (CoreGraphics CGColor accessors not NULL-safe; run build-freebsd/cg-color-nullsafe.py on the overlay and staged copy)"; exit 1
fi
# No script here may stop processes by name (pkill/killall hit other users' processes): comments are ignored,
# this file is skipped (it names the words). NOKILL_DIR overrides the scanned directory (for tests).
nk=ok; NKD="${NOKILL_DIR:-$HERE}"
for f in "$NKD"/*.sh; do
	[ "$(basename "$f")" = stage-gate.sh ] && continue
	if sed 's/#.*//' "$f" 2>/dev/null | grep -Eq '(^|[^[:alnum:]_-])(pkill|killall)([^[:alnum:]_-]|$)'; then nk=FAIL; nkf="$nkf $(basename "$f")"; fi
done
if [ "$nk" = FAIL ]; then
	echo "STAGECHK FAIL nokill=FAIL (name-based process kill in:${nkf}; stop own processes by pid)"; exit 1
fi
# Disk guards must exist and be overridable (static check only: disk-guard-selftest.sh runs chrome-series.sh, which runs this gate).
dg=ok
grep -q SERIES_DISK_MAX "$HERE/chrome-series.sh" && grep -q SERIES_DISK_STOP "$HERE/chrome-sr-batch.sh" \
	&& [ -f "$HERE/disk-guard-selftest.sh" ] && sh -n "$HERE/disk-guard-selftest.sh" || dg=FAIL
if [ "$dg" = FAIL ]; then
	echo "STAGECHK FAIL diskguard=FAIL (SERIES_DISK_MAX/SERIES_DISK_STOP guard or disk-guard-selftest.sh missing)"; exit 1
fi
# Relink reproducibility: static flag check only (relink-repro-check.sh without --static does two relinks; opt-in).
sh "$HERE/relink-repro-check.sh" --static || { echo "STAGECHK FAIL relinkrepro=FAIL"; exit 1; }
if [ -z "$bad" ] && [ "$ML" != none ]; then
	echo "STAGECHK ok cfinit=ok cgpatch=$cgp nokill=$nk diskguard=$dg images=$n mldr=$ML restaged=[${fixed# }] key:$sel"; exit 0
fi
echo "STAGECHK FAIL images=$n mldr=$ML mismatches:${bad:- none}"; exit 1
