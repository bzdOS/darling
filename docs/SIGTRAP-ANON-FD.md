# Control #153 step 31 — MAP_ANON mmap forwards fd=-1: the 32 GiB reservation now succeeds

**Date:** 2026-10-09
**Branch:** task/anon-fd
**Base:** pr-arm64 = 160ffeceb (step 30 merged)
**Method:** one pristine run of the known scenario (`docs/SIGTRAP-RIP.md`, no
`MLDR_TRAP_RESUME_AT`) under the step-30 dtrace `syscall::mmap` probe,
`timeout -k 5 300`; mldr rebuilt. Trace log:
`$DARLING_SRC_DIR/../diag/anon-fd.dtrace`; run log:
`$DARLING_SRC_DIR/../diag/anon-fd-run.log`.

---

## 0. Result

- **The 32 GiB reservation now succeeds.** The trace shows
  `mmap(addr=0x13800000000, len=0x800000000, prot=0, flags=0x1002, fd=-1, off=0)`
  → `ret=0xb3c000 errno=0`. The 16 GiB reservation (`len=0x400000000`) and every
  other large pool mapping also succeed, all with `fd=-1`, `errno=0`.
- **The death point moved far past the reservation.** The initializer that used
  to crash (`Chrome+0x212A4C0`, PartitionAlloc) now runs to completion, the next
  initializer (`Chrome+0x14D300`) runs, and the run log ends at a `dlopen` path
  probe of the Chrome framework (§3).
- **The fix had to go in the Linux-ABI path, not the macOS path** the dispatch
  named. Step 30 assumed the guest's `mmap` is a macOS BSD syscall (case
  `MACOS_SYS_mmap`). It is not: the guest's `libsystem_kernel._mmap` →
  `___mmap` → `__darling_bsd_syscall` looks up `___bsd_syscall_table[197]`,
  which is `_sys_mmap`, and `_sys_mmap` emits **Linux syscall 9**
  (`movl $0x9, (%rsp); callq _linux_syscall`). So the guest's `mmap` arrives at
  mldr as a raw Linux-ABI syscall and is handled by `dispatch_linux_syscall`
  (case `LINUX_SYS_mmap`, `freebsd_syscall_trap.c:1707`). Both cases are fixed
  (the macOS one as instructed, the Linux one because it is the live path).

## 1. The change (`src/startup/mldr/freebsd_syscall_trap.c`)

```
     case MACOS_SYS_mmap:
-        /* macOS mmap args: addr, len, prot, flags, fd, offset — same layout */
-        return freebsd_raw_syscall(SYS_mmap, a1, a2, a3, a4, a5, a6);
+        /* macOS mmap args: addr, len, prot, flags, fd, offset — same layout.
+         * Darwin ignores fd under MAP_ANON; FreeBSD returns EINVAL for a
+         * non-(-1) fd there, so anonymous mappings forward fd=-1. */
+        return freebsd_raw_syscall(SYS_mmap, a1, a2, a3, a4,
+                                   (a4 & MAP_ANON) ? -1 : a5, a6);
```
```
         if (a4 & 0x20)    freebsd_flags |= MAP_ANON;     /* MAP_ANONYMOUS */
         if (a4 & 0x20000) freebsd_flags |= MAP_STACK;
-        long mr = freebsd_raw_syscall(SYS_mmap, a1, a2, a3, freebsd_flags, a5, a6);
+        /* Darwin ignores fd under MAP_ANON; FreeBSD rejects a non-(-1) fd
+         * with EINVAL, so anonymous mappings forward fd=-1. */
+        long mr = freebsd_raw_syscall(SYS_mmap, a1, a2, a3, freebsd_flags,
+                                      (a4 & 0x20) ? -1 : a5, a6);
```

`offset` is untouched in both. The macOS case is the dispatch's request; the
Linux case is the one that fires for this guest (the Linux flags bit for
`MAP_ANONYMOUS` is `0x20`, translated to FreeBSD `MAP_ANON 0x1000` just above).

## 2. The trace — the 32 GiB reservation succeeds

```
MMAP-ENTRY pid=21815 exec=mldr len=0x800000000 addr=0x13800000000 prot=0 flags=0x1002 fd=-1 off=0
MMAP-RET   pid=21815 exec=mldr len=0x800000000 ret=0xb3c000 errno=0
...
MMAP-ENTRY pid=21815 exec=mldr len=0x400000000 addr=0x11309692000 prot=0 flags=0x1002 fd=-1 off=0
MMAP-RET   pid=21815 exec=mldr len=0x400000000 ret=0xa504000 errno=0
```

`fd=-1` (the fix) and `errno=0` (success) for both reservations. Step 30's
`errno=22 (EINVAL)` is gone.

## 3. The new stop point

The run log ends (`anon-fd-run.log`, last lines) with the guest now running the
initializer that previously died and the next one, then a `dlopen` path probe:

```
dyld: calling initializer function 0xbe81cf214c0 in .../Google Chrome for Testing Framework   <- the old crash point (Chrome+0x212A4C0)
dyld: calling initializer function 0xbe81ce5c300 in .../Google Chrome for Testing Framework   <- the next initializer (Chrome+0x14D300)
  dlopen_internal(/Frameworks/.../Google Chrome for Testing Framework) ==> 0x7fc80b777ff1
[0] /Frameworks/.../Google Chrome for Testing Framework
  stat=1  dlopen=OK
[1] /tmp/Frameworks/...   dlopen=...: image not found
[2] //../Frameworks/...   dlopen=...: image not found
[3] /FWMACHO             dlopen=...: image not found
```

The previously-fatal SIGTRAP is gone; the process no longer dies on the
reservation. It reaches the Chrome framework's `dlopen`/initializer phase and
the log ends there (the framework itself dlopens OK at path `[0]`; the three
fallback paths report `image not found`). `mldr` then exited (no
`exited on signal` line in `dmesg` for this run's pid — unlike steps 30/31a,
which logged `exited on signal 5`); the leaked `darlingserver` was killed by
pid after the run.

## 4. Repro

```sh
# rebuild mldr with the change
sh build-freebsd/build-mldr-only.sh        # installs ${DARLING_BUILD_DIR}/dserver/mldr-real/mldr

# run under the mmap probe
sudo dtrace -n 'syscall::mmap:entry /arg1 >= 0x400000000/ { self->len=arg1;
  printf("ENTRY len=%#lx addr=%#lx flags=%#x fd=%d\n", arg1,arg0,arg3,arg4); }
syscall::mmap:return /self->len/ { printf("RET len=%#lx ret=%#lx errno=%d\n",
  self->len,arg0,errno); self->len=0; }' \
  -c 'timeout -k 5 300 env DARLING_SRC_DIR=$DARLING_SRC_DIR DARLING_OVERLAY=$DARLING_OVERLAY \
      DARLING_BUILD_DIR=$DARLING_BUILD_DIR DARLING_TEST_BINARY=cft-fwmacho-probe-macho \
      DARLING_STAGING_TREES="$(cat /tmp/stub76-staging.txt)" DARLING_SMOKE_REFRESH=1 \
      $DARLING_BUILD_DIR/launch-dynamic' > diag/anon-fd.dtrace 2>&1
grep 'len=0x800000000' diag/anon-fd.dtrace   # ret=... errno=0

# why the Linux path: the guest's mmap reaches mldr as Linux syscall 9
llvm-objdump -d --start-address=0x25f38 --stop-address=0x25f58 \
  "$DARLING_OVERLAY/usr/lib/system/libsystem_kernel.dylib"   # ___mmap: movl $0xc5,%eax
llvm-objdump -d --start-address=0x35fd0 --stop-address=0x36010 \
  "$DARLING_OVERLAY/usr/lib/system/libsystem_kernel.dylib"   # _sys_mmap: movl $0x9,(%rsp); call _linux_syscall
```
