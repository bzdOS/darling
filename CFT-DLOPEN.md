# Control #31 — Chrome Framework Dependency Cascade Map

**Date:** 2026-10-04
**Branch:** task/dep-cascade-map
**Base:** origin/pr-arm64 = 1becd3217
**Method:** Host-side Mach-O load command parsing (no guest run, no staging)

## Summary

- **Total LC_LOAD_DYLIB / LC_LOAD_WEAK_DYLIB:** 66
- **Resolved in staged overlay:** 64
- **Missing from overlay:** 2
- **First unresolved dep after CoreFoundation:** `/System/Library/Frameworks/ScreenCaptureKit.framework/Versions/A/ScreenCaptureKit` (LC_LOAD_WEAK_DYLIB, dep #22)

## Dependency Table

| # | Load Command | Dep Path | Provider in Overlay | Provider LC_ID cur/compat | Consumer compat | Status |
|---|---|---|---|---|---|---|
| 1 | LC_LOAD_DYLIB | `/usr/lib/CoreFoundationExtras.dylib` | `usr/lib/CoreFoundationExtras.dylib` | cur=0xffffffff compat=0xffffffff | compat=0x0 | OK |
| 2 | LC_LOAD_DYLIB | `/usr/lib/libobjc.A.dylib` | `usr/lib/libobjc.A.dylib` | cur=0x0 compat=0x0 | compat=0x0 | OK (no version info) |
| 3 | LC_LOAD_DYLIB | `/usr/lib/CoreGraphicsExtras.dylib` | `usr/lib/CoreGraphicsExtras.dylib` | cur=0xffffffff compat=0xffffffff | compat=0x0 | OK |
| 4 | LC_LOAD_DYLIB | `/usr/lib/CoreTextExtras.dylib` | `usr/lib/CoreTextExtras.dylib` | cur=0xffffffff compat=0xffffffff | compat=0x0 | OK |
| 5 | LC_LOAD_DYLIB | `/usr/lib/FoundationExtras.dylib` | `usr/lib/FoundationExtras.dylib` | cur=0xffffffff compat=0xffffffff | compat=0x0 | OK |
| 6 | LC_LOAD_DYLIB | `/System/Library/Frameworks/Security.framework/Versions/A/Security` | `System/Library/Frameworks/Security.framework/Versions/A/Security` | cur=0xffffffff compat=0xffffffff | compat=0x0 | OK |
| 7 | LC_LOAD_DYLIB | `/System/Library/Frameworks/ApplicationServices.framework/Versions/A/ApplicationServices` | `System/Library/Frameworks/ApplicationServices.framework/Versions/A/ApplicationServices` | cur=0xffffffff compat=0xffffffff | compat=0x0 | OK |
| 8 | LC_LOAD_DYLIB | `/System/Library/Frameworks/CoreServices.framework/Versions/A/CoreServices` | `System/Library/Frameworks/CoreServices.framework/Versions/A/CoreServices` | cur=0xffffffff compat=0xffffffff | compat=0x0 | OK |
| 9 | LC_LOAD_DYLIB | `/System/Library/Frameworks/CFNetwork.framework/Versions/A/CFNetwork` | `System/Library/Frameworks/CFNetwork.framework/Versions/A/CFNetwork` | cur=0xffffffff compat=0xffffffff | compat=0x0 | OK |
| 10 | LC_LOAD_DYLIB | `/usr/lib/AppKitExtras.dylib` | `usr/lib/AppKitExtras.dylib` | cur=0xffffffff compat=0xffffffff | compat=0x0 | OK |
| 11 | LC_LOAD_DYLIB | `/usr/lib/IOKitExtras.dylib` | `usr/lib/IOKitExtras.dylib` | cur=0xffffffff compat=0xffffffff | compat=0x0 | OK |
| 12 | LC_LOAD_DYLIB | `/System/Library/Frameworks/OpenDirectory.framework/Versions/A/OpenDirectory` | `System/Library/Frameworks/OpenDirectory.framework/Versions/A/OpenDirectory` | cur=0xffffffff compat=0xffffffff | compat=0x0 | OK |
| 13 | LC_LOAD_DYLIB | `/System/Library/Frameworks/CryptoTokenKit.framework/Versions/A/CryptoTokenKit` | `System/Library/Frameworks/CryptoTokenKit.framework/Versions/A/CryptoTokenKit` | cur=0xffffffff compat=0xffffffff | compat=0x0 | OK |
| 14 | LC_LOAD_DYLIB | `/System/Library/Frameworks/LocalAuthentication.framework/Versions/A/LocalAuthentication` | `System/Library/Frameworks/LocalAuthentication.framework/Versions/A/LocalAuthentication` | cur=0xffffffff compat=0xffffffff | compat=0x0 | OK |
| 15 | LC_LOAD_DYLIB | `/System/Library/Frameworks/Accelerate.framework/Versions/A/Accelerate` | `System/Library/Frameworks/Accelerate.framework/Versions/A/Accelerate` | cur=0xffffffff compat=0xffffffff | compat=0x0 | OK |
| 16 | LC_LOAD_DYLIB | `/System/Library/Frameworks/AudioUnit.framework/Versions/A/AudioUnit` | `System/Library/Frameworks/AudioUnit.framework/Versions/A/AudioUnit` | cur=0xffffffff compat=0xffffffff | compat=0x0 | OK |
| 17 | LC_LOAD_DYLIB | `/System/Library/Frameworks/AVFAudio.framework/Versions/A/AVFAudio` | `System/Library/Frameworks/AVFAudio.framework/Versions/A/AVFAudio` | cur=0xffffffff compat=0xffffffff | compat=0x0 | OK |
| 18 | LC_LOAD_DYLIB | `/System/Library/Frameworks/Carbon.framework/Versions/A/Carbon` | `System/Library/Frameworks/Carbon.framework/Versions/A/Carbon` | cur=0xffffffff compat=0xffffffff | compat=0x0 | OK |
| 19 | LC_LOAD_DYLIB | `/System/Library/Frameworks/CoreVideo.framework/Versions/A/CoreVideo` | `System/Library/Frameworks/CoreVideo.framework/Versions/A/CoreVideo` | cur=0xffffffff compat=0xffffffff | compat=0x0 | OK |
| 20 | LC_LOAD_DYLIB | `/usr/lib/QuartzCoreExtras.dylib` | `usr/lib/QuartzCoreExtras.dylib` | cur=0xffffffff compat=0xffffffff | compat=0x0 | OK |
| 21 | LC_LOAD_DYLIB | `/System/Library/Frameworks/CoreImage.framework/Versions/A/CoreImage` | `System/Library/Frameworks/CoreImage.framework/Versions/A/CoreImage` | cur=0xffffffff compat=0xffffffff | compat=0x0 | OK |
| 22 | LC_LOAD_WEAK_DYLIB | `/System/Library/Frameworks/ScreenCaptureKit.framework/Versions/A/ScreenCaptureKit` | — | — | compat=0x0 | **MISSING** |
| 23 | LC_LOAD_DYLIB | `/usr/lib/UniformTypeIdentifiersExtras.dylib` | `usr/lib/UniformTypeIdentifiersExtras.dylib` | cur=0xffffffff compat=0xffffffff | compat=0x0 | OK |
| 24 | LC_LOAD_DYLIB | `/System/Library/Frameworks/Network.framework/Versions/A/Network` | `System/Library/Frameworks/Network.framework/Versions/A/Network` | cur=0xffffffff compat=0xffffffff | compat=0x0 | OK |
| 25 | LC_LOAD_DYLIB | `/usr/lib/SystemConfigurationExtras.dylib` | `usr/lib/SystemConfigurationExtras.dylib` | cur=0xffffffff compat=0xffffffff | compat=0x0 | OK |
| 26 | LC_LOAD_DYLIB | `/System/Library/Frameworks/IOSurface.framework/Versions/A/IOSurface` | `System/Library/Frameworks/IOSurface.framework/Versions/A/IOSurface` | cur=0xffffffff compat=0xffffffff | compat=0x0 | OK |
| 27 | LC_LOAD_DYLIB | `/System/Library/Frameworks/CoreMedia.framework/Versions/A/CoreMedia` | `System/Library/Frameworks/CoreMedia.framework/Versions/A/CoreMedia` | cur=0xffffffff compat=0xffffffff | compat=0x0 | OK |
| 28 | LC_LOAD_DYLIB | `/usr/lib/MetalExtras.dylib` | `usr/lib/MetalExtras.dylib` | cur=0xffffffff compat=0xffffffff | compat=0x0 | OK |
| 29 | LC_LOAD_WEAK_DYLIB | `/System/Library/PrivateFrameworks/SkyLight.framework/Versions/A/SkyLight` | — | — | compat=0x0 | **MISSING** |
| 30 | LC_LOAD_DYLIB | `/System/Library/Frameworks/AudioToolbox.framework/Versions/A/AudioToolbox` | `System/Library/Frameworks/AudioToolbox.framework/Versions/A/AudioToolbox` | cur=0xffffffff compat=0xffffffff | compat=0x0 | OK |
| 31 | LC_LOAD_DYLIB | `/usr/lib/CoreAudioExtras.dylib` | `usr/lib/CoreAudioExtras.dylib` | cur=0xffffffff compat=0xffffffff | compat=0x0 | OK |
| 32 | LC_LOAD_DYLIB | `/System/Library/Frameworks/OpenGL.framework/Versions/A/OpenGL` | `System/Library/Frameworks/OpenGL.framework/Versions/A/OpenGL` | cur=0xffffffff compat=0xffffffff | compat=0x0 | OK |
| 33 | LC_LOAD_DYLIB | `/System/Library/Frameworks/Quartz.framework/Versions/A/Quartz` | `System/Library/Frameworks/Quartz.framework/Versions/A/Quartz` | cur=0xffffffff compat=0xffffffff | compat=0x0 | OK |
| 34 | LC_LOAD_DYLIB | `/System/Library/Frameworks/Cocoa.framework/Versions/A/Cocoa` | `System/Library/Frameworks/Cocoa.framework/Versions/A/Cocoa` | cur=0xffffffff compat=0xffffffff | compat=0x0 | OK |
| 35 | LC_LOAD_DYLIB | `/usr/lib/AVFoundationExtras.dylib` | `usr/lib/AVFoundationExtras.dylib` | cur=0xffffffff compat=0xffffffff | compat=0x0 | OK |
| 36 | LC_LOAD_DYLIB | `/System/Library/Frameworks/VideoToolbox.framework/Versions/A/VideoToolbox` | `System/Library/Frameworks/VideoToolbox.framework/Versions/A/VideoToolbox` | cur=0xffffffff compat=0xffffffff | compat=0x0 | OK |
| 37 | LC_LOAD_DYLIB | `/System/Library/Frameworks/CoreMediaIO.framework/Versions/A/CoreMediaIO` | `System/Library/Frameworks/CoreMediaIO.framework/Versions/A/CoreMediaIO` | cur=0xffffffff compat=0xffffffff | compat=0x0 | OK |
| 38 | LC_LOAD_DYLIB | `/System/Library/Frameworks/Accessibility.framework/Versions/A/Accessibility` | `System/Library/Frameworks/Accessibility.framework/Versions/A/Accessibility` | cur=0xffffffff compat=0xffffffff | compat=0x0 | OK |
| 39 | LC_LOAD_DYLIB | `/System/Library/Frameworks/MetalKit.framework/Versions/A/MetalKit` | `System/Library/Frameworks/MetalKit.framework/Versions/A/MetalKit` | cur=0xffffffff compat=0xffffffff | compat=0x0 | OK |
| 40 | LC_LOAD_DYLIB | `/usr/lib/CoreBluetoothExtras.dylib` | `usr/lib/CoreBluetoothExtras.dylib` | cur=0xffffffff compat=0xffffffff | compat=0x0 | OK |
| 41 | LC_LOAD_DYLIB | `/usr/lib/IOBluetoothExtras.dylib` | `usr/lib/IOBluetoothExtras.dylib` | cur=0xffffffff compat=0xffffffff | compat=0x0 | OK |
| 42 | LC_LOAD_DYLIB | `/System/Library/Frameworks/CoreMIDI.framework/Versions/A/CoreMIDI` | `System/Library/Frameworks/CoreMIDI.framework/Versions/A/CoreMIDI` | cur=0xffffffff compat=0xffffffff | compat=0x0 | OK |
| 43 | LC_LOAD_DYLIB | `/System/Library/Frameworks/MediaAccessibility.framework/Versions/A/MediaAccessibility` | `System/Library/Frameworks/MediaAccessibility.framework/Versions/A/MediaAccessibility` | cur=0xffffffff compat=0xffffffff | compat=0x0 | OK |
| 44 | LC_LOAD_DYLIB | `/System/Library/Frameworks/SecurityInterface.framework/Versions/A/SecurityInterface` | `System/Library/Frameworks/SecurityInterface.framework/Versions/A/SecurityInterface` | cur=0xffffffff compat=0xffffffff | compat=0x0 | OK |
| 45 | LC_LOAD_DYLIB | `/usr/lib/MediaPlayerExtras.dylib` | `usr/lib/MediaPlayerExtras.dylib` | cur=0xffffffff compat=0xffffffff | compat=0x0 | OK |
| 46 | LC_LOAD_DYLIB | `/usr/lib/AuthenticationServicesExtras.dylib` | `usr/lib/AuthenticationServicesExtras.dylib` | cur=0xffffffff compat=0xffffffff | compat=0x0 | OK |
| 47 | LC_LOAD_DYLIB | `/System/Library/Frameworks/CoreHaptics.framework/Versions/A/CoreHaptics` | `System/Library/Frameworks/CoreHaptics.framework/Versions/A/CoreHaptics` | cur=0xffffffff compat=0xffffffff | compat=0x0 | OK |
| 48 | LC_LOAD_DYLIB | `/usr/lib/GameControllerExtras.dylib` | `usr/lib/GameControllerExtras.dylib` | cur=0xffffffff compat=0xffffffff | compat=0x0 | OK |
| 49 | LC_LOAD_DYLIB | `/System/Library/Frameworks/ForceFeedback.framework/Versions/A/ForceFeedback` | `System/Library/Frameworks/ForceFeedback.framework/Versions/A/ForceFeedback` | cur=0xffffffff compat=0xffffffff | compat=0x0 | OK |
| 50 | LC_LOAD_DYLIB | `/System/Library/Frameworks/CoreWLAN.framework/Versions/A/CoreWLAN` | `System/Library/Frameworks/CoreWLAN.framework/Versions/A/CoreWLAN` | cur=0xffffffff compat=0xffffffff | compat=0x0 | OK |
| 51 | LC_LOAD_DYLIB | `/System/Library/Frameworks/CoreLocation.framework/Versions/A/CoreLocation` | `System/Library/Frameworks/CoreLocation.framework/Versions/A/CoreLocation` | cur=0xffffffff compat=0xffffffff | compat=0x0 | OK |
| 52 | LC_LOAD_DYLIB | `/usr/lib/VisionExtras.dylib` | `usr/lib/VisionExtras.dylib` | cur=0xffffffff compat=0xffffffff | compat=0x0 | OK |
| 53 | LC_LOAD_DYLIB | `/System/Library/Frameworks/CoreML.framework/Versions/A/CoreML` | `System/Library/Frameworks/CoreML.framework/Versions/A/CoreML` | cur=0xffffffff compat=0xffffffff | compat=0x0 | OK |
| 54 | LC_LOAD_DYLIB | `/System/Library/Frameworks/DiskArbitration.framework/Versions/A/DiskArbitration` | `System/Library/Frameworks/DiskArbitration.framework/Versions/A/DiskArbitration` | cur=0xffffffff compat=0xffffffff | compat=0x0 | OK |
| 55 | LC_LOAD_DYLIB | `/System/Library/Frameworks/ServiceManagement.framework/Versions/A/ServiceManagement` | `System/Library/Frameworks/ServiceManagement.framework/Versions/A/ServiceManagement` | cur=0xffffffff compat=0xffffffff | compat=0x0 | OK |
| 56 | LC_LOAD_DYLIB | `/System/Library/Frameworks/SafariServices.framework/Versions/A/SafariServices` | `System/Library/Frameworks/SafariServices.framework/Versions/A/SafariServices` | cur=0xffffffff compat=0xffffffff | compat=0x0 | OK |
| 57 | LC_LOAD_DYLIB | `/usr/lib/UserNotificationsExtras.dylib` | `usr/lib/UserNotificationsExtras.dylib` | cur=0xffffffff compat=0xffffffff | compat=0x0 | OK |
| 58 | LC_LOAD_DYLIB | `/System/Library/Frameworks/LocalAuthenticationEmbeddedUI.framework/Versions/A/LocalAuthenticationEmbeddedUI` | `System/Library/Frameworks/LocalAuthenticationEmbeddedUI.framework/Versions/A/LocalAuthenticationEmbeddedUI` | cur=0xffffffff compat=0xffffffff | compat=0x0 | OK |
| 59 | LC_LOAD_DYLIB | `/usr/lib/libcups.2.dylib` | `usr/lib/libcups.2.dylib` | cur=0x10000 compat=0x10000 | compat=0x0 | OK |
| 60 | LC_LOAD_DYLIB | `/usr/lib/libbsm.0.dylib` | `usr/lib/libbsm.0.dylib` | cur=0x0 compat=0x0 | compat=0x0 | OK (no version info) |
| 61 | LC_LOAD_DYLIB | `/usr/lib/libpmenergy.dylib` | `usr/lib/libpmenergy.dylib` | cur=0xffffffff compat=0xffffffff | compat=0x0 | OK |
| 62 | LC_LOAD_DYLIB | `/usr/lib/libpmsample.dylib` | `usr/lib/libpmsample.dylib` | cur=0xffffffff compat=0xffffffff | compat=0x0 | OK |
| 63 | LC_LOAD_DYLIB | `/usr/lib/libresolv.9.dylib` | `usr/lib/libresolv.9.dylib` | cur=0x0 compat=0x0 | compat=0x0 | OK (no version info) |
| 64 | LC_LOAD_DYLIB | `/usr/lib/libsandbox.1.dylib` | `usr/lib/libsandbox.1.dylib` | cur=0xffffffff compat=0xffffffff | compat=0x0 | OK |
| 65 | LC_LOAD_DYLIB | `/usr/lib/libbz2.1.0.dylib` | `usr/lib/libbz2.1.0.dylib` | cur=0x0 compat=0x0 | compat=0x0 | OK (no version info) |
| 66 | LC_LOAD_DYLIB | `/usr/lib/LSysX.dylib` | `usr/lib/LSysX.dylib` | cur=0x0 compat=0x0 | compat=0x0 | OK (no version info) |

## Missing Dependencies (Step 2 Analysis)

Both missing deps are `LC_LOAD_WEAK_DYLIB` — the dynamic linker will not fail if they are absent at load time. They are weak references, meaning the framework can load without them.

| # | Dep Path | Load Command | Resolution |
|---|---|---|---|
| 22 | `/System/Library/Frameworks/ScreenCaptureKit.framework/Versions/A/ScreenCaptureKit` | LC_LOAD_WEAK_DYLIB | Weak — load succeeds without it. No action needed for milestone 02. |
| 29 | `/System/Library/PrivateFrameworks/SkyLight.framework/Versions/A/SkyLight` | LC_LOAD_WEAK_DYLIB | Weak — load succeeds without it. No action needed for milestone 02. |

**Step 2 verdict:** Both unresolvable deps are weak loads. Neither blocks the cascade. The first unresolvable dep after CoreFoundation (#22 ScreenCaptureKit) is closed by the weak-load mechanism itself — no staging or wrapper work is needed. The cascade proceeds through all 66 deps without a hard failure.

## First Unresolved Dep After CoreFoundation

**Dep #22: `/System/Library/Frameworks/ScreenCaptureKit.framework/Versions/A/ScreenCaptureKit`** (LC_LOAD_WEAK_DYLIB)

This is the first dep after CoreFoundation that has no provider in the staged overlay. However, because it is a weak load, the framework will still load successfully — the dep is simply skipped. This means the cascade does NOT fail at dep #22; it continues through all remaining deps.

**Milestone 02 expectation:** "failure strictly on the next dep after CoreFoundation." Since both missing deps are weak, the cascade does NOT fail at the first missing dep. The expectation is NOT met — the cascade proceeds past all 66 deps without a hard failure.

## Reproduction Command

```sh
python3 << 'PYEOF'
import struct, os

def parse_macho_deps(path):
    deps = []
    with open(path, 'rb') as f:
        magic = struct.unpack('<I', f.read(4))[0]
        endian = '<'
        cputype, cpusubtype, filetype, ncmds, sizeofcmds, flags, reserved = struct.unpack(endian + 'IIIIIII', f.read(28))
        for i in range(ncmds):
            pos = f.tell()
            cmd, cmdsize = struct.unpack(endian + 'II', f.read(8))
            if cmd == 0xC or cmd == (0x18 | 0x80000000):
                name_offset = struct.unpack(endian + 'I', f.read(4))[0]
                timestamp = struct.unpack(endian + 'I', f.read(4))[0]
                current_version = struct.unpack(endian + 'I', f.read(4))[0]
                compat_version = struct.unpack(endian + 'I', f.read(4))[0]
                name_start = pos + name_offset
                f.seek(name_start)
                name = b''
                while True:
                    ch = f.read(1)
                    if ch == b'\x00':
                        break
                    name += ch
                cmd_name = 'LC_LOAD_DYLIB' if cmd == 0xC else 'LC_LOAD_WEAK_DYLIB'
                deps.append({'cmd': cmd_name, 'name': name.decode('utf-8', errors='replace'), 'current_version': current_version, 'compat_version': compat_version})
            f.seek(pos + cmdsize)
    return deps

chrome_fw = os.environ.get('CHROME_FW_PATH', 'Google Chrome for Testing Framework')
deps = parse_macho_deps(chrome_fw)
for i, dep in enumerate(deps):
    print(f"{i+1}: {dep['cmd']}: {dep['name']}")
PYEOF
```

**Output:** 66 deps listed in load command order (see table above).
