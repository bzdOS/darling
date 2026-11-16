#!/bin/sh
# appearance-check.sh - static check: AppKit API Chrome sends is implemented, unknown selectors do not return garbage.
# usage: appearance-check.sh        Env: DARLING_SRC_DIR DARLING_OVERLAY CHROME_APP
# Two facts, one verdict line:
#   appearance: the NSAppearance family (every selector with "ppearance" in its name that Chrome's
#     __objc_selrefs holds and no overlay image defines, see appkit-selector-census.py) must be defined; the
#     [NSApp effectiveAppearance] result was a heap pointer (garbage) used as a receiver: "unknown class" abort.
#   forward: FoundationExtras.dylib must carry the forward handler of stub-frameworks/behaviors/FoundationExtras.dylib/forward.c
#     (marker OBJC-FWD-MISS and the _objc_setForwardHandler import); CoreFoundation's own handler returns RAX garbage.
#   screen: AppKitExtras.dylib must carry the NSScreen behaviour of stub-frameworks/behaviors/AppKitExtras.dylib/screen.c
#     (backingScaleFactor 1.0, colorSpace, localizedName, maximumFramesPerSecond 60, minimumRefreshInterval, the two
#     maximum*ExtendedDynamicRangeColorComponentValue 1.0): each of its seven selectors came back as OBJC-FWD-MISS and a zero.
# Output: APPEARANCE-CHECK ok|FAIL appearance_missing=<n>[names] forward=installed|absent screen_missing=<n>[names]
# Exit 1 on FAIL. Always one verdict line, also when a variable is missing or a helper fails (FAIL <reason>).
if [ -z "$DARLING_OVERLAY" ] || [ -z "$CHROME_APP" ]; then
	echo "APPEARANCE-CHECK FAIL missing env:$([ -z "$DARLING_OVERLAY" ] && printf ' DARLING_OVERLAY')$([ -z "$CHROME_APP" ] && printf ' CHROME_APP')"
	exit 1
fi
W=$(cd "$(dirname "$0")" && pwd)
FW=$(ls "$CHROME_APP"/Contents/Frameworks/*Framework.framework/Versions/[0-9]*/*Framework 2>/dev/null | head -1)
[ -n "$FW" ] || { echo "APPEARANCE-CHECK FAIL no Chrome framework under CHROME_APP"; exit 1; }
CENSUS=$(python3 -I "$W/appkit-selector-census.py" "$FW" "$DARLING_OVERLAY" --list --grep 'ppearance' 2>&1) || {
	echo "APPEARANCE-CHECK FAIL selector census failed: $(printf '%s\n' "$CENSUS" | tail -1)"; exit 1; }
miss=$(printf '%s\n' "$CENSUS" | sed 1d | tr '\n' ',' | sed 's/,$//')
n=0; [ -n "$miss" ] && n=$(printf '%s\n' "$miss" | tr ',' '\n' | grep -c .)
F="$DARLING_OVERLAY/usr/lib/FoundationExtras.dylib"
fwd=absent
if grep -aq "OBJC-FWD-MISS" "$F" && grep -aq "_objc_setForwardHandler" "$F"; then fwd=installed; fi
A="$DARLING_OVERLAY/usr/lib/AppKitExtras.dylib"
smiss=""; sn=0
for sel in backingScaleFactor colorSpace localizedName maximumFramesPerSecond minimumRefreshInterval \
	maximumExtendedDynamicRangeColorComponentValue maximumPotentialExtendedDynamicRangeColorComponentValue; do
	grep -aq "$sel" "$A" 2>/dev/null || { smiss="$smiss${smiss:+,}$sel"; sn=$((sn+1)); }
done
if [ "$n" -eq 0 ] && [ "$fwd" = installed ] && [ "$sn" -eq 0 ]; then
	echo "APPEARANCE-CHECK ok appearance_missing=0 forward=installed screen_missing=0"; exit 0
fi
echo "APPEARANCE-CHECK FAIL appearance_missing=$n[$miss] forward=$fwd screen_missing=$sn[$smiss]"; exit 1
