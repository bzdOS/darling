# Control #141: firstpass link — imageLoaderCache condition + ld64.lld flag workaround

## 1. Condition for creating `imageLoaderCache` (the quote)

File: `/usr/src/contrib/llvm-project/lld/MachO/SyntheticSections.cpp`
(lines 791-807, `StubHelperSection::setUp()`):

```
791: void StubHelperSection::setUp() {
792:   Symbol *binder = symtab->addUndefined("dyld_stub_binder", /*file=*/nullptr,
793:                                         /*isWeakRef=*/false);
794:   if (auto *undefined = dyn_cast<Undefined>(binder))
795:     treatUndefinedSymbol(*undefined,
796:                          "lazy binding (normally in libSystem.dylib)");
797:
798:   // treatUndefinedSymbol() can replace binder with a DylibSymbol; re-check.
799:   stubBinder = dyn_cast_or_null<DylibSymbol>(binder);
800:   if (stubBinder == nullptr)
801:     return;
802:
803:   in.got->addEntry(stubBinder);
804:
805:   in.imageLoaderCache->parent =
806:       ConcatOutputSection::getOrCreateForInput(in.imageLoaderCache);
807:   addInputSection(in.imageLoaderCache);
```

**The condition:** the `imageLoaderCache` synthetic section is created (lines 805-807)
only when `stubBinder != nullptr`, i.e. when `dyld_stub_binder` resolves to a
`DylibSymbol` — a symbol defined in one of the linked dylibraries (normally
`libSystem.dylib`). If `dyld_stub_binder` is absent, or is a plain `Defined`
(defined in an object file of the target itself), `setUp()` returns at line 800-801
and `in.imageLoaderCache` stays `nullptr` (initialized `nullptr` at
`SyntheticSections.h:860`).

## 2. Why the firstpass link segfaults

`libsystem_dyld_firstpass.dylib` defines `dyld_stub_binder` in its own object
files (`src/dyld_stub_binder.S.o`), so at link time the symbol is a `Defined`,
not a `DylibSymbol`. `StubHelperSection::setUp()` therefore returns early and
`in.imageLoaderCache` remains `nullptr`.

The link still needs the stub helper: with chained fixups disabled (the default
for `-platform_version,macos,11,11`, which is below the macOS 13.0 chained-fixup
threshold — `Driver.cpp:1104-1125`), every stub for a `DylibSymbol` is recorded
in the lazy-binding section (`SyntheticSections.cpp:739,748` via
`addBindingsForStub`), so `StubHelperSection::isNeeded()` is true
(`SyntheticSections.cpp:780`). `StubHelperSection::writeTo` then calls
`X86_64::writeStubHelperHeader`, which dereferences `in.imageLoaderCache->getVA()`
(`Arch/X86_64.cpp:159`) → nullptr → SIGSEGV.

Reproduced (before the workaround), tail of the link log:

```
 #6 0x000000000062963d lld::macho::InputSection::getVA(unsigned long) const (ld64.lld+0x62963d)
 #8 0x000000000064db45 lld::macho::StubHelperSection::writeTo(unsigned char*) const (ld64.lld+0x64db45)
clang++: error: unable to execute command: Segmentation fault (core dumped)
```

## 3. The flag workaround: `-fixup_chains`

With `-fixup_chains`, stubs for `DylibSymbol`s are recorded as GOT entries
(`SyntheticSections.cpp:765-766`, `StubsSection::addEntry`) instead of
lazy-binding entries, so the lazy-binding section stays empty,
`StubHelperSection::isNeeded()` is false, the stub helper is not emitted, and the
nullptr `imageLoaderCache` is never dereferenced. The link succeeds.

The flag is added to the firstpass target only, in the salvage CMakeLists
(`build-freebsd/dyld-salvage/src/external/dyld/CMakeLists.txt`):

```cmake
set_property(TARGET system_dyld_firstpass APPEND_STRING PROPERTY
	LINK_FLAGS " -Wl,-platform_version,macos,${CMAKE_OSX_DEPLOYMENT_TARGET},${CMAKE_OSX_DEPLOYMENT_TARGET} -Wl,-fixup_chains")
```

This changes only how the linker binds the same inputs (GOT/chained-fixups
instead of classic lazy opcodes); the target, objects and dylib inputs are
unchanged. No lld source was modified.

## 4. Reproduction

Set `DARLING_SRC_DIR` / `DARLING_OVERLAY` / `DARLING_BUILD_DIR` to the source,
overlay and build trees, then:

```sh
export PATH=/usr/local/bin:/usr/local/sbin:/usr/bin:/bin:/sbin:/usr/sbin
cd "$DARLING_SRC_DIR" && sh build-freebsd/build.dy-only.sh
# -> RC=0, builds system_loader + libsystem_dyld_firstpass.dylib (with -fixup_chains)

llvm-nm -gU "$DARLING_BUILD_DIR/dyld-only/src/external/dyld/libsystem_dyld_firstpass.dylib"
```

The generated firstpass link command contains `-Wl,-fixup_chains` (verified:
`ninja -t commands src/external/dyld/libsystem_dyld_firstpass.dylib | grep -c fixup_chains` → 1).

## 5. Link result

`sh build-freebsd/build.dy-only.sh` → **RC=0**. The firstpass dylib is produced:

```
$DARLING_BUILD_DIR/dyld-only/src/external/dyld/libsystem_dyld_firstpass.dylib
Mach-O 64-bit x86_64 dynamically linked shared library, flags:<|DYLDLINK|WEAK_DEFINES|BINDS_TO_WEAK|NO_REEXPORTED_DYLIBS>
883584 bytes
```

## 6. `llvm-nm -gU` of the fresh dylib (180 exported symbols)

```
0000000000019fb0 T _CRSetCrashLogMessage
00000000000013a0 T _NSAddImage
00000000000012a0 T _NSAddLibrary
0000000000001320 T _NSAddLibraryWithSearching
0000000000001120 T _NSAddressOfSymbol
0000000000001bf0 T _NSCreateObjectFileImageFromFile
0000000000001cb0 T _NSCreateObjectFileImageFromMemory
0000000000001d80 T _NSDestroyObjectFileImage
0000000000002290 T _NSGetSectionDataInObjectFileImage
00000000000008e0 T _NSInstallLinkEditErrorHandlers
00000000000021c0 T _NSIsSymbolDefinedInObjectFileImage
0000000000000af0 T _NSIsSymbolNameDefined
0000000000000c80 T _NSIsSymbolNameDefinedInImage
0000000000000bb0 T _NSIsSymbolNameDefinedWithHint
0000000000000a70 T _NSLibraryNameForModule
0000000000002370 T _NSLinkEditError
0000000000001e40 T _NSLinkModule
0000000000000d50 T _NSLookupAndBindSymbol
0000000000000e10 T _NSLookupAndBindSymbolWithHint
0000000000000f90 T _NSLookupSymbolInImage
0000000000000ed0 T _NSLookupSymbolInModule
00000000000011e0 T _NSModuleForSymbol
00000000000009b0 T _NSNameOfModule
0000000000001060 T _NSNameOfSymbol
0000000000001f10 T _NSSymbolDefinitionCountInObjectFileImage
0000000000001fc0 T _NSSymbolDefinitionNameInObjectFileImage
0000000000002080 T _NSSymbolReferenceCountInObjectFileImage
00000000000020f0 T _NSSymbolReferenceNameInObjectFileImage
0000000000002450 T _NSUnLinkModule
0000000000001460 T _NSVersionOfLinkTimeLibrary
```

(full output: 180 symbols, all `T` — the firstpass dyld exports; see the
reproduction command above for the complete list.)

## 7. Flag matrix (what was tried on the firstpass link)

| Flag | Result |
|---|---|
| (baseline) `-flat_namespace -undefined,suppress` | RC=1 — SIGSEGV in `StubHelperSection::writeTo` → `InputSection::getVA` (nullptr `imageLoaderCache`) |
| `-undefined,dynamic_lookup` | RC=1 — same SIGSEGV |
| `-no_fixup_chains` (explicit) | RC=1 — same SIGSEGV (this is already the default for macOS 11.0) |
| **`-fixup_chains`** | **RC=0 — link succeeds, valid dylib, nm -gU clean** |

Only `-fixup_chains` avoids the crash: it is the one flag that keeps the
lazy-binding section empty (stubs → GOT) so the stub helper — the only writer
that dereferences `imageLoaderCache` — is not emitted.
