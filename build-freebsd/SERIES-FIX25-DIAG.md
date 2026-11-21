# SERIES-FIX25: fix24 17:02Z rc133 class — 25 OBJC-FWD-MISS selectors

Diagnostics for one series in the style of `SERIES-FIX24-DIAG.md`: measured facts and
reproducible commands only. The hypothesis at the end is marked as a hypothesis.

Placeholders: `$DARLING_SRC_DIR` = source tree, `$DARLING_OVERLAY` = overlay with built
images, `$DARLING_BUILD_DIR` = build/log directory.

## Provenance (fix24 series, 17:02Z)

Series log: `$DARLING_BUILD_DIR/fix24-series-1120c.log`
Run logs: `$DARLING_BUILD_DIR/chrome-sr-fix24{1..10}.log`
Summary: `$DARLING_BUILD_DIR/../series-summaries/fix24.txt`

LAUNCH line (excerpt, `grep -n '^LAUNCH' "$DARLING_BUILD_DIR"/fix24-series-1120c.log`):

```text
LAUNCH fix24 10 120 start=2026-11-20T17:02:17Z driver_md5=6c7711b4fdb1ef6d0e4d209bea7fcefd git_head=4e1330c3a6fe6d93fb1f3dd7a3aff8d11f1fc735
```

SERIES/SUMMARY lines (excerpts):

```text
$ grep -n '^SERIES fix24\|^FWDMISS' "$DARLING_BUILD_DIR"/fix24-series-1120c.log
54:FWDMISS +[ASWebAuthenticationSessionWebBrowserSessionManager sharedManager], +[CALayer layer], +[NSAccessibilityRemoteUIElement remoteTokenForLocalUIElement:], +[NSEvent pressedMouseButtons], +[NSImage imageWithSystemSymbolName:accessibilityDescription:], +[NSViewBackingLayer layer], +[UTType typeWithMIMEType:], -[BrowserNativeWidgetWindow addTitlebarAccessoryViewController:], -[BrowserNativeWidgetWindow convertPointFromScreen:], -[BrowserNativeWidgetWindow setAnimationBehavior:], -[BrowserNativeWidgetWindow setTitlebarAppearsTransparent:], -[CALayer setAnchorPoint:], -[CALayer setAutoresizingMask:], -[CALayer setGeometryFlipped:], -[CALayerContext setLayer:], -[CALayerContext setSubwindow:], -[NSConstantDictionary _encodingCantBeStoredInEightBitCFString], -[NSConstantDictionary getCharacters:range:], -[NSFileManager ubiquityIdentityToken], -[NSKVONotifying_BrowserCrApplication isFullKeyboardAccessEnabled], -[NSTitlebarAccessoryViewController setFullScreenMinHeight:], -[NSTitlebarAccessoryViewController setHidden:], -[NSTitlebarAccessoryViewController setLayoutAttribute:], -[NSUserNotificationCenter deliveredNotifications], -[__NSCFDictionary symbolicTraits]
55:SERIES fix24 N=10 valid=10 survived20=5 top=rc133x10 windows=0 fwdmiss=25 mldr=68a3c9ede158fdee2aea9749f794fab6 args=""

$ cat "$DARLING_BUILD_DIR"/../series-summaries/fix24.txt
# 2026-11-20T17:08:41Z
SERIES fix24 N=10 valid=10 survived20=5 top=rc133x10 windows=0 fwdmiss=25 mldr=68a3c9ede158fdee2aea9749f794fab6 args=""
run 1 valid rc=133 dur=26
run 2 valid rc=133 dur=91
run 3 valid rc=133 dur=15
run 4 valid rc=133 dur=15
run 5 valid rc=133 dur=22
run 6 valid rc=133 dur=21
run 7 valid rc=133 dur=12
run 8 valid rc=133 dur=16
run 9 valid rc=133 dur=12
run 10 valid rc=133 dur=36
```

Log checksums (md5, provenance):

```text
md5 $DARLING_BUILD_DIR/fix24-series-1120c.log      dd099aa538ae5e5ee72718b1b4d31d6c
md5 $DARLING_BUILD_DIR/chrome-sr-fix241.log        9963221b557cd389d66232cc56947ca7
md5 $DARLING_BUILD_DIR/chrome-sr-fix242.log        8cecc5d93f0d76023cc8f5d9f4bb386a
md5 $DARLING_BUILD_DIR/chrome-sr-fix243.log        47aba936f21dea54a3ec9a6454048ea0
md5 $DARLING_BUILD_DIR/chrome-sr-fix244.log        1e205f071e5dfe93523c42444dd7b029
md5 $DARLING_BUILD_DIR/chrome-sr-fix245.log        c17897d71e829094d038d22dd9ce458f
md5 $DARLING_BUILD_DIR/chrome-sr-fix246.log        0d9997031807ccfb47f817030e0f2446
md5 $DARLING_BUILD_DIR/chrome-sr-fix247.log        09d07e00776990f23dd7f582b3a6ba64
md5 $DARLING_BUILD_DIR/chrome-sr-fix248.log        f2703ed3c422c0e4e156c5e95fee58c2
md5 $DARLING_BUILD_DIR/chrome-sr-fix249.log        5e73d901e7b3a023e04eb491b67fdf2b
md5 $DARLING_BUILD_DIR/chrome-sr-fix2410.log       3b83aaf17b87ff2bcab30edfaa2ce0f7
md5 $DARLING_BUILD_DIR/../series-summaries/fix24.txt  2c3749da2d5e33b707abccde4f7d07df
```

## Selector collection and convergence

Commands:

```sh
grep -ah 'OBJC-FWD-MISS' "$DARLING_BUILD_DIR"/chrome-sr-fix24{1..10}.log | sort | uniq -c | sort -rn
grep -ah 'OBJC-FWD-MISS' "$DARLING_BUILD_DIR"/chrome-sr-fix24{1..10}.log | sort -u | wc -l   # unique selectors
grep -ah 'OBJC-FWD-MISS' "$DARLING_BUILD_DIR"/chrome-sr-fix24{1..10}.log | wc -l                # total hits
for i in 1 2 3 4 5 6 7 8 9 10; do grep -a 'OBJC-FWD-MISS' "$DARLING_BUILD_DIR/chrome-sr-fix24${i}.log" | wc -l; done
```

Measured output:

```text
unique selectors = 25
total hits       = 250
SUMMARY fwdmiss  = 25   (matches unique selector count)
per-run hits     = 25,25,25,25,25,25,25,25,25,25  (sum 250)
```

Each `OBJC-FWD-MISS` is logged once per class×selector pair per process
(`build-freebsd/stub-frameworks/behaviors/FoundationExtras.dylib/forward.c`), so
frequency = number of runs where the selector appeared. All 25 appear in every run.

## Table: all 25 unique selectors with frequencies

| # | selector | freq (runs/10) | cluster (owner) |
|---|----------|-----------------|-----------------|
| 1 | `+[ASWebAuthenticationSessionWebBrowserSessionManager sharedManager]` | 10 | AuthenticationServices |
| 2 | `+[CALayer layer]` | 10 | QuartzCore |
| 3 | `+[NSAccessibilityRemoteUIElement remoteTokenForLocalUIElement:]` | 10 | AppKit |
| 4 | `+[NSEvent pressedMouseButtons]` | 10 | AppKit |
| 5 | `+[NSImage imageWithSystemSymbolName:accessibilityDescription:]` | 10 | AppKit |
| 6 | `+[NSViewBackingLayer layer]` | 10 | QuartzCore (inherits `CALayer`) |
| 7 | `+[UTType typeWithMIMEType:]` | 10 | UniformTypeIdentifiers |
| 8 | `-[BrowserNativeWidgetWindow addTitlebarAccessoryViewController:]` | 10 | AppKit (NSWindow subclass) |
| 9 | `-[BrowserNativeWidgetWindow convertPointFromScreen:]` | 10 | AppKit (NSWindow subclass) |
| 10 | `-[BrowserNativeWidgetWindow setAnimationBehavior:]` | 10 | AppKit (NSWindow subclass) |
| 11 | `-[BrowserNativeWidgetWindow setTitlebarAppearsTransparent:]` | 10 | AppKit (NSWindow subclass) |
| 12 | `-[CALayer setAnchorPoint:]` | 10 | QuartzCore |
| 13 | `-[CALayer setAutoresizingMask:]` | 10 | QuartzCore |
| 14 | `-[CALayer setGeometryFlipped:]` | 10 | QuartzCore |
| 15 | `-[CALayerContext setLayer:]` | 10 | QuartzCore |
| 16 | `-[CALayerContext setSubwindow:]` | 10 | QuartzCore |
| 17 | `-[NSConstantDictionary _encodingCantBeStoredInEightBitCFString]` | 10 | CoreFoundationExtras/Foundation |
| 18 | `-[NSConstantDictionary getCharacters:range:]` | 10 | CoreFoundationExtras/Foundation |
| 19 | `-[NSFileManager ubiquityIdentityToken]` | 10 | Foundation |
| 20 | `-[NSKVONotifying_BrowserCrApplication isFullKeyboardAccessEnabled]` | 10 | AppKit (NSApplication) |
| 21 | `-[NSTitlebarAccessoryViewController setFullScreenMinHeight:]` | 10 | AppKit |
| 22 | `-[NSTitlebarAccessoryViewController setHidden:]` | 10 | AppKit |
| 23 | `-[NSTitlebarAccessoryViewController setLayoutAttribute:]` | 10 | AppKit |
| 24 | `-[NSUserNotificationCenter deliveredNotifications]` | 10 | Foundation |
| 25 | `-[__NSCFDictionary symbolicTraits]` | 10 | Foundation (receiver/type mismatch) |

Frequencies come directly from `sort | uniq -c | sort -rn` on the 10 run logs (sum 250).

## Clusters

| cluster (owner) | selectors | hits | share of 250 |
|------------------|-----------:|-----:|-------------:|
| AppKit: BrowserNativeWidgetWindow (4), NSTitlebarAccessoryViewController (3), NSImage, NSEvent, NSAccessibilityRemoteUIElement, NSKVONotifying_BrowserCrApplication (1 each) | 11 | 110 | 44 % |
| QuartzCore: CALayer (4 incl. `+layer`), CALayerContext (2), NSViewBackingLayer `+layer` inherited (1) | 7 | 70 | 28 % |
| Foundation/CoreFoundationExtras: NSConstantDictionary (2), NSFileManager (1), NSUserNotificationCenter (1), __NSCFDictionary (1) | 5 | 50 | 20 % |
| UniformTypeIdentifiers: UTType | 1 | 10 | 4 % |
| AuthenticationServices: ASWebAuthenticationSessionWebBrowserSessionManager | 1 | 10 | 4 % |
| **total** | **25** | **250** | **100 %** |

Top by size on this table: AppKit (1), QuartzCore (2), Foundation (3). The previous
series' QuartzCore-wiring candidate is #2 here; per dispatch, providers measured for the
top-3 clusters and one hypothesis given (QuartzCore chosen as the falsifiable wiring
candidate after descending the table).

## Top-3 clusters: measured providers

### 1. AppKit — 11 selectors, 110 hits

Overlay image: `$DARLING_OVERLAY/System/Library/Frameworks/AppKit.framework/Versions/C/AppKit`
(3351984 bytes, 2026-11-18 build).

```sh
strings "$DARLING_OVERLAY"/System/Library/Frameworks/AppKit.framework/Versions/C/AppKit \
  | grep -xE 'setTitlebarAppearsTransparent:|setAnimationBehavior:|convertPointFromScreen:|addTitlebarAccessoryViewController:|setFullScreenMinHeight:|setLayoutAttribute:|setHidden:|pressedMouseButtons|deliveredNotifications|isFullKeyboardAccessEnabled|remoteTokenForLocalUIElement:|imageWithSystemSymbolName:accessibilityDescription:'
# -> setHidden:
# -> setAnimationBehavior:
# -> setTitlebarAppearsTransparent:
# (other 8 selectors absent as strings)
```

Source checks (`$DARLING_SRC_DIR`):

```sh
for s in setTitlebarAppearsTransparent setAnimationBehavior convertPointFromScreen \
         addTitlebarAccessoryViewController setFullScreenMinHeight setLayoutAttribute \
         pressedMouseButtons deliveredNotifications remoteTokenForLocalUIElement \
         isFullKeyboardAccessEnabled 'imageWithSystemSymbolName:accessibilityDescription:'; do
  grep -rn "$s" "$DARLING_SRC_DIR"/src/external/cocotron/AppKit
done
# -> no matches for these selectors in cocotron AppKit sources
grep -n '@implementation' "$DARLING_SRC_DIR"/src/external/cocotron/AppKit/NSTitlebarAccessoryViewController.m
# -> 3:@implementation NSTitlebarAccessoryViewController
grep -n 'setHidden\|setFullScreenMinHeight\|setLayoutAttribute' \
  "$DARLING_SRC_DIR"/src/external/cocotron/AppKit/NSTitlebarAccessoryViewController.m
# -> no method implementations found
```

Measurement: 3 selector names appear in the AppKit image strings but the 8 others and
the listed method bodies are absent from the checked cocotron AppKit sources; cluster is
spread across many receiver classes (heterogeneous).

### 2. QuartzCore — 7 selectors, 70 hits

Overlay framework image (stub): `$DARLING_OVERLAY/System/Library/Frameworks/QuartzCore.framework/Versions/A/QuartzCore`

```sh
ls -l "$DARLING_OVERLAY"/System/Library/Frameworks/QuartzCore.framework/Versions/A/QuartzCore
# -rwxrwxr-x ... 15168 ... QuartzCore
md5 "$DARLING_OVERLAY"/System/Library/Frameworks/QuartzCore.framework/Versions/A/QuartzCore
# 637a1da37f4b8225ff6331ce16df587b

nm "$DARLING_OVERLAY"/System/Library/Frameworks/QuartzCore.framework/Versions/A/QuartzCore | grep 'OBJC_CLASS_'
# D _OBJC_CLASS_$_CALayer
# D _OBJC_CLASS_$_CALayerContext
# D _OBJC_CLASS_$_CAOpenGLLayer
# D _OBJC_CLASS_$_CATransaction
# D _OBJC_CLASS_$_CIContext
# D _OBJC_CLASS_$_CIImage
# (plus metaclass/ro symbols and U _OBJC_CLASS_$_NSObject)

strings "$DARLING_OVERLAY"/System/Library/Frameworks/QuartzCore.framework/Versions/A/QuartzCore \
  | grep -xE 'setAnchorPoint:|setGeometryFlipped:|setAutoresizingMask:|setLayer:|setSubwindow:|layer'
# -> (none)
```

Extras dylib: `$DARLING_OVERLAY/usr/lib/QuartzCoreExtras.dylib` (17664 bytes, md5
`3e77886f42f34c4dbaaa7d2d02ca6e11`), same selector-string check returns none.

Dev-stub implementation (`$DARLING_SRC_DIR/src/frameworks/dev-stubs/QuartzCore/src/classes.m`):

```sh
grep -n -A8 'implementation CALayer$' "$DARLING_SRC_DIR"/src/frameworks/dev-stubs/QuartzCore/src/classes.m
# 161:@implementation CALayer
# - methodSignatureForSelector: / forwardInvocation: only (no layer property/animation behavior)

grep -n 'dev-stubs/QuartzCore' "$DARLING_SRC_DIR"/src/frameworks/CMakeLists.txt
# 176: add_subdirectory(dev-stubs/QuartzCore)
```

Cocotron QuartzCore sources (`$DARLING_SRC_DIR/src/external/cocotron/QuartzCore/`, measured):

```sh
grep -n 'setAnchorPoint:' "$DARLING_SRC_DIR"/src/external/cocotron/QuartzCore/CALayer.m
# 85:- (void) setAnchorPoint: (CGPoint) value {

grep -n 'setLayer:\|setSubwindow:' "$DARLING_SRC_DIR"/src/external/cocotron/QuartzCore/CALayerContext.m
# 53:- (void) setLayer: (CALayer *) layer {
# 62:- (void) setSubwindow: (CGSubWindow*) subwindow

grep -rn 'setGeometryFlipped\|setAutoresizingMask\|+ (id)layer\|+ (instancetype)layer\|+layer' \
  "$DARLING_SRC_DIR"/src/external/cocotron/QuartzCore/CALayer.m
# -> no matches (the 3 others absent in cocotron CALayer.m)
```

Build-gui stub path (`$DARLING_SRC_DIR/build-freebsd/build-gui.sh`): lines 393–421 build a
stub `QuartzCore.framework` from `${SRC}/src/freebsd-shims/stub-frameworks/QuartzCore/QuartzCore.m`
into the staged/overlay framework; the overlay image is the 15168-byte stub above.

Measurement summary: CALayer/CALayerContext classes exist in the overlay stub image but
none of the 6 selectors (+ `+layer`) are present as selector strings/impls there;
cocotron QuartzCore implements 3/6 (`setAnchorPoint:`, `setLayer:`, `setSubwindow:`) but
those implementations are not what the overlay framework provides; `+layer`,
`setGeometryFlipped:`, `setAutoresizingMask:` were not found in cocotron `CALayer.m`.
`+[NSViewBackingLayer layer]` is the inherited `CALayer` factory method
(`NSViewBackingLayer : CALayer`, AppKit image contains class symbol but no `+layer` impl).

### 3. Foundation/CoreFoundationExtras — 5 selectors, 50 hits

Overlay images:

```sh
nm "$DARLING_OVERLAY"/usr/lib/CoreFoundationExtras.dylib | grep 'OBJC_CLASS_\$_NSConstantDictionary'
# 0000000000002140 D _OBJC_CLASS_$_NSConstantDictionary
md5 "$DARLING_OVERLAY"/usr/lib/CoreFoundationExtras.dylib
# e2c8676796c83f8ceb9c4196cd00b3e4

strings "$DARLING_OVERLAY"/usr/lib/CoreFoundationExtras.dylib \
  | grep -xE '_encodingCantBeStoredInEightBitCFString|getCharacters:range:|ubiquityIdentityToken|symbolicTraits'
# -> (none)

strings "$DARLING_OVERLAY"/usr/lib/FoundationExtras.dylib \
  | grep -xE '_encodingCantBeStoredInEightBitCFString|getCharacters:range:|ubiquityIdentityToken|symbolicTraits'
# -> (none)

strings "$DARLING_OVERLAY"/System/Library/Frameworks/Foundation.framework/Versions/C/Foundation \
  | grep -xE 'ubiquityIdentityToken|symbolicTraits|_encodingCantBeStoredInEightBitCFString|getCharacters:range:'
# -> getCharacters:range:
# -> _encodingCantBeStoredInEightBitCFString
# (present as selector strings in Foundation image)

nm "$DARLING_OVERLAY"/System/Library/Frameworks/CoreFoundation.framework/Versions/A/CoreFoundation \
  | grep '__NSCFDictionary' | head -2
# 00000000001791a0 t +[__NSCFDictionary automaticallyNotifiesObserversForKey:]
# 0000000000179880 t -[__NSCFDictionary _isDeallocating]
```

Source checks (`$DARLING_SRC_DIR`):

```sh
grep -rn 'ubiquityIdentityToken' "$DARLING_SRC_DIR"/src/external/foundation "$DARLING_SRC_DIR"/src/external/cocotron
# src/external/foundation/include/Foundation/NSFileManager.h: - (id <NSObject, NSCopying, NSCoding>)ubiquityIdentityToken;
# (declaration only; no implementation found in searched trees)

grep -rn 'symbolicTraits' "$DARLING_SRC_DIR"/src/external/cocotron
# AppKit/NSFont.m, AppKit/NSFontDescriptor.m, AppKit/include/AppKit/NSFontDescriptor.h
# (font/descriptor API; no NSDictionary/__NSCFDictionary implementation)
```

Measurement: `NSConstantDictionary` class is provided by CoreFoundationExtras but the two
new string selectors are not in CoreFoundationExtras/FoundationExtras dylibs (Foundation
framework image contains them as selector strings). `ubiquityIdentityToken` is declared
in Foundation headers without found implementation; `symbolicTraits` exists for font
classes, not for dictionary receivers.

## Hypothesis (one, falsifiable)

**Hypothesis (QuartzCore wiring/provider gap, cluster #2):** the uniform `rc=133x10` in
this series is sustained by the overlay QuartzCore being a 15 KB stub that exports
`CALayer`/`CALayerContext` but does not provide the 7 selectors in cluster #2, while the
cocotron QuartzCore sources already implement 3 of them (`setAnchorPoint:`, `setLayer:`,
`setSubwindow:`). If the QuartzCore provider used at runtime supplies concrete
implementations for these 7 selectors (at minimum wiring the existing 3 cocotron
implementations and adding no-op/stub-safe behavior for `+layer`,
`setGeometryFlipped:`, `setAutoresizingMask:` and inherited `+[NSViewBackingLayer layer]`),
then the FWD-MISS table for this exact series shape should lose exactly those 7 selectors.

**Falsifiable prediction:**
- pass: `fwdmiss` becomes `18` (25 - 7) with the 7 QuartzCore-cluster selectors absent
  from `OBJC-FWD-MISS` in a rerun of the same series parameters on an otherwise unchanged
  state; exit/abort behavior may change but the selector-count reduction is the primary
  observable.
- fail: after the QuartzCore-provider change, any of those 7 selectors still appear as
  `OBJC-FWD-MISS` in `chrome-sr-fix24{1..10}.log` (provider not actually wired), or
  `fwdmiss` is not 18 for reasons traceable to this cluster.

**Verification command (same series barrier):**

```sh
sh "$DARLING_SRC_DIR"/build-freebsd/chrome-series.sh fix24 10 120
# then:
grep -ah 'OBJC-FWD-MISS' "$DARLING_BUILD_DIR"/chrome-sr-fix24{1..10}.log | sort -u | wc -l
grep -ah 'OBJC-FWD-MISS' "$DARLING_BUILD_DIR"/chrome-sr-fix24{1..10}.log | sort -u \
  | grep -E 'CALayer|CALayerContext|NSViewBackingLayer layer' || echo "QUARTZCORE_CLUSTER_CLEAR"
grep -o 'fwdmiss=[0-9]*' "$DARLING_BUILD_DIR"/../series-summaries/fix24.txt | tail -1
```

Expected on confirmation: unique count 18, `QUARTZCORE_CLUSTER_CLEAR`, and summary
`fwdmiss=18`.

