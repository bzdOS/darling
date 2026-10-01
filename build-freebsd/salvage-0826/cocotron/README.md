# cocotron — recovered commit

Patch: 0001-AppKit-add-a-Wayland-backend.patch

- Base: 085c77f407a8b8ba62fd1c5cac61d48ca76f44e7 (upstream: darlinghq/darling-cocotron)
- Recovered commit: d191144b805a025398c6b2bf198ce10f9ccf9728
- Provenance: the commit was lost in a repository transfer. It was rebuilt
  by replaying the recorded file edits (edits.json) in timestamp order on
  top of the base and using the verbatim commit message (commits.md), both
  from a preserved session log kept outside this repository.
- Composition verified against the commit message: WaylandDisplay.{h,m},
  WaylandWindow.{h,m}, WaylandInput.{h,m}, WaylandKeyCodes.h, Info.plist
  under AppKit/Wayland.backend/, plus AppKit/CMakeLists.txt.
- Edits applied: 44/44. Three sub-agents had written the files; where
  versions converged, the last edit before the original commit time won,
  which is what timestamp-ordered replay produces.

## Build result (build-freebsd/build-wayland-backend.sh)

Not built — the expected outcome: Wayland.backend was never compiled in the
original commit either. All script preflights passed (clang, ld64.lld,
wayland-scanner, wayland-client and xkbcommon via pkg-config, xdg-shell.xml,
the GUI frameworks in the overlay, HIToolbox/Events.h found under
tests/vendor/fakesdk), and wayland-scanner generated
xdg-shell-client-protocol.{h,c}. The first compile then failed:

    In file included from .../AppKit/include/AppKit/NSGraphics.h:21:
    fatal error: 'ApplicationServices/ApplicationServices.h' file not found

No ApplicationServices header exists anywhere in the tree's SDK material
(flat SDK, fakesdk, missing-headers) — the same class of pre-existing SDK
gap as the HIToolbox/OpenGL absence the script header documents. The
script also expects wayland_shim.c, wayland_ifaces.c and wayland_tramp.s
inside Wayland.backend; those files belong to a later state of the
backend and are not part of this recovered commit.
