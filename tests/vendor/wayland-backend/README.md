# tests/vendor/wayland-backend/

The built Wayland.backend NSDisplay bundle, kept in-tree because **its
sources no longer exist anywhere**.

## Why this is a committed binary

`src/external/cocotron/AppKit/Wayland.backend/` — the three `.m` files
(`WaylandDisplay.m`, `WaylandWindow.m`, `WaylandInput.m`) plus the shim
(`wayland_shim.c`, `wayland_ifaces.c`, `wayland_tramp.s`) that
`build-freebsd/build-wayland-backend.sh` compiles — were never committed and
did not survive a copy of the build machine. Checked and confirmed absent
from: the working tree, every branch of this repository, every branch of the
`darlinghq/darling-cocotron` submodule, the dangling-object set
(`git fsck --dangling`), and the machine's filesystem.

The build script's own header still says "THIS SCRIPT HAS NEVER BEEN RUN" on
this branch, and it cannot be: there is nothing to compile. So the binary is
the artifact, and this is the only copy of it. Losing it would end the
Wayland work outright, which is why it is committed rather than left in a
scratch dir.

## Provenance

| | |
|---|---|
| Built from | the lost sources above; rebuildable only if they are recovered |
| Found at | the overlay's guest path, i.e. `<overlay>/System/Library/Frameworks/AppKit.framework/Versions/C/Resources/Backends/Wayland.backend/Contents/{MacOS/Wayland,Info.plist}` |
| `Wayland` | Mach-O 64-bit x86_64 dynamically linked shared library, 83824 bytes |
| `Wayland` sha256 | `a4797cddc449477fe65317548e58d6c18f050e838804596d470b4f9ae69eff2c` |
| `Info.plist` | 768 bytes, `NSPrincipalClass` = `WaylandDisplay`, `NSPriority` = 300 |
| Build date | 2026-08-28, from the file mtimes (20:16 for the dylib, 17:14 for the plist). There is no `LC_BUILD_VERSION` load command to read a real timestamp from — that load command did not exist in the toolchain that built it. |

Verify the copy is intact at any time:

    shasum -a 256 tests/vendor/wayland-backend/Wayland
    # a4797cddc449477fe65317548e58d6c18f050e838804596d470b4f9ae69eff2c

## On the `bsdos-*` strings — cleared for commit, deliberately

A strings scan of the dylib turns up exactly two occurrences of a `bsdos-*`
token, and they are the reason this file nearly went uncommitted:

    ./.bsdos-wlshm-%d-%d
    /bsdos-waylandwindow-%d-%d

They are `printf` templates for the temporary files the shim creates: the
first backs the `wl_shm` pool, the second names a window. They are **product
identifiers of the OS this repository builds** (bzdOS), not infrastructure
names. The AGENTS.md great-list keys on the *build machine's* name, and does
not match a bare `bsdos`; the strings are what make the product identifiable
at runtime, not who built it.

Recorded here so the presence is never a surprise to a later reader or to the
pre-push greps, and so nobody re-litigates it: **strings cleared for commit by
the head — `bsdos-*` templates are product identifiers.** Every other
absolute path in the binary is a guest path (`/System/Library/...`,
`/usr/lib/...`); the full sweep is in the commit that added this directory.

## How it is used

`tests/src/wayland-window-create.m` loads it through `NSBundle` (by its guest
path) and drives `WaylandDisplay` → `WaylandWindow` → a `wl_shm` buffer.
Because the sources are gone, the test declares the private methods itself,
so their signatures are checked against this dylib before anything is
launched:

    python3 build-freebsd/check-wayland-window-probe.py \
      tests/vendor/wayland-backend/Wayland

That check is not ceremony: it caught
`-_acquireBackBufferForWidth:height:` returning a 48-byte struct by value
(`^{?=^{wl_buffer}^vQiiiic}24@0:8i16i20`) where the test had assumed an `id`.
Re-verify it after any change to either file.
