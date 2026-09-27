/* Empty UnicodeUtilities.h stub.
 *
 * Two files in this tree include <CarbonCore/UnicodeUtilities.h>:
 * AppKit/include/AppKit/NSDisplay.h (which every display backend, Wayland
 * included, pulls in) and CoreGraphics/include/CoreGraphics/CGSKeyboardLayout.h.
 * Neither one uses a single symbol from it -- the include is dead weight in
 * both. The real header carries the Unicode collation API (uc_* / UCOL_*),
 * which nothing in this tree calls, so declaring any of it here would be
 * inventing definitions rather than restoring real ones.
 *
 * If a future keymap or text-encoding change actually needs the collation
 * API, replace this with the real header from the same MacOSX10.15.sdk the
 * rest of tests/vendor/ is sourced from (CarbonCore.framework/Versions/A/
 * Headers/UnicodeUtilities.h) instead of growing this file.
 */
#ifndef UNICODEUTILITIES_H_
#define UNICODEUTILITIES_H_

#endif /* UNICODEUTILITIES_H_ */
