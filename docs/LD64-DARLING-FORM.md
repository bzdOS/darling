# LD64-DARLING-FORM - enabling the DARLING form of fixupsEnd() in the host ld64

Control #142 follow-up. Goal: the host cctools ld64 is built in the non-DARLING
form of Atom<x86_64>::fixupsEnd() (operator[] + ud2 on the legitimate
one-past-the-end index); enable the DARLING form via the build config, rebuild
ONLY ld64, run the libsystem_kernel link.

## (a) The guard - file:line
src/external/cctools-port/cctools/ld64/src/ld/parsers/macho_relocatable_file.cpp:759

    #ifdef DARLING
        virtual ld::Fixup::iterator fixupsBegin() const { return machofile()._fixups.data() + _fixupsStartIndex; }         // :762
        virtual ld::Fixup::iterator fixupsEnd()   const { return machofile()._fixups.data() + (_fixupsStartIndex+_fixupsCount); }  // :763
    #else // !DARLING
        virtual ld::Fixup::iterator fixupsBegin() const { return &machofile()._fixups[_fixupsStartIndex]; }               // :769
        virtual ld::Fixup::iterator fixupsEnd()   const { return &machofile()._fixups[_fixupsStartIndex+_fixupsCount]; }   // :770
    #endif

The switch is the DARLING macro. The same tree has more DARLING blocks:
LinkEdit.hpp:53,1328,1339,1350; LinkEditClassic.hpp:736,890; Options.h:330,342,371;
compact_unwind.cpp:79; objc.cpp:167,208,254,302. The host ld64 build does not
define DARLING.

## (a) The config change made (build-host-tools/ld64-toolchain.cmake)
    -set(CMAKE_C_FLAGS_INIT "")
    -set(CMAKE_CXX_FLAGS_INIT "")
    +set(CMAKE_C_FLAGS_INIT "-DDARLING")
    +set(CMAKE_CXX_FLAGS_INIT "-DDARLING")

## (b) Rebuild result - FAILS, and NOT because of DARLING
CMAKE_*_FLAGS_INIT only takes effect on a fresh configure (the cached
CMAKE_CXX_FLAGS wins otherwise), so the ld64 build was re-configured. That fresh
configure then fails on an unrelated, pre-existing issue:

    ld64/src/CMakeLists.txt:28:   -DPROGRAM_PREFIX="${APPLE_TARGET_TRIPLET_PRIMARY}-"

The generated ninja command carries those quotes unescaped:

    -DPROGRAM_PREFIX="x86_64-apple-darwin20-"

/bin/sh strips them, so the macro becomes the bare token x86_64-apple-darwin20-
and Options.cpp:6325 fails:

    ld/Options.cpp:6325:12: error: 'x86_64' does not refer to a value
    ld/Options.cpp:6325:12: error: use of undeclared identifier 'apple'
    ld/Options.cpp:6325:12: error: use of undeclared identifier 'darwin20'
      6325 |  argv[0] = PROGRAM_PREFIX "ld_classic";

Proof it is independent of DARLING: a fresh configure WITHOUT -DDARLING fails
identically (the same three errors at Options.cpp:6325). So the blocker is the
CMakeLists PROGRAM_PREFIX quoting, not the DARLING macro.

Fixing it means editing the ld64 CMakeLists (or Options.cpp), which this step
forbids ("код ld64 не править", "касается только конфига сборки").

## Result
No ld64 was produced -> no link result. BLOCKED on the CMakeLists:28
PROGRAM_PREFIX quoting. The config change (-DDARLING) is in place and correct in
itself, but cannot be exercised until the host-ld64 configure can complete. The
previous (non-DARLING) ld64 binary was restored so the tree still links.
