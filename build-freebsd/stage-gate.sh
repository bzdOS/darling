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
if [ -z "$bad" ] && [ "$ML" != none ]; then
	echo "STAGECHK ok images=$n mldr=$ML restaged=[${fixed# }] key:$sel"; exit 0
fi
echo "STAGECHK FAIL images=$n mldr=$ML mismatches:${bad:- none}"; exit 1
