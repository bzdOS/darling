# missing-headers

Replacement headers for `#include <IOKit/...>` paths that resolve to
**broken symlinks** in this repo, discovered while writing
`src/freebsd-shims/iokit_shim.c` (see
`docs/SPEC-iokit-coregraphics-build.md` for the shim itself).

## What's broken and why

`src/external/IOKitUser/darling/include/IOKit/graphics/IOGraphicsLib.h`
(the file `IOKit/graphics/IOGraphicsLib.h` actually resolves to, via
`../../../../graphics.subproj/IOGraphicsLib.h` -- that hop is fine, it's a
real file) unconditionally does, at its own lines 31-32:

```c
#include <IOKit/graphics/IOFramebufferShared.h>
#include <IOKit/graphics/IOGraphicsInterface.h>
```

Both of those headers, and two more that CoreGraphics/CoreGraphicsPrivate.h
pull in directly, are themselves symlinks into git submodules that are
declared in `src/external/IOKitUser/.gitmodules` but were never
initialized -- `git submodule status` for both shows the `-` prefix that
means "not checked out":

```
-905186151d713259296f3ae9458195a7097ea323 darling/submodules/IOGraphics
-189e98e32092d5f5a2c365cc85fd36ac7da2d371 darling/submodules/IOHIDFamily
```

Concretely:

| `#include <...>` path | symlink target | resolves? |
|---|---|---|
| `IOKit/graphics/IOFramebufferShared.h` | `darling/submodules/IOGraphics/IOGraphicsFamily/IOKit/graphics/IOFramebufferShared.h` | **no** -- `IOGraphics/` submodule dir is empty |
| `IOKit/graphics/IOGraphicsInterface.h` | same submodule, different file | **no** |
| `IOKit/graphics/IOGraphicsTypes.h` | same submodule, different file | **no** |
| `IOKit/hidsystem/IOLLEvent.h` | `darling/submodules/IOHIDFamily/IOHIDSystem/IOKit/hidsystem/IOLLEvent.h` | **no** -- `IOHIDFamily/` submodule dir is empty |

Verified with `readlink -f` on each path (empty output = dangling) and by
confirming `darling/submodules/IOGraphics/` and `.../IOHIDFamily/` contain
nothing but `.`/`..`. Also checked `tests/vendor/macosx-sdk-flat.tar.gz`
(the pre-flattened SDK headers used by every `build-freebsd/build-*.sh`
script) for all four names -- none are in that archive either, and its own
copy of `IOGraphicsLib.h` is byte-for-byte identical to the darling one
(same two dangling `#include`s), meaning the flat SDK snapshot was very
likely produced by walking this same tree's symlinks and simply couldn't
resolve these either.

**This directly contradicts** the earlier read in
`docs/SPEC-iokit-coregraphics-build.md` §5-6, which asserted these headers
"существуют, компилируются как обычные C-заголовки" -- that was true for
`IOKitLib.h`/`IOGraphicsLib.h` themselves and the `xnu`-submodule-backed
headers (`IOTypes.h`, `IOReturn.h`, `IOKitKeys.h`,
`OSMessageNotification.h` -- all confirmed resolving, since `xnu` *is* a
fully checked-out submodule), but is **false** for these four
IOGraphics/IOHIDFamily-backed ones. Not caught by the SPEC's own grep pass
because a grep for identifiers doesn't notice that the file a `#include`
names is an unreadable dangling symlink -- only trying to actually open it
does.

## What's in this directory

Four minimal replacement headers, laid out to mirror the same
`IOKit/graphics/...` / `IOKit/hidsystem/...` include paths so that adding
this directory to `-I` *before* `src/external/IOKitUser/darling/include`
(see `build-freebsd/build-iokit-shim.sh`) makes clang resolve the
`#include <IOKit/graphics/IOGraphicsTypes.h>`-style paths here instead of
hitting the dangling symlinks. (In practice the order likely doesn't even
matter -- a broken symlink makes `open()` fail with ENOENT, and clang's
`-I` search keeps trying subsequent directories on ENOENT -- but listing
this directory first makes the intent explicit and avoids relying on that
fallback behavior.)

Each header provides **only** the identifiers actually referenced by the
CoreGraphics translation units this shim targets (verified by grep, see
each file's own comment) -- not a reconstruction of the real Apple/IOKit
family headers. Two of the four (`IOFramebufferShared.h`,
`IOGraphicsInterface.h`) are **empty** on purpose: nothing in
`IOGraphicsLib.h`'s own body or in the CoreGraphics sources this shim
covers actually uses any identifier from either -- they are pulled in only
because `IOGraphicsLib.h` `#include`s them unconditionally, so an empty
file with the right name is enough to let that `#include` succeed.

## Risk / not verified

- `IOGraphicsTypes.h`'s five values (`kDisplayVendorID`,
  `kDisplayProductID`, `kDisplaySerialNumber`,
  `kDisplayVendorIDUnknown`, `kDisplayProductIDGeneric`) are reconstructed
  from general knowledge of the public macOS `IOGraphicsTypes.h` API, **not
  copied from any file in this repo** (none exists here to copy from) and
  not verified against a real SDK header. If wrong, the practical impact is
  low: `CGDisplayIOServicePort()` (the only caller) never reaches the code
  that uses them, because `iokit_shim.c`'s `IOServiceGetMatchingServices()`
  always fails first (see that file's own comment) -- so this only matters
  for anything that reads these constants directly outside that dead path,
  which is unaudited (AppKit is out of scope, per the SPEC).
- `IOLLEvent.h`'s `NXEventData` is stubbed as an opaque 40-byte blob,
  matching the only concrete size constraint found in-tree
  (`CoreGraphicsPrivate.h:110`'s comment "type-dependent data: 40 bytes").
  The real type is a union of several event-specific structs; nothing found
  in CoreGraphics reads/writes its fields (only grepped CoreGraphics/ and
  AppKit/, not the full tree), so field-level layout was not reconstructed.
  If any code elsewhere in Cocotron populates specific `NXEventData`
  fields, this stub will not match and will need real field definitions.
- Not checked: whether `IOGraphicsLibPrivate.h` or `IOAccelSurfaceControl.h`
  (siblings of `IOGraphicsLib.h` under the same `graphics/` dir) are
  `#include`d by anything this shim's build reaches -- only `IOGraphicsLib.h`
  itself, `IOFramebufferShared.h`, and `IOGraphicsInterface.h` were checked.
- Nothing here has been compiled. See
  `build-freebsd/build-iokit-shim.sh`'s own header comment.
