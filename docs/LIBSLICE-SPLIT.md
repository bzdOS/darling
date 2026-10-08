# LIBSLICE-SPLIT - one libsyscall slice per thin link

Control #142 step 5. The libsystem_kernel narrow link failed on 267 duplicate
symbols because both libsyscall slices reached one link; this splits them.

## (0) Source of the double slice
build-freebsd/narrow-build-target.sh, the fat-link branch (lines ~113-120).
The link command comes from `ninja -t commands "$TARGET" | grep -F -- "-o $TARGET"`
(narrow-build-target.sh:60), which is the FAT target's link and therefore lists
BOTH slices:
  src/external/xnu/darling/src/libsystem_kernel/libsyscall/CMakeFiles/libsyscall_32.dir/.../clock-i386-User.c.o
  src/external/xnu/darling/src/libsystem_kernel/libsyscall/CMakeFiles/libsyscall_64.dir/.../clock-x86_64-User.c.o
The old split only rewrote `-arch i386 -arch x86_64` -> `-arch i386|x86_64` and
kept the whole object list, so each thin link still carried the other slice and
died with duplicates (ld: 267 duplicate symbols for architecture x86_64).

## Change (build-freebsd/narrow-build-target.sh, the two sed lines)
- collapse BOTH arch-pair orders (`-arch i386 -arch x86_64` and
  `-arch x86_64 -arch i386`; the command has two of each) to the thin arch;
- drop the other slice's objects: libsyscall_64.* from the i386 link,
  libsyscall_32.* from the x86_64 link.

## (1) x86_64 relink
  $ sh /tmp/nbt-lx.sh   # the derived x86_64 thin link
  RC=0  duplicates=0  Undefined symbols=0
  -> libsystem_kernel.dylib.x86_64 (Mach-O 64-bit x86_64 dylib, 678556 bytes)

## (2) i386 relink - explicit refusal
  $ sh /tmp/nbt-li.sh
  RC=1  duplicates=0  Undefined symbols=1 (17 symbols)
  Undefined symbols for architecture i386:
    ___memcpy_chk, ___memset_chk, ___stack_chk_fail, ___stack_chk_guard,
    ___strcat_chk, ___strcpy_chk, ___strlcpy_chk, ___strncpy_chk, ___udivdi3,
    _getenv, _isspace, _puts, _strcasecmp, _strcat, _strdup, _strncasecmp_l,
    _strtok_r, _sys_fstatfs64, ...
  ld: symbol(s) not found for architecture i386
These are libc / compiler-rt symbols that the i386 slice should pull from the
firstpass dylibs, but those dylib inputs are x86_64-only, so the i386 slice
cannot be linked from this narrow set. The duplicate error is gone; the i386
slice is refused for this reason (not fixed here).

## (3) Fresh dylib exports
  $ llvm-nm -gU .../libsystem_kernel.dylib.x86_64 | grep -cE 'os_unfair_lock_(lock|unlock)'
  0        (total exported symbols: 1360)
So the x86_64 dylib does NOT export _os_unfair_lock_lock/_os_unfair_lock_unlock.

## Note
The full script runs li.sh (i386) before lx.sh and exits on the i386 failure, so
it reports RC=1 and does not lipo the fat dylib; the x86_64 slice is produced by
lx.sh. Reproduce the x86_64 result with the commands above (or run the script and
then lx.sh).

Reproduce:
  export PATH=/usr/local/bin:/usr/local/sbin:/usr/bin:/bin:/sbin:/usr/sbin
  export DARLING_SRC_DIR=... DARLING_OVERLAY=... DARLING_BUILD_DIR=...
  sh build-freebsd/narrow-build-target.sh src/external/xnu/darling/src/libsystem_kernel/libsystem_kernel.dylib
  sh /tmp/nbt-lx.sh          # x86_64 thin link: RC=0
  llvm-nm -gU "$DARLING_BUILD_DIR/src/external/xnu/darling/src/libsystem_kernel/libsystem_kernel.dylib.x86_64"
