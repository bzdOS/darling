# SPEC: build-freebsd/build-coredata-coreservices.sh

Status: **research + script written, nothing built or run.** No FreeBSD box
was touched. This document explains what `build-freebsd/build-coredata-
coreservices.sh` does, in what order, and where it is most likely to fail.
Written by reading `CMakeLists.txt` files, existing `build-freebsd/*.sh`
scripts, and — for one specific claim below — by reproducing a clang failure
locally on this (Linux, Ubuntu clang 18) machine, not the FreeBSD dev VM. See
`docs/SPEC-remaining-frameworks.md` for how CoreData/CoreServices were
selected as "the only two of six remaining frameworks buildable today" and
`docs/SPEC-gui-build.md` for the base pattern (`build-freebsd/build-gui.sh`)
this script extends.

## 1. What it builds, and in what order

```
CoreFoundation.dylib, Foundation.dylib   (already in overlay, staged read-only)
        │
        ├──► CoreData.dylib                                    [1/3]
        │      cocotron submodule, 25 .m files, single link target
        │
        └──► CoreServices subframeworks (each own dylib,        [2/3]
             nested under CoreServices.framework/Versions/A/
             Frameworks/<Sub>.framework/):
                 AE                 (empty.c + stub.c)
                 DictionaryServices (empty.c)
                 OSServices         (empty.c)
                 SearchKit          (SKAnalysis.c + SKIndex.c)
                 SharedFileList     (constants.c)
                 Metadata           (MDQuery.c + MDItem.c, needs libdispatch.dylib)
                 CarbonCore         (28 files, C++, needs real ICU headers +
                                      libicucore.A.dylib's C++ symbols)
                       │
                       ▼
             CoreServices.dylib (umbrella, constants.m)          [3/3]
               -reexport_library × 7 (the above)
               -reexported_symbols_list reexport.exp
```

Both `CoreData.dylib` and `CoreServices.dylib` (plus its 7 built
subframeworks) are copied into the **persistent** overlay
(`$DARLING_OVERLAY/System/Library/Frameworks/...`), the same side effect
`build-real-macho-tests.sh` has for `Foundation.dylib` — needed so
`build-gui.sh`'s `require_frameworks AppKit CoreText CoreData QuartzCore
ImageIO CoreServices` (`build-freebsd/build-gui.sh:293`) stops failing on
`CoreData`/`CoreServices` specifically (it will still FATAL on `CoreText`,
`QuartzCore`, `ImageIO` — those are blocked on Onyx2D/CoreGraphics, out of
this task's scope).

## 2. Deliberate deviations from the real CMakeLists.txt

These are not silent omissions — the script prints a loud banner
(`build-freebsd/build-coredata-coreservices.sh`'s "DELIBERATELY SKIPPED"
section) before building the umbrella, and this doc explains why each one
was cut instead of attempted.

### 2.1 FSEvents — SKIPPED entirely

`src/frameworks/CoreServices/src/FSEvents/FSEventsImpl.m:46,70` calls
`inotify_init1()`/`inotify_add_watch()` directly; `fseventsd.m:142,149` calls
`fanotify_init()`/`fanotify_mark()` directly. These are **real Linux kernel
syscalls**, not part of any macOS API surface Darling reimplements over BSD
primitives elsewhere in this port — they are Linux-specific filesystem
notification mechanisms with **no FreeBSD equivalent** (FreeBSD's answer is
`kqueue`/`EVFILT_VNODE`, structurally different, not a drop-in). The headers
that declare these calls do exist locally
(`src/external/xnu/darling/src/libsystem_kernel/emulation/include/
linux_premigration/ext/fanotify.h`, and a second local copy at
`src/frameworks/CoreServices/src/FSEvents/linux/fanotify.h`) — so FSEvents
would plausibly **compile** — but the actual symbols
(`fanotify_init`/`fanotify_mark`/`inotify_init1`/`inotify_add_watch`) are Linux
syscalls; there's nothing in this FreeBSD-targeted `libSystem.B.dylib` that
would define them, so it would fail at **link** time with undefined symbols,
or — worse — link cleanly against some unrelated same-named host symbol and
misbehave at runtime. Making FSEvents work for real means rewriting
`FSEventsImpl.m` against kqueue, which is a development task (same framing
`build-gui.sh` uses for the missing Wayland backend), not something this
build script can produce.

**Consequence:** the `fseventsd` daemon (`add_darling_executable(fseventsd
...)`, `src/frameworks/CoreServices/src/FSEvents/CMakeLists.txt`) is also
not built — it's a separate executable target anyway, out of scope for "the
CoreServices framework" even if FSEvents itself worked.

### 2.2 LaunchServices — SKIPPED entirely

`src/frameworks/CoreServices/src/LaunchServices/LSRunning.m:20-21` includes
`<xpc/xpc.h>` and `<xpc/private.h>`; `LaunchServices.c:24` includes
`<launch_priv.h>`. **XPC is not implemented anywhere in this port** (not
checked exhaustively — see §4 — but no `xpc.framework`/`libxpc` build target
turned up in the same searches that found everything else in this doc).
Without XPC headers/symbols, `LSRunning.m` cannot compile at all, not just
fail to link. LaunchServices also needs its own `FMDB` dependency
(`src/external/fmdb`, populated, `sqlite3`-backed Objective-C wrapper — the
one part of LaunchServices' dependency chain that IS readily buildable) and
`CarbonCore` (built by this script) — but the XPC blocker makes the rest
moot. Also drops `launchservicesd` (separate daemon subdirectory,
`add_subdirectory(launchservicesd)` in LaunchServices' own CMakeLists.txt —
not inspected at all, out of scope the same way `fseventsd` is).

### 2.3 CFNetwork reexport — SKIPPED

The real `CMakeLists.txt` (`src/frameworks/CoreServices/CMakeLists.txt:60`)
reexports `CFNetwork` (`src/external/cfnetwork`, populated submodule, own
`CMakeLists.txt` at `src/external/cfnetwork/src/CMakeLists.txt`) into the
umbrella alongside the 9 subframeworks. Building CFNetwork itself is a real
networking-stack port (HTTP/TLS/proxy config etc.) — explicitly out of scope
per `docs/SPEC-remaining-frameworks.md`'s framing of this task ("CoreData and
CoreServices... only need CoreFoundation and Foundation") and not attempted
here. The umbrella link in this script omits the corresponding
`-reexport_library` line rather than pointing it at a nonexistent dylib.

**Net effect of §2.1–2.3:** the built `CoreServices.dylib` reexports 7 of the
9 real subframeworks (missing `FSEvents`, `LaunchServices`) and skips the
`CFNetwork` reexport the real umbrella also carries. Anything that links
against `CoreServices.framework` expecting `FSEventStream*`,
`LSCopyApplicationURLsForBundleIdentifier`-style LaunchServices calls, or
CFNetwork symbols to resolve **through CoreServices' reexport** will fail to
link. Since `build-gui.sh`'s own `require_frameworks` check
(`build-freebsd/build-gui.sh:293`) only checks that
`CoreServices.framework/Versions/*/CoreServices` exists as a file, not that
it exports every real symbol, this narrows but does not eliminate the risk
that AppKit's own build later fails at **its** link step over one of these
missing symbols (AppKit's `#import <CoreServices/...>` usage was not
audited for this task — see §4).

## 3. Reused / extended build mechanism

Same pattern as `build-freebsd/build-real-macho-tests.sh` (Foundation) and
`build-freebsd/build-gui.sh` (Onyx2D/CoreGraphics/AppKit):

- Unpack `tests/vendor/macosx-sdk-flat.tar.gz` for Apple SDK headers
  (regular files only — virtiofs can't resolve the SDK's symlinks, see
  `tests/vendor/README.md`).
- Copy `libSystem.B.dylib`, `libobjc.A.dylib`, `libicucore.A.dylib`,
  `libc++.1.dylib`, `libc++abi.dylib`, `usr/lib/system/*`,
  `CoreFoundation.dylib`, `Foundation.dylib` from the persistent overlay into
  a throwaway `staged-overlay/` (same virtiofs-symlink reasoning).
- `-F${SDK_FLAT}/Frameworks` for `<CoreFoundation/CoreFoundation.h>` — see
  the header comment on this exact trap in `build-freebsd/build-gui.sh:165-
  173` and the task prompt's own warning. **Verified locally** (not on
  FreeBSD) that the tarball actually contains a real, non-dangling
  `Frameworks/CoreFoundation.framework/Headers/CoreFoundation.h` (109 lines,
  a real umbrella header) despite `tests/vendor/README.md:42-46`'s comment
  that the *original* SDK's copy is a dangling symlink — the flatten script
  must special-case it (`README.md:47-50` copies a *directory*, `tar -chf -`
  with `-h` dereference, from a location where it does resolve). This
  contradicts nothing — `hello-cf.c`'s own comment
  (`build-real-macho-tests.sh:113-115`) is about a *different* file
  (`corefoundation-headers/` from the *submodule* checkout, a different tree
  from the *Apple SDK's* `Frameworks/CoreFoundation.framework/Headers/`) —
  but it's worth flagging since the two "CoreFoundation.h" claims sound
  contradictory at first read.
- `extract_sources()` (verbatim copy of `build-gui.sh`'s version): regexes a
  CMake `set(<Name>_sources ...)` block. **Only usable for CoreData
  (`CoreData_sources`) and CarbonCore (`CarbonCore_SRCS`)** — confirmed by
  reading all 9 CoreServices sub-CMakeLists.txt files that the other 8
  (`AE`, `DictionaryServices`, `FSEvents`, `LaunchServices`, `Metadata`,
  `OSServices`, `SearchKit`, `SharedFileList`) list their sources **inline**
  inside the `add_framework(... SOURCES a.c b.c ... DEPENDENCIES ...)` call
  itself, not as a separate `set()` variable — exactly the risk
  `docs/SPEC-remaining-frameworks.md:228-237` predicted. Since each of those
  is 1-2 files, this script hardcodes their source lists directly (see
  `build_sub` calls) rather than writing a second, differently-shaped
  extraction regex for a one-time, small, manually-verifiable list. **This
  means if upstream ever adds a file to one of these small sub-CMakeLists.txt
  without a matching update here, the new file is silently NOT compiled** —
  unlike `extract_sources()`-driven targets, which FATAL loudly if their
  regex comes up empty but say nothing if the *set* of files just grows.
- `compile_list()` (this script's equivalent of `build-gui.sh`'s
  `compile_component()`): same `-w -O0` compile-each-file-separately
  approach, extended with a `case` on file extension to pick `-x objective-
  c++` for `.cpp`, `-x objective-c` for `.m` and (matching
  `build-real-macho-tests.sh`'s Foundation convention) `.c`.
- `require_frameworks`-style checks: this script uses a narrower
  `require_dylib` (checks one exact file path) instead of `build-gui.sh`'s
  `find ... -name "${fw}"` glob, since it only ever needs to check one
  external dylib (`libdispatch.dylib`), not a `.framework` bundle.

## 4. Risks, ranked by how likely they are to actually break the build

1. **CarbonCore's C++ standard library headers vs. `-nostdinc` — VERIFIED
   BROKEN locally (Linux clang, not FreeBSD).** `CLANG_FLAGS` carries
   `-nostdinc` (needed so every other header resolves through the staged
   SDK/overlay, not the build host's own `/usr/include` — same as
   `build-gui.sh`). CarbonCore is the **first** target across all three
   raw-clang build scripts in this repo to need real C++ standard headers
   (`<vector>`, `<map>`, `<mutex>`, `<algorithm>`, ...) — CoreData/Onyx2D/
   CoreGraphics/AppKit are all plain C/Objective-C. Reproduced directly on
   this machine: `echo '#include <vector>' | clang -x c++ -nostdinc -stdlib=
   libc++ -E -v -` fails with `'vector' file not found` — `-nostdinc` strips
   clang's normal libc++ resource-dir search path, and `-stdlib=libc++`
   alone does not restore it. The script works around this by `find`-ing a
   `.../c++/v1` directory under `/usr/local` **at run time on the FreeBSD
   box itself** (path varies by installed llvm/clang pkg version, so it
   can't be hardcoded from a machine that has never seen the real path) and
   `-isystem`-ing it in, then FATALs early with a clear message + `find`
   command to run by hand if that directory doesn't exist. **This exact
   probe has never been run on FreeBSD 15.1** — the fallback path
   (`-isystem` a directory found by pattern-matching `*c++/v1`) could still
   miss version-specific quirks (e.g. an ABI mismatch between the found
   headers and whatever `libc++.1.dylib`/`libc++abi.dylib` were already
   built with in the overlay).
2. **`libicucore.A.dylib` may not export ICU's C++ class symbols.**
   `CarbonCore/MacLocales.cpp`, `UnicodeUtilities.cpp`,
   `TextEncodingConverter.cpp` use real ICU C++ classes (`icu::Locale`,
   `icu::Collator`, mangled C++ symbols like `_ZN3icuXX...`), not just ICU's
   C `u_`-prefixed function surface. Every other script in this repo that
   links `libicucore.A.dylib` (`build-real-macho-tests.sh`,
   `build-gui.sh`) does so only because Foundation/Onyx2D transitively need
   *some* ICU symbol, and none of them are known to use the C++ class API
   specifically — so this is the first time this exact symbol set is
   exercised. If the staged `libicucore.A.dylib` was built C-API-only (a
   common trimming for a libicucore reimplementation), the CarbonCore link
   fails with undefined C++-mangled symbols, and no header-level fix helps.
3. **`-reexport_library` / `-reexported_symbols_list` passed bare to
   `ld64.lld`, not `-Wl,`-prefixed, and never exercised by any script in
   this repo before this one.** The real `CMakeLists.txt`
   (`cmake/use_ld64.cmake:183-186`) uses `-Wl,-reexport_library,...` because
   CMake links through the `cc`/`clang` driver, which needs `-Wl,` to pass
   options through to the linker. This script — like `build-gui.sh` and
   `build-real-macho-tests.sh` before it — invokes `ld64.lld` **directly**,
   not through `clang`, so the `-Wl,` wrapper would be wrong (it would try
   to pass a literal `-Wl,-reexport_library,...` string as one argument to
   ld64.lld, which doesn't understand `-Wl,`). This script passes
   `-reexport_library <path>` and `-reexported_symbols_list <file>` as bare,
   separate arguments, on the assumption that ld64.lld's own flag parser
   accepts the same option spellings cctools' real `ld` does. **Unverified**
   — if ld64.lld requires a different spelling or doesn't implement
   `-reexported_symbols_list` at all, the umbrella link (step 3/3) fails,
   independent of whether every subframework built cleanly.
4. **CoreServices' `include/` header tree was spot-checked, not read in
   full.** Confirmed `-I${CS}/include` resolves `<AE/AE.h>`,
   `<CarbonCore/...>`, `<SharedFileList/SharedFileList.h>`, etc. (the
   directory literally contains one subdir per (sub)framework name,
   matching the import prefix — same pattern as CoreData's and Foundation's
   own `include/` trees). Did **not** open all 237 header files under
   `src/frameworks/CoreServices/include/` + `src/CarbonCore/*.h` to check
   for further transitive `#include`s the way `docs/SPEC-gui-build.md`'s own
   risk #5 flags for AppKit — only the `.m`/`.c`/`.cpp` **implementation**
   files' top-level includes were grepped (§ of this doc and inline script
   comments cite exactly which files were checked).
5. **`NSXMLPersistentStore.m` (one of CoreData's 25 sources) was not read.**
   Its name implies XML parsing (likely via `libxml2` or a CF XML API) —
   `CoreData/CMakeLists.txt`'s own `DEPENDENCIES` list
   (`objc system CoreFoundation Foundation`) doesn't mention `libxml2`
   separately, suggesting it's expected to go through `CFXMLNode`/
   `CFXMLParser` (part of `CoreFoundation.h`'s own umbrella, conditionally
   compiled per `#if !DEPLOYMENT_RUNTIME_SWIFT` — seen while reading the
   flattened `CoreFoundation.h` for risk item in §3) rather than a separate
   native XML library — but this was inferred, not confirmed by reading the
   file.
6. **AppKit's own (unbuilt, in `build-gui.sh`) `#import <CoreServices/...>`
   / `<CoreData/...>` usage was not audited.** This script only guarantees
   the two frameworks link as standalone dylibs with the symbols their own
   sources define — it does not check whether AppKit's actual call sites
   into CoreData/CoreServices (`AppKit/CMakeLists.txt` DEPENDENCIES,
   `build-freebsd/build-gui.sh:293`) need anything from the two skipped
   subframeworks (FSEvents, LaunchServices) or CFNetwork. If they do,
   AppKit's own future link step is where that surfaces, not this script.
7. **10.12 vs. 10.10 `-mmacosx-version-min` deviation.** Both CoreData's own
   `CMakeLists.txt:14` and CoreServices' own build request
   `-mmacosx-version-min=10.10` (matching `build-gui.sh`'s Onyx2D/
   CoreGraphics/AppKit choice); this script uses 10.12 throughout instead,
   to match the already-built `Foundation.dylib` (built at 10.12 by
   `build-real-macho-tests.sh`) both frameworks link against. This is a
   deliberate choice per this task's explicit instruction, not an oversight
   — but it means when `build-gui.sh`'s Onyx2D/CoreGraphics/AppKit (10.10)
   eventually link against `CoreData.dylib`/`CoreServices.dylib` (10.12,
   built here) in the same process, the binary ends up with **mixed
   version-min metadata across its dependency graph**, an inherent
   consequence of Foundation and Onyx2D/AppKit having been built at
   different version-mins by *different, independently-written* scripts
   before this task started — not something this script alone can resolve
   without also changing `build-gui.sh` (out of scope here).
8. **`-x objective-c++` for CarbonCore's `.cpp` files is a convention
   choice, not a requirement confirmed by reading the files.** None of
   CarbonCore's `.cpp` files were seen using `@interface`/`@implementation`
   or other ObjC syntax in the grepped `#include` header — they may be
   compilable as plain `-x c++` instead. `-x objective-c++` was chosen only
   for consistency with this port's blanket `-fobjc-runtime=...`/`-fblocks`
   flags applied everywhere else; if it causes trouble (e.g. some libc++
   header interacting badly with `-fobjc-runtime`), dropping to plain `c++`
   for this one target is a one-line, low-risk change.

## 5. What I did not check

- Did not run anything on the FreeBSD dev VM (185) — no compile, no link, no
  `pkg` verification of llvm/clang/ld64.lld presence or version, no check of
  what `/usr/local/.../c++/v1` actually resolves to there. The `-nostdinc` +
  libc++ failure (§4.1) was reproduced on this (Linux) machine only; the
  script's `find`-based workaround is untested on the real target.
- Did not open `src/external/cfnetwork/src/CMakeLists.txt` beyond confirming
  it exists — CFNetwork's own dependency chain (§2.3) is unexamined.
- Did not open `src/frameworks/CoreServices/src/LaunchServices/
  launchservicesd/` or `src/frameworks/CoreServices/src/FSEvents/
  fseventsd.m` beyond the specific `fanotify_init`/`fanotify_mark` calls
  already quoted in §2.1 — both are separate executable targets out of
  scope regardless.
- Did not check whether `xpc.framework`/`libxpc` exists ANYWHERE in this
  repo outside the two `#include`s that motivated skipping LaunchServices
  (§2.2) — it is asserted "not implemented anywhere in this port" based on
  the same searches that turned up everything else in this document, not an
  exhaustive repo-wide audit.
- Did not read all 237 header files under `CoreServices/include/` +
  `src/CarbonCore/*.h`, nor all 25 CoreData headers, for further transitive
  `#include`s beyond what the `.m`/`.c`/`.cpp` implementation files
  themselves directly include (§4.4).
- Did not verify `libicucore.A.dylib`'s actual exported symbol table (no
  `nm`/`otool` access to the real FreeBSD-built binary) — §4.2's C++-symbol
  risk is inferred from source code usage, not confirmed against the built
  dylib.
- Did not verify `ld64.lld --help` / its source for the exact accepted
  spelling of `-reexport_library`/`-reexported_symbols_list` as bare
  top-level flags (§4.3) — inferred from cctools' `ld` option naming
  convention, not confirmed against whatever ld64.lld version `pkg install
  llvm` provides on FreeBSD 15.1.
- Did not check `NSXMLPersistentStore.m`'s actual contents (§4.5).
- Did not check FMDB's own `CMakeLists.txt`/sources in depth (moot here
  since LaunchServices, FMDB's only consumer in this task's scope, is
  skipped — noted for whoever picks LaunchServices back up later).
