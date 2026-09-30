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
| `Wayland` sha256 | `b918e22e77875289ac6a730fba27d39cddc3e36a6234916de2d2c363897e73da` |
| `Info.plist` | 768 bytes, `NSPrincipalClass` = `WaylandDisplay`, `NSPriority` = 300 |
| Build date | 2026-08-28, from the file mtimes (20:16 for the dylib, 17:14 for the plist). There is no `LC_BUILD_VERSION` load command to read a real timestamp from — that load command did not exist in the toolchain that built it. |

Verify the copy is intact at any time:

    shasum -a 256 tests/vendor/wayland-backend/Wayland
    # b918e22e77875289ac6a730fba27d39cddc3e36a6234916de2d2c363897e73da

## The shm name fix, 2026-09-30 — one byte, blessed

**The hash above changed, deliberately, and this is the whole record of it.**

| | |
|---|---|
| Old sha256 | `a4797cddc449477fe65317548e58d6c18f050e838804596d470b4f9ae69eff2c` |
| New sha256 | `b918e22e77875289ac6a730fba27d39cddc3e36a6234916de2d2c363897e73da` |
| Offset | `0x8b3c` |
| Change | one byte, `.` (0x2e) → `/` (0x2f) |
| String | `./.bsdos-wlshm-%d-%d` → `/.bsdos-wlshm-%d-%d` |
| Size | 83824 bytes, unchanged — same-length edit, nothing moved |

**Why.** `shm_open` is specified to take a name of one leading slash followed
by non-slash characters. `./.bsdos-wlshm-0-1` has two slashes once the
implementation prepends the root, so it is rejected — and the guest rejected it:

    WaylandWindow: shm allocation failed for 640x480 buffer: Invalid argument

Proved on the host with no emulation in the picture at all, in four lines of C
on native FreeBSD with native libc:

    shm_open("./.bsdos-wlshm-0-1") = -1  errno=22 (Invalid argument)
    shm_open("/.bsdos-wlshm-0-1")  =  3  errno=0

So this was never an emulation wall: the window could not have reached a frame
on a native machine either, and every hour spent looking for it in the
emulation layer was spent in the wrong place. The second template in the next
section, `/bsdos-waylandwindow-%d-%d`, was already correct and is untouched.

**Who decided.** Not the worker who found it, deliberately — the worker located
it, measured it, gave the offset and both hashes, and left the decision alone.
The head blessed the byte on 2026-09-30. It is recorded here rather than only in
a commit message because the artifact cannot be rebuilt: **if these sources are
ever recovered, the fix belongs in `WaylandWindow.m` and this edit should be
dropped in favour of a real rebuild.** A one-byte patch to an artifact whose
provenance is lost is a debt, and the way to keep it from becoming a surprise is
to write down what it was and why.

**Every pin that names the old hash was updated in the same commit**, or the
next run would have failed on the gate instead of on the thing being measured:
`build-freebsd/run-wayland-window-probe.sh` (`EXPECT_SHA`, the hard gate that
compares overlay, vendored copy and this file) and the two places above. The
guest reads the overlay's copy, so the overlay's copy must be updated too, and
`build-freebsd/fill-bundle-contents.sh` will refuse to run if the two disagree.

## On the `bsdos-*` strings — cleared for commit, deliberately

A strings scan of the dylib turns up exactly two occurrences of a `bsdos-*`
token, and they are the reason this file nearly went uncommitted:

    /.bsdos-wlshm-%d-%d
    /bsdos-waylandwindow-%d-%d

(the first was `./.bsdos-wlshm-%d-%d` until the 2026-09-30 fix above)

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
