/* Shim: the Darling umbrella header pulls ATS/ColorSync/HIServices/QD, none ported. AppKit headers need the CoreGraphics and CoreText types from it. */
#import <CoreGraphics/CoreGraphics.h>
#import <CoreText/CoreText.h>
/* Carbon UnicodeUtilities type used by NSDisplay.h (CarbonCore is not ported). */
typedef struct UCKeyboardLayout UCKeyboardLayout;
