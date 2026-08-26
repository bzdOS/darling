# SPEC: build order for CoreText, CoreData, QuartzCore, ImageIO, CoreServices, OpenGL

Status: **research only, nothing built or run.** No FreeBSD box was touched.
This document reads `CMakeLists.txt` files and the existing (never-run)
`build-freebsd/build-gui.sh` / `docs/SPEC-gui-build.md` to plan the order and
mechanism for staging these six frameworks into the overlay so that
`build-gui.sh`'s AppKit stage (`build-freebsd/build-gui.sh:292-314`) stops
FATALing at `require_frameworks AppKit CoreText CoreData QuartzCore ImageIO
CoreServices` (`build-freebsd/build-gui.sh:293`).

Out of scope (other agents): **IOKit, CoreGraphics** (CoreGraphics/CMakeLists.txt
dependency chain), **Onyx2D** and the `src/native/` ELF-wrapper scheme.

## 0. The mechanism these frameworks must fit into

This port does **not** build via CMake's `COMPONENT_gui` tree — that tree has
never been exercised (`docs/SPEC-gui-build.md:1-19`). The only working model
is `build-freebsd/build-gui.sh`'s pattern, copied from
`build-real-macho-tests.sh`: raw `clang -target x86_64-apple-macos10.10` +
`ld64.lld -syslibroot <staged-overlay>`, one component at a time — extract a
CMake `set(<Name>_sources ...)` list with a python regex
(`build-freebsd/build-gui.sh:183-200`), compile each file, link a `.dylib`
into `${STAGED_OVERLAY}/System/Library/Frameworks/<Name>.framework/...`, then
`require_frameworks` before the next stage that depends on it
(`build-freebsd/build-gui.sh:229-246`). **Any build-order plan for these six
frameworks should extend this same script/pattern**, not invent a new one —
the "honest" section of `docs/SPEC-gui-build.md:182-231` already lists the
concrete failure modes this pattern hits (bare `-lname` instead of the
`src/native/` Mach-O wrapper scheme, `extract_sources()` regex fragility,
mixed `-mmacosx-version-min`, etc.) and those apply equally to the six
frameworks below.

Verified on host 2026-08-26 (per `docs/SPEC-gui-build.md:89-95`): overlay has
only `CoreFoundation.framework DirectoryService.framework Foundation.framework
LDAP.framework SystemConfiguration.framework`. None of the six target
frameworks, nor `IOKit`/`CoreGraphics`/`Onyx2D`, exist yet.

## 1. Per-framework table

| Framework | Source location | State | Size | Own DEPENDENCIES (from CMakeLists) | Verdict |
|---|---|---|---|---|---|
| **CoreData** | `src/external/cocotron/CoreData` (cocotron submodule, `.gitmodules:235-237`, checked out at `085c77f4`) | live Apple-derived Obj-C, not a stub | 64 files (25 `.m`, rest headers/private) | `objc system CoreFoundation Foundation` (`src/external/cocotron/CoreData/CMakeLists.txt:52-58`) | **needed for real** — it's an AppKit link-time DEPENDENCY (`AppKit/CMakeLists.txt:~548`), but its own deps are only things already staged (CF/Foundation/objc/system). Lightest of the six. |
| **CoreText** | `src/external/cocotron/CoreText` (same submodule) | live Obj-C | 32 files (13 `.m`/`.c`, rest headers) | `objc system CoreFoundation Foundation CoreGraphics Onyx2D` + native `FreeType` (`src/external/cocotron/CoreText/CMakeLists.txt:35-45`, `find_package(Freetype REQUIRED)` at line 15) | **needed for real** — AppKit's font/text stack (`KTFont*.m`, `CTFont*.m`) is not decorative; also a build-order **input** to Onyx2D itself (`build-gui.sh:251` already `-I`'s `CoreText/include` while compiling Onyx2D, i.e. Onyx2D consumes CoreText headers before CoreText the dylib is even built). Blocked on CoreGraphics + Onyx2D (other agents) + native FreeType. |
| **QuartzCore** | `src/external/cocotron/QuartzCore` (same submodule) | live Obj-C/Obj-C++, largest of the six | 90 files, but the `QuartzCore_sources` list in CMakeLists only compiles ~35 of them — all `CI*.m` (CoreImage filters) are explicitly commented out with `# CoreImage is reexported` (`src/external/cocotron/QuartzCore/CMakeLists.txt:44-52`) and pulled in instead via `-Wl,-reexport_library,.../CoreImage` / `.../CoreVideo` (lines ~93-96) plus `add_dependencies(QuartzCore CoreImage CoreVideo)` | `objc system CoreFoundation Foundation Onyx2D OpenGL CoreGraphics Metal_private` + conditionally `indium_private` (`BUILD_METAL`) + `cxx` (`src/external/cocotron/QuartzCore/CMakeLists.txt:~82-92`) | **needed for real, build last** — heaviest dependency set of the six: needs Onyx2D+CoreGraphics (other agents), **and** OpenGL, **and** the separate `CoreImage` (427 source files, `src/frameworks/CoreImage/CMakeLists.txt`) + `CoreVideo` (3 files) frameworks, which are gated on `COMPONENT_dev_gui_common` (`src/frameworks/CMakeLists.txt:~50-56`) and are **not otherwise assigned to any of the three agents** — see Risks §4. `Metal_private` is cheap (an `INTERFACE` header-only CMake target, `src/external/metal/CMakeLists.txt:88-92`, not a compiled framework). |
| **ImageIO** | `src/frameworks/ImageIO` (own repo tree, not the cocotron submodule) | live, small | 10 files (3 `.m`) | `system objc Foundation CoreFoundation Onyx2D` (`src/frameworks/ImageIO/CMakeLists.txt:16-24`) | **needed for real** — small and self-contained once Onyx2D exists. No dependency on CoreGraphics, CoreText, or the others in this list — can build in parallel with CoreData/CoreServices. |
| **CoreServices** | `src/frameworks/CoreServices` (own repo tree) | live, umbrella framework over 8 sub-frameworks | 135 files total across `src/{AE,CarbonCore,DictionaryServices,FSEvents,LaunchServices,Metadata,OSServices,SearchKit,SharedFileList}` (3/33/2/7/16/3/2/3/2 files respectively) | Umbrella depends on `FSEvents LaunchServices CarbonCore AE DictionaryServices Metadata SearchKit SharedFileList OSServices system CoreFoundation` (`src/frameworks/CoreServices/CMakeLists.txt:29-45`) and re-exports `CFNetwork` too (`CMakeLists.txt:60`, external submodule `src/external/cfnetwork`, populated). Sub-framework deps: most are `CoreFoundation + system` only; `CarbonCore` also needs `Foundation cxx icucore iconv` (`src/CarbonCore/CMakeLists.txt:53-59`); `LaunchServices` also needs `CarbonCore FMDB icucore cxx` (`src/LaunchServices/CMakeLists.txt:26-34`, external submodule `src/external/fmdb`, populated) | **needed for real** — AppKit's file/URL/launch-services glue. **No dependency on CoreGraphics/Onyx2D/OpenGL at all** — this whole tree only needs CoreFoundation (+Foundation for two subcomponents), so it can be built entirely independently of the other agents' work, in parallel with CoreData. Widest fan-out (9 separate link targets: 8 subs + umbrella) but each is small. |
| **OpenGL** | `src/frameworks/OpenGL` (own repo tree) | live — a real, non-trivial CGL-over-EGL implementation, not a stub | 13 files, but only **1** `.c` (`OpenGL.c`, 416 lines) does anything; rest are headers | `system GL GLU EGL CoreFoundation CoreGraphics` (`src/frameworks/OpenGL/CMakeLists.txt:24-31`), reexports native `libGL.dylib`/`libGLU.dylib` built by `src/native` (`CMakeLists.txt:32-34`) | see §2 — **needed at link-time by the whole AppKit/QuartzCore target, but the actual code is a thin, host-GL-backed CGL shim; the currently-written `build-gui.sh` already routes around it** (see §2). |

Where "cocotron submodule" is used above: `git submodule status` shows
`src/external/cocotron` checked out at commit `085c77f4`
(`remotes/origin/upstream-507-g085c77f4`) — populated, not an empty
submodule. `ImageIO`/`CoreServices`/`OpenGL` are **not** part of that
submodule; they live directly under `src/frameworks/` in this repo.

There is **no dev-stub usable as a drop-in substitute** for any of these six
during a real GUI build: `src/frameworks/dev-stubs/{CoreText,CoreData,
QuartzCore,ImageIO,OpenGL}` exist (`src/frameworks/CMakeLists.txt:168-176`,
no `CoreServices` stub exists at all) but they only get **installed** when
`COMPONENT_cli_dev AND NOT COMPONENT_gui` — i.e. CLI-only builds that
explicitly *don't* want Cocotron. When `COMPONENT_gui` is on, the stubs are
still compiled but marked `NO_INSTALL` with a `_stub` target suffix
(`src/frameworks/CMakeLists.txt:159-166`) — they're mutually exclusive with
the real GUI frameworks by design, not a togglable fallback. Since
`build-gui.sh` doesn't use `COMPONENT_gui` either (see §0), the dev-stubs are
simply irrelevant to this build path — using them would mean writing a
*third*, parallel staging step, not "flip a flag."

## 2. OpenGL in detail (per task's specific ask)

`src/frameworks/OpenGL/OpenGL.c` (416 lines) is a real CGL implementation
that creates contexts via **EGL** (`#include <EGL/egl.h>` at line 20,
`eglGetDisplay`/`eglChooseConfig` etc. follow) — i.e. it's already written to
go through EGL rather than a full desktop-GL/GLX path, which is compatible
with a software/offscreen EGL backend (llvmpipe) and doesn't inherently
require a real GPU. The dependency is **whole-target, not per-file**: CMake's
`add_framework(OpenGL DEPENDENCIES ... GL GLU EGL ...)` and `AppKit`'s own
`DEPENDENCIES ... OpenGL ...` are link-time framework dependencies, not
per-source-file `#import`s — so there's no way to build "most of AppKit"
while skipping OpenGL by omitting a subset of files; the dependency is at the
`add_framework()`/whole-.dylib level.

However: **the currently-written (never-run) `build-gui.sh` does not actually
link against a built `OpenGL.framework` at all.** Both its CoreGraphics link
(`build-freebsd/build-gui.sh:290`) and its AppKit link
(`build-freebsd/build-gui.sh:314`) pass a bare `-lGL` (host Mesa `libGL.so`
via `ld64.lld`'s plain `-lname` resolution — itself unverified, see
`docs/SPEC-gui-build.md:182-194` risk #1) instead of staging and linking
`OpenGL.framework`. Its own `require_frameworks AppKit` call
(`build-freebsd/build-gui.sh:293`) does **not** list `OpenGL` even though
`AppKit/CMakeLists.txt`'s `DEPENDENCIES` does — this looks like a deliberate
simplification (or an oversight) by whoever wrote that script, not something
verified to work.

Conclusion: for this specific build script's current shape, OpenGL.framework
is **not currently a hard blocker** for reaching the AppKit stage — the script
bypasses it with bare `-lGL`. But that bypass is architecturally the wrong
long-term answer for two reasons: (1) it's the same "bare `-lname` against
host `.so`" pattern flagged as `build-gui.sh`'s #1 most-likely failure mode
for Onyx2D's jpeg/png/tiff/gif linking (`docs/SPEC-gui-build.md:124-139`) —
completely unverified whether `ld64.lld -syslibroot` actually resolves it;
(2) QuartzCore's own `CMakeLists.txt` DEPENDENCIES genuinely need `OpenGL`
(the framework, not bare GL) for `CAOpenGLLayer.m`/`CAWindowOpenGLContext.m`.
So: **build the real `OpenGL.framework`** (it's cheap — 1 source file) rather
than lean on the AppKit-stage bypass, but it does **not** need to sit early in
the order for AppKit alone — only QuartzCore needs the real framework target.
Given the project's Wayland-not-OpenGL direction (CLAUDE.md), the honest
framing is: **needed for real at the API-surface level** (Cocotron's own code
calls into it), **not needed for real at the rendering level** (nothing in
this project's actual display pipeline should be issuing GL draw calls — it's
present only because Cocotron's CALayer/CAOpenGLLayer machinery expects a CGL
backend to exist, even if it's never exercised through the eventual
Wayland/software-rasterizer path). It cannot be stubbed away structurally
(link-time whole-framework dependency) but its *implementation cost* is
already near-zero (headers + 416-line EGL shim), so "build it for real" and
"stub it" cost about the same effort — building it for real is simpler.

## 3. Proposed build order

```
                    ┌─────────────┐       ┌──────────────┐
  (already staged)  │CoreFoundation├──────►│  CoreData     │  step 1
  Foundation, CF     │  Foundation  │       │ (own tree)   │
                    └─────────────┘       └──────────────┘

                    ┌─────────────┐       ┌──────────────┐
  (already staged)  │CoreFoundation├──────►│CoreServices   │  step 1
  Foundation, CF,    │  Foundation  │       │ (8 subs + umb)│  (parallel w/ CoreData)
  cxx/icucore/iconv  └─────────────┘       └──────────────┘
  (staged), FMDB
  (external, populated)

  ┌──────────┐      ┌─────────────┐       ┌──────────────┐
  │  OpenGL  │◄─────│ (native GL/  │       │              │  step 2
  │ (own tree)│      │  GLU/EGL,    │       └──────────────┘
  └──────────┘      │  host Mesa)  │
                    └─────────────┘

  [other agent]      [other agent]         ┌──────────────┐
  CoreGraphics ──────►     +      ─────────►│  ImageIO     │  step 3
  Onyx2D                                    │ (own tree)   │  (as soon as Onyx2D exists)
                                             └──────────────┘

  [other agent]      [other agent]         ┌──────────────┐
  CoreGraphics ──────►  Onyx2D  ───────────►│  CoreText    │  step 4
                                             │ (cocotron)   │  (also feeds Onyx2D's own
                                             └──────────────┘   build — build-gui.sh:251
                                                                 already -I's CoreText/include
                                                                 while compiling Onyx2D)

  CoreData +          [CoreImage 427 files  ┌──────────────┐
  CoreServices +       + CoreVideo, NOT     │ QuartzCore   │  step 5 — LAST
  CoreText +           assigned to any      │ (cocotron)   │
  OpenGL +             agent — see Risks]──►│              │
  Onyx2D + CoreGraphics [other agent] ──────►└──────────────┘
  [other agent]
```

Concretely, extending `build-freebsd/build-gui.sh`'s numbered stages:

1. **CoreData** and **CoreServices** first, in either order or in parallel —
   both depend only on things already in the overlay
   (`CoreFoundation`/`Foundation`) plus their own already-populated
   submodules (`FMDB` for `LaunchServices`). They have zero dependency on the
   other two agents' work, so they're the only two of these six that could be
   staged **today**, independent of CoreGraphics/Onyx2D progress.
   `CoreServices` is 9 separate `add_framework()` targets (8 subs + the
   umbrella `constants.m` that reexports all of them via
   `-Wl,-reexported_symbols_list,...` per-arch `.exp` files,
   `src/frameworks/CoreServices/CMakeLists.txt:64-79`), so it's mechanically
   the most fiddly of the "easy" two, even though each individual target is
   small.
2. **OpenGL** can also build today (needs `CoreGraphics` per its own
   `CMakeLists.txt` DEPENDENCIES, but see §2 — the *header* surface it needs
   from `CoreGraphics`/`CoreFoundation` is small, and native GL/GLU/EGL are
   host libraries via `src/native`'s `wrap_elf()`, `src/native/CMakeLists.txt:13-23`).
   Put it in step 2 because its own DEPENDENCIES list names `CoreGraphics`
   formally, but it's not truly gated on CoreGraphics being *finished* the
   way CoreText/QuartzCore are (OpenGL.c doesn't call into CoreGraphics
   internals, just declares the dependency for `CGLPixelFormat`-adjacent
   glue).
3. **ImageIO** as soon as **Onyx2D** exists (other agent) — it's the smallest
   of the graphics-dependent three (10 files, only 3 `.m`) and has no
   dependency on CoreText/CoreServices/CoreData, so it doesn't need to wait
   for anything else in this list.
4. **CoreText** once **CoreGraphics + Onyx2D** exist (other agent) and native
   FreeType is confirmed linkable (`pkg install freetype2`, already in
   `docs/SPEC-gui-build.md:149`'s pkg list, already checked by
   `build-gui.sh`'s preflight at `build-freebsd/build-gui.sh:109`). Put before
   QuartzCore because QuartzCore doesn't depend on CoreText directly, but
   CoreText is one of the lighter graphics-dependent frameworks and unblocks
   nothing else being sequenced later except AppKit itself.
5. **QuartzCore last**, and only after: Onyx2D, CoreGraphics (other agent),
   OpenGL (step 2, this agent's own work), and — the big caveat — **CoreImage
   (427 files) + CoreVideo (3 files)**, which QuartzCore's `CMakeLists.txt`
   hard-depends on via `add_dependencies(QuartzCore CoreImage CoreVideo)` and
   `-Wl,-reexport_library,...` (see Risks §4). `Metal_private` is free
   (header-only interface target); `indium_private` only applies if
   `BUILD_METAL` is on (auto-detected via `llvm-config`+Vulkan at CMake
   configure time, `CMakeLists.txt:232-249` — irrelevant to `build-gui.sh`,
   which doesn't use that CMake path at all, so this can be ignored for the
   raw-clang build script).

Then: `AppKit` (already scripted, `build-freebsd/build-gui.sh:292-314`, but
its `require_frameworks` call is missing an `OpenGL` check — see §2 — and its
own link line only pulls bare `-lGL`, not `OpenGL.framework`).

## 4. Risks

- **CoreImage/CoreVideo are not assigned to any of the three agents but are a
  hard build-time dependency of QuartzCore.** `add_dependencies(QuartzCore
  CoreImage CoreVideo)` (`src/external/cocotron/QuartzCore/CMakeLists.txt`,
  near the `add_framework(QuartzCore ...)` block) plus two
  `-Wl,-reexport_library,...` flags mean QuartzCore's link will fail without
  them existing as built dylibs first. CoreImage alone is 427 source files —
  bigger than all six frameworks in this task combined. This needs to be
  explicitly claimed by someone (possibly the QuartzCore builder, i.e.
  whoever executes step 5 above) or the QuartzCore stage will FATAL the same
  way AppKit currently does.
- **The "bare `-lname` against host `.so`" linking pattern is unverified for
  every native lib these frameworks touch** — not just Onyx2D's jpeg/png/tiff
  (documented risk in `docs/SPEC-gui-build.md:124-139,186-194`), but also
  CoreText's `-lfreetype`-equivalent and OpenGL's `-lGL -lGLU -lEGL`. If
  `ld64.lld -syslibroot <overlay>` can't resolve bare `-lname` to a host ELF
  `.so` the way a normal `cc` invocation would, **none** of CoreText/OpenGL
  link, regardless of source-level correctness. This should be the first
  thing tested on the FreeBSD box, before writing any more per-framework
  compile logic, since it invalidates the whole approach if it fails.
- **`extract_sources()`'s regex is untested against these six CMakeLists.txt
  files.** It was written and reasoned about only for
  `AppKit_sources`/`CoreGraphics_sources`/`Onyx2D_sources`
  (`docs/SPEC-gui-build.md:207-212`). `CoreServices` in particular doesn't
  even use a single `<Name>_sources` variable per target the same way — it's
  9 separate `add_subdirectory()`s, each with its own `CMakeLists.txt` and
  its own (unverified) sources-variable shape; the extraction helper will
  need to run once per sub-framework directory, and someone needs to check
  each of those 9 files' `set(...)` formatting individually rather than
  assume they all match the `AppKit_sources` pattern.
- **`CoreServices`'s per-arch reexport `.exp` files**
  (`src/frameworks/CoreServices/CMakeLists.txt:82-88`, files
  `reexport.exp`/`reexport_arm64.exp`) are consumed via
  `-Wl,-reexported_symbols_list,...` — this is a real linker feature of
  `ld64.lld` that hasn't been exercised anywhere else in this repo's existing
  build scripts (`build-real-macho-tests.sh`, current `build-gui.sh`); it may
  need its own verification pass separate from ordinary `-dylib` linking.
- **CoreText is also a *build input* to Onyx2D**, not just an AppKit
  dependency: `build-gui.sh` already adds `-I${COCOTRON}/CoreText -I
  ${COCOTRON}/CoreText/include` when compiling Onyx2D
  (`build-freebsd/build-gui.sh:251`) — i.e. Onyx2D's sources `#include`
  CoreText **headers** even though Onyx2D is built *before* CoreText in the
  existing script's numbering. This works today only because it's a
  header-only include (no link dependency back onto CoreText the dylib), but
  it means "CoreText" as a *header tree* needs to be present/correct before
  Onyx2D compiles, even though "CoreText" as a *linked framework* isn't
  needed until AppKit. Worth flagging so nobody assumes CoreText is purely
  downstream of Onyx2D.
- **AppKit resources are out of scope everywhere.** Even once all six
  frameworks here link, `AppKit/CMakeLists.txt:468-526`'s `.nib`/`.tiff`
  resources are not installed by `build-gui.sh`
  (`docs/SPEC-gui-build.md:224-228`) — a linked AppKit.dylib will still fail
  at runtime opening any panel that loads a NIB. Not this task's problem, but
  relevant to "when is this actually done."

## 5. What I did not check

- Did not run anything on the FreeBSD dev VM (185) — no compile, no link, no
  `pkg-config`/`pkg install` verification of freetype2/fontconfig/mesa-libs/
  libpng/tiff/jpeg/gif package names or `.so` presence on FreeBSD 15.1.
  Everything above is read from source, not executed.
- Did not read every `.m` file in `CoreData`/`CoreText`/`QuartzCore`/
  `CoreServices`'s 9 sub-targets for `#import`/`#include` statements beyond
  what CMakeLists.txt's own `include_directories()`/`DEPENDENCIES` declare —
  there could be additional transitive header needs the CMakeLists.txt
  doesn't surface (the way `docs/SPEC-gui-build.md` risk #5 already flags for
  AppKit's own 375 files).
- Did not check whether `src/external/cocotron/CoreText/CoreText.h` /
  `CoreData`'s headers are complete enough to satisfy AppKit's `#import
  <CoreText/...>` usage (mirrors the same open question
  `docs/SPEC-gui-build.md:213-219` raises for AppKit itself).
- Did not check whether the individual `.exp` (`reexport.exp`/
  `reexport_arm64.exp`) files under `CoreServices` actually list symbols that
  exist in the code as written — only that the CMake plumbing references
  them.
- Did not check FMDB's (`src/external/fmdb`) or CFNetwork's
  (`src/external/cfnetwork`) own build requirements beyond confirming the
  submodules are populated (11 and 16 top-level files respectively found) —
  did not open their CMakeLists.txt.
- Did not check `src/frameworks/CoreImage/CMakeLists.txt` or
  `src/frameworks/CoreVideo/CMakeLists.txt` in the same depth as the six
  in-scope frameworks beyond their `DEPENDENCIES` lines and source-file
  counts — they're flagged as an unassigned risk (§4), not analyzed as a
  build target the way the six in-scope ones are.
- Did not verify FreeType/fontconfig/mesa/libpng/tiff/jpeg/gif are actually
  installed on the dev VM right now — only that `build-gui.sh`'s preflight
  checks for some of them (`build-freebsd/build-gui.sh:108-126`) and that
  `docs/SPEC-gui-build.md:143-155` lists the expected `pkg install` set.
- Did not investigate the `AppKit`/`CoreGraphics` X11-vs-Wayland backend
  question (`docs/SPEC-gui-build.md §7`) — out of scope for this task, which
  is about framework link dependencies, not backend implementation.
