#!/bin/sh
# catx-keys-probe.sh - the key Chrome passes to +[CATransaction setValue:forKey:] (kCATransactionDisableActions) must not be NULL
# in any image that exports it: a NULL key raises "CATransaction is not key value coding compliant for the key (null)".
# Static check of the overlay images (no run, no lock): every image of the flat lookup order that exports the symbol
# (darling-extras.dylib first, then the QuartzCore stand-ins) must hold a non-null object (rebase or bind counts).
# Usage: sh build-freebsd/catx-keys-probe.sh [overlay-dir] [symbol]    Env: DARLING_SRC_DIR DARLING_OVERLAY
# One verdict line: CATX-KEYS pass|fail key=<sym> val=<non-null|null> src=<image>; exit 0 on pass.
SRC="${DARLING_SRC_DIR:?}"; OD="${1:-${DARLING_OVERLAY:?}}"; KEY="${2:-_kCATransactionDisableActions}"
export PATH="/usr/local/bin:/usr/local/sbin:/usr/bin:/bin:/sbin:/usr/sbin"
C="$SRC/build-freebsd/data-shadow-census.py"
src=none; val=absent; bad=0
for img in usr/lib/darling-extras.dylib usr/lib/QuartzCoreExtras.dylib System/Library/Frameworks/QuartzCore.framework/Versions/A/QuartzCore; do
	[ -f "$OD/$img" ] || continue
	v=$(python3 -I "$C" value "$OD/$img" "$KEY" 2>/dev/null)
	[ "$v" = absent ] && continue
	[ "$src" = none ] && { src=$img; val=$v; }
	[ "$v" = null ] && { [ "$bad" = 0 ] && { src=$img; val=null; }; bad=1; }
done
src=${src##*/}
if [ "$bad" = 0 ] && [ "$val" = non-null ]; then echo "CATX-KEYS pass key=$KEY val=$val src=$src"; exit 0; fi
echo "CATX-KEYS fail key=$KEY val=$val src=$src"; exit 1
