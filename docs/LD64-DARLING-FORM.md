# LD64-DARLING-FORM - enabling the DARLING form of fixupsEnd() in the host ld64

Control #142 follow-up. The host cctools ld64 was built in the non-DARLING form
of Atom<x86_64>::fixupsEnd() (operator[] + ud2 on the legitimate one-past-the-end
index); this enables the DARLING form via the build config and reruns the link.

## (a) The guard - file:line
src/external/cctools-port/cctools/ld64/src/ld/parsers/macho_relocatable_file.cpp:759

    #ifdef DARLING
        fixupsBegin()/fixupsEnd() = machofile()._fixups.data() + ...     // :762/:763
    #else // !DARLING
        &machofile()._fixups[...]                                        // :769/:770
    #endif

More DARLING blocks in the same tree: LinkEdit.hpp:53,1328,1339,1350;
LinkEditClassic.hpp:736,890; Options.h:330,342,371; compact_unwind.cpp:79;
objc.cpp:167,208,254,302. The switch is the DARLING macro; the host ld64 build
did not define it.

## (a) Config changes (build-host-tools/, only build config; no ld64 code)
1. build-host-tools/ld64-toolchain.cmake
     CMAKE_C_FLAGS_INIT / CMAKE_CXX_FLAGS_INIT  +=  -DDARLING
2. build-host-tools/ld64-prefix-fix.h (new), force-included by the toolchain
   CMAKE_*_FLAGS_INIT needs a fresh configure, and a fresh configure exposed a
   pre-existing quoting bug in ld64/src/CMakeLists.txt:28:
     -DPROGRAM_PREFIX="${APPLE_TARGET_TRIPLET_PRIMARY}-"
   reaches the command as -DPROGRAM_PREFIX="x86_64-apple-darwin20-"; /bin/sh
   strips the quotes, PROGRAM_PREFIX becomes the bare token x86_64-apple-darwin20-
   and Options.cpp:6325 fails ('x86_64' does not refer to a value). The shim is
   force-included (-include) and re-defines PROGRAM_PREFIX as a string literal.
   This is independent of DARLING: a fresh configure without -DDARLING failed
   identically.

## (b) Result
Build: ninja RC=0, ld64 produced (41/41).
Binary check (lldb disassemble of fixupsEnd) - no bounds check, no ud2:
  andl 0x4b(%rdi),%eax ; addl 0x40(%rdi),%eax ; addq 0x58(%rcx),%rax ; retq
  i.e. the DARLING form, _fixups.data() + (_fixupsStartIndex+_fixupsCount).
Link (narrow-build-target.sh ... libsystem_kernel.dylib): the SIGILL is GONE.
  It now fails on a DIFFERENT error - ordinary symbol resolution:
    Undefined symbols for architecture x86_64:
      "_libsimple_lock_lock", "_libsimple_lock_unlock",
      "_libsimple_rwlock_lock_read", "_libsimple_rwlock_lock_write",
      "_libsimple_rwlock_unlock_read", "_libsimple_rwlock_unlock_write"
    ld: symbol(s) not found for architecture x86_64
    cc: error: linker command failed with exit code 1
  (7 undefined _libsimple_* lock symbols; a separate issue, not the ld64 crash.)

Reproduce:
  export PATH=/usr/local/bin:/usr/local/sbin:/usr/bin:/bin:/sbin:/usr/sbin
  rm -rf "$DARLING_SRC_DIR/build-host-tools/ld64"
  cmake -S "$DARLING_SRC_DIR/src/external/cctools-port/cctools/ld64/src" \
        -B "$DARLING_SRC_DIR/build-host-tools/ld64" -G Ninja \
        -DCMAKE_TOOLCHAIN_FILE="$DARLING_SRC_DIR/build-host-tools/ld64-toolchain.cmake" \
        -DCMAKE_BUILD_TYPE=Release \
        -DAPPLE_TARGET_TRIPLET_PRIMARY=x86_64-apple-darwin20 -DTARGET_x86_64=ON
  ninja -C "$DARLING_SRC_DIR/build-host-tools/ld64"
  sh build-freebsd/narrow-build-target.sh src/external/xnu/darling/src/libsystem_kernel/libsystem_kernel.dylib
