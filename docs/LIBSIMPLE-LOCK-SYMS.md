# LIBSIMPLE-LOCK-SYMS - supplying the _libsimple_* lock symbols to the narrow link

Control #142 step 4. The libsystem_kernel narrow link failed
on 7 undefined _libsimple_* symbols; this supplies their definitions.

## (0) Defining file
src/libsimple/src/lock.c
  void libsimple_lock_lock(libsimple_lock_t*)              :108
  void libsimple_lock_unlock(libsimple_lock_t*)            (lock.c)
  void libsimple_rwlock_lock_read(libsimple_rwlock_t*)     :302
  void libsimple_rwlock_lock_write(libsimple_rwlock_t*)    :364
  void libsimple_rwlock_unlock_read(libsimple_rwlock_t*)   :350
  void libsimple_rwlock_unlock_write(libsimple_rwlock_t*)  :410
Declared in src/libsimple/include/libsimple/lock.h; used from
libsystem_kernel/emulation/src/common/guarded/table.c and
.../xnu_syscall/bsd/impl/unistd/{getuid,getgroups}.c.

The link already carries src/libsimple-darling/liblibsimple_darling.a, but that
archive was produced by the cctools ar wrapper and its symbols do not resolve
(llvm-nm on the archive is empty; the extracted member has them). The prebuilt
fat object
  src/libsimple-darling/CMakeFiles/libsimple_darling.dir/src/lock.c.o
does define all 13 _libsimple_* symbols (Mach-O fat, i386+x86_64).

## Change (build-freebsd/narrow-build-target.sh, one block)
Before the link, insert that fat lock.o into /tmp/nbt-link.sh just ahead of
src/libsimple-darling/liblibsimple_darling.a, so it goes into both thin slices.

## (1) Relink
  [libsystem_kernel] added libsimple lock.o to the link (13 libsimple symbols)
  Undefined symbols: 0  (the 7 _libsimple_* are gone)
The link now fails on a DIFFERENT error - ordinary duplicate symbols:
  duplicate symbol '_clock_get_attributes' in:
      .../libsyscall_32.dir/mach/clock-i386-User.c.o
      .../libsyscall_64.dir/mach/clock-x86_64-User.c.o
  ... 267 duplicate symbols, all the i386 vs x86_64 slices of libsyscall
  ld: 267 duplicate symbols for architecture x86_64
  cc: error: linker command failed with exit code 1
These come from the target's own object list carrying BOTH the i386 and the
x86_64 slices of libsyscall; they are not the _libsimple_* symbols and are a
separate issue (the link is not fixed here).

## (2) llvm-nm -gU of the fresh libsystem_kernel
N/A - the link fails before producing the dylib, so there is no file to check.

Reproduce:
  export PATH=/usr/local/bin:/usr/local/sbin:/usr/bin:/bin:/sbin:/usr/sbin
  export DARLING_SRC_DIR=... DARLING_OVERLAY=... DARLING_BUILD_DIR=...
  sh build-freebsd/narrow-build-target.sh src/external/xnu/darling/src/libsystem_kernel/libsystem_kernel.dylib
