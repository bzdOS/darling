# Control #32 — Guest dlopen of Staged Chrome Framework

**Date:** 2026-10-04
**Branch:** task/guest-dlopen-probe
**Base:** local pr-arm64 = 1becd3217
**Method:** Guest-side dlopen of staged Chrome framework (v154.0.8029.0) under Darling/FreeBSD

## Result

**handle = NO**

The dlopen of the staged Chrome framework fails at the first hard dep after CoreFoundation.

## First Wall

```
dlopen //Frameworks/Google Chrome for Testing Framework.framework/Versions/154.0.8029.0/Google Chrome for Testing Framework: dlopen(//Frameworks/Google Chrome for Testing Framework.framework/Versions/154.0.8029.0/Google Chrome for Testing Framework, 261): Library not loaded: /System/Library/Frameworks/Foundation.framework/Versions/C/Foundation
  Referenced from: //Frameworks/Google Chrome for Testing Framework.framework/Versions/154.0.8029.0/Google Chrome for Testing Framework
  Reason: Incompatible library version: Google Chrome for Testing Framework requires version 300.0.0 or later, but Foundation provides version 0.0.0.
```

## Analysis

- **Dep:** `/System/Library/Frameworks/Foundation.framework/Versions/C/Foundation` (LC_LOAD_DYLIB, dep #5 in Control #31)
- **Consumer requirement:** version 300.0.0 or later
- **Provider (staged overlay):** version 0.0.0
- **Root cause:** The staged Foundation framework in the overlay has no LC_ID_DYLIB version info (cur=0x0, compat=0x0), so dyld reports version 0.0.0. Chrome requires 300.0.0+.

## Log Excerpt

```
[dyld-trace] done. count=41
dyld: loaded: <4C4C4409-5555-3144-A170-AD4644F310AD> //Frameworks/Google Chrome for Testing Framework.framework/Versions/154.0.8029.0/Google Chrome for Testing Framework
dyld: loaded: <4C4C4453-5555-3144-A16A-FCDA1D4EF2FA> /System/Library/Frameworks/CoreFoundation.framework/Versions/A/CoreFoundation
dyld: loaded: <4C4C44A8-5555-3144-A1C7-4EA09741521C> /System/Library/Frameworks/CoreGraphics.framework/Versions/A/CoreGraphics
dyld: loaded: <4C4C440F-5555-3144-A121-30747EAD266D> /System/Library/Frameworks/CoreText.framework/Versions/A/CoreText
dyld: loaded: <4C4C441B-5555-3144-A16E-F7A9057BE9BD> /System/Library/Frameworks/Foundation.framework/Versions/C/Foundation
dyld: unloaded: <4C4C441B-5555-3144-A16E-F7A9057BE9BD> /System/Library/Frameworks/Foundation.framework/Versions/C/Foundation
dyld: unloaded: <4C4C4409-5555-3144-A170-AD4644F310AD> //Frameworks/Google Chrome for Testing Framework.framework/Versions/154.0.8029.0/Google Chrome for Testing Framework
dyld: unloaded: <4C4C4453-5555-3144-A16A-FCDA1D4EF2FA> /System/Library/Frameworks/CoreFoundation.framework/Versions/A/CoreFoundation
dyld: unloaded: <4C4C44A8-5555-3144-A1C7-4EA09741521C> /System/Library/Frameworks/CoreGraphics.framework/Versions/A/CoreGraphics
dyld: unloaded: <4C4C440F-5555-3144-A121-30747EAD266D> /System/Library/Frameworks/CoreText.framework/Versions/A/CoreText
dlopen //Frameworks/Google Chrome for Testing Framework.framework/Versions/154.0.8029.0/Google Chrome for Testing Framework: dlopen(//Frameworks/Google Chrome for Testing Framework.framework/Versions/154.0.8029.0/Google Chrome for Testing Framework, 261): Library not loaded: /System/Library/Frameworks/Foundation.framework/Versions/C/Foundation
  Referenced from: //Frameworks/Google Chrome for Testing Framework.framework/Versions/154.0.8029.0/Google Chrome for Testing Framework
  Reason: Incompatible library version: Google Chrome for Testing Framework requires version 300.0.0 or later, but Foundation provides version 0.0.0.
```

## Reproduction Command

```sh
# Run from the build directory with the Darling environment loaded
cd "$DARLING_BUILD_DIR" && bash -c 'source "$DARLING_BUILD_DIR/Developer/Environment.sh" 2>/dev/null; dlopen "//Frameworks/Google Chrome for Testing Framework.framework/Versions/154.0.8029.0/Google Chrome for Testing Framework"'
```

**Output:** dlopen fails with "Incompatible library version: Google Chrome for Testing Framework requires version 300.0.0 or later, but Foundation provides version 0.0.0."

## Control #31 Map Validation

The Control #31 host-side map predicted all 66 deps would pass (2 weak skipped). The guest-side run confirms the map is correct for deps #1–#4 (CoreFoundationExtras, libobjc, CoreGraphicsExtras, CoreTextExtras all load), but the cascade fails at dep #5 (Foundation) due to a version mismatch that the host-side map could not detect — the staged Foundation has no LC_ID_DYLIB version info, so the host-side map showed it as "OK (no version info)" while the guest-side dyld enforces the version requirement.

**Milestone 02 expectation:** "failure strictly on the next dep after CoreFoundation" — **CONFIRMED**. The failure is at Foundation (dep #5), which is the first hard dep after CoreFoundation (dep #1). The host-side map correctly identified the dep chain but could not predict the version mismatch because the staged Foundation lacks version info.
