#!/bin/sh
# appearance-check.sh - static check: AppKit API Chrome sends is implemented, unknown selectors do not return garbage.
# usage: appearance-check.sh        Env: DARLING_SRC_DIR DARLING_OVERLAY CHROME_APP
# Two facts, one verdict line:
#   appearance: the NSAppearance family (every selector with "ppearance" in its name that Chrome's
#     __objc_selrefs holds and no overlay image defines, see appkit-selector-census.py) must be defined; the
#     [NSApp effectiveAppearance] result was a heap pointer (garbage) used as a receiver: "unknown class" abort.
#   forward: FoundationExtras.dylib must carry the forward handler of stub-frameworks/behaviors/FoundationExtras.dylib/forward.c
#     (marker OBJC-FWD-MISS and the _objc_setForwardHandler import); CoreFoundation's own handler returns RAX garbage.
# Output: APPEARANCE-CHECK ok|FAIL appearance_missing=<n>[names] forward=installed|absent
# Exit 1 on FAIL.
: "${DARLING_OVERLAY:?}" "${CHROME_APP:?}"
W=$(cd "$(dirname "$0")" && pwd)
FW=$(ls "$CHROME_APP"/Contents/Frameworks/*Framework.framework/Versions/[0-9]*/*Framework 2>/dev/null | head -1)
[ -n "$FW" ] || { echo "APPEARANCE-CHECK FAIL no Chrome framework under CHROME_APP"; exit 1; }
miss=$(python3 -I "$W/appkit-selector-census.py" "$FW" "$DARLING_OVERLAY" --list --grep 'ppearance' | sed 1d | tr '\n' ',' | sed 's/,$//')
n=0; [ -n "$miss" ] && n=$(printf '%s\n' "$miss" | tr ',' '\n' | grep -c .)
F="$DARLING_OVERLAY/usr/lib/FoundationExtras.dylib"
fwd=absent
if grep -aq "OBJC-FWD-MISS" "$F" && grep -aq "_objc_setForwardHandler" "$F"; then fwd=installed; fi
if [ "$n" -eq 0 ] && [ "$fwd" = installed ]; then
	echo "APPEARANCE-CHECK ok appearance_missing=0 forward=installed"; exit 0
fi
echo "APPEARANCE-CHECK FAIL appearance_missing=$n[$miss] forward=$fwd"; exit 1
