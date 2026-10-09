# Control #152 step 30 — errno of the failing reservation: **EINVAL (22)**, caused by `fd`

**Date:** 2026-10-09
**Branch:** task/resv-errno
**Base:** pr-arm64 = 039059f857 (step 29 merged)
**Method:** one pristine run of the known scenario (`sigexc-rip-2`, no
`MLDR_TRAP_RESUME_AT`), tracing `mmap` with **dtrace** (`syscall::mmap`), plus a
3-line host microtest to isolate the cause. Trace log:
`$DARLING_SRC_DIR/../diag/resv-errno.dtrace`; run log:
`$DARLING_SRC_DIR/../diag/resv-errno-run.log`.

---

## 0. Result

- **Reservation 1 (32 GiB, `Chrome+0x1863281`):** the guest `mmap` is
  `mmap(addr=0x11800000000, len=0x800000000, prot=0, flags=0x1002, fd=0xfd000000, off=0)`
  → **`MAP_FAILED`, `errno = 22 (EINVAL)`**.
- **Reservation 2 (16 GiB, `Chrome+0x186331D`): never reached.** The pool init
  crashes on the first reservation's failure, so no `mmap` with `len=0x400000000`
  appears in the trace at all.
- **Cause (measured): the `fd` argument.** FreeBSD rejects a non-`-1` `fd` with
  `MAP_ANON`; Darwin ignores `fd` when `MAP_ANON` is set. mldr's macOS mmap path
  forwards `fd` unchanged, so FreeBSD sees the wrapper's packed `fd = 0xfd000000`
  and returns `EINVAL`. The 32 GiB size and the ~1 TB hint are **not** the cause
  (step 29's overcommit hypothesis is disproven).

## 1. The run and the trace

Scenario: `docs/SIGTRAP-RIP.md` repro (pristine — no `MLDR_TRAP_RESUME_AT`),
wrapped `timeout -k 5 300`, run under `sudo dtrace -c`. `ktrace`/`truss` were
rejected in favour of dtrace: `truss -e` shows **execve environment** strings, not
a syscall filter, and `ktrace -t c` would record every syscall of a process that
forwards every guest syscall — dtrace's `syscall::mmap` probe is far cheaper.

Probe (matched 2 probes; only large mappings logged):

```
syscall::mmap:entry  /arg1 >= 0x400000000/ { self->len = arg1; printf(...) }
syscall::mmap:return /self->len/           { printf(... errno ...); self->len = 0; }
```

The trace, in full for the reservation (`exec=mldr`, the only relevant lines):

```
MMAP-ENTRY pid=37833 exec=mldr len=0x800000000 addr=0x11800000000 prot=0 flags=0x1002 fd=-50331648 off=0
MMAP-RET   pid=37833 exec=mldr len=0x800000000 ret=0xffffffffffffffff errno=22
MMAP-ENTRY pid=37833 exec=mldr len=0x800000000 addr=0x11800000000 prot=0 flags=0x1002 fd=-50331648 off=0
MMAP-RET   pid=37833 exec=mldr len=0x800000000 ret=0xffffffffffffffff errno=22
```

`fd=-50331648` is `0xfd000000` as a signed int. The reservation is attempted
**twice**: the wrapper `Chrome+0x16E3F60` calls the raw `mmap` at
`Chrome+0x16E4240`, which returns `0` on `MAP_FAILED`; the wrapper's fallback
path (`Chrome+0x16E41CD`) then calls `0x16E4240` again — the same request, the
same `EINVAL` — and finally returns `0` to the caller, which traps.

The run reached the trap (`SIGTRAP-AUDIT-DELIVER`, rip `…c77c1`) and the run log
froze at 1 292 544 lines; `timeout` then reaped the wrapper (exit 124). No
`darlingserver` was left alive afterwards.

## 2. Isolating the cause — host microtest

The guest request was replayed directly on the host (a 3-line C program, also
captured by the still-running dtrace probe):

```
A addr=0x11800000000 fd=0xfd000000 : p=0xffffffffffffffff errno=22   <- guest's exact args
B addr=NULL          fd=-1         : p=0x822d31000       errno=0
C addr=NULL          fd=0xfd000000 : p=0xffffffffffffffff errno=22
D addr=0x11800000000 fd=-1         : p=0x1180083f000     errno=0
```

`fd` is the sole discriminator: with `fd = -1` the same 32 GiB `PROT_NONE`
`MAP_ANON` mapping **succeeds** (with or without the hint); with
`fd = 0xfd000000` it fails `EINVAL` regardless of the hint. So:

- size 32 GiB → fine; ~1 TB hint → fine; `PROT_NONE` → fine; `MAP_PRIVATE|MAP_ANON` → fine;
- **non-`-1` `fd` with `MAP_ANON` → `EINVAL` on FreeBSD.**

## 3. Where this lands in the tree

The Darwin→FreeBSD mmap difference is not handled:

```
src/startup/mldr/freebsd_syscall_trap.c
  641:     case MACOS_SYS_mmap:
  642:         /* macOS mmap args: addr, len, prot, flags, fd, offset — same layout */
  643:         return freebsd_raw_syscall(SYS_mmap, a1, a2, a3, a4, a5, a6);
```

The macOS branch forwards `fd` (and `prot`/`flags`) unchanged, unlike the Linux
branch (`:1704-1718`) which remaps flags. On Darwin `fd` is ignored when
`MAP_ANON` is set; on FreeBSD it is not, so a `MAP_ANON` mapping carrying a
non-`-1` `fd` is rejected `EINVAL` here. This is the located owner of the
failure: **`src/startup/mldr/freebsd_syscall_trap.c:641-643`** (the untranslated
forward), returning via **`freebsd_raw_syscall`** (`:412`). No fix is proposed
here — that is the head's call.

## 4. Honest limits

- Only the 32 GiB reservation appears; the 16 GiB one is genuinely never issued
  (the process dies on the first failure), so its errno cannot be named — it has
  no `mmap` in the trace. This is a negative by construction, not a missing search.
- The wrapper packs its 6th argument (`0xfd`) into the `fd` slot; *why* Chrome
  does so (a PartitionAlloc tag in that slot) is not resolved here — the measured
  fact is the value FreeBSD received and rejected.
- The microtest is a host-side replay of the guest's exact arguments, not the
  guest's own code path; it isolates the argument that causes `EINVAL`.

## Repro

```sh
# 1. the run under a dtrace mmap probe (cheaper than ktrace/truss here)
sudo dtrace -n 'syscall::mmap:entry /arg1 >= 0x400000000/ { self->len=arg1;
  printf("ENTRY len=%#lx addr=%#lx prot=%#x flags=%#x fd=%d\n", arg1,arg0,arg2,arg3,arg4); }
syscall::mmap:return /self->len/ { printf("RET len=%#lx ret=%#lx errno=%d\n",
  self->len,arg0,errno); self->len=0; }' \
  -c 'timeout -k 5 300 env DARLING_SRC_DIR=$DARLING_SRC_DIR DARLING_OVERLAY=$DARLING_OVERLAY \
      DARLING_BUILD_DIR=$DARLING_BUILD_DIR DARLING_TEST_BINARY=cft-fwmacho-probe-macho \
      DARLING_STAGING_TREES="$(cat /tmp/stub76-staging.txt)" DARLING_SMOKE_REFRESH=1 \
      $DARLING_BUILD_DIR/launch-dynamic' > diag/resv-errno.dtrace 2>&1
grep 'len=0x800000000' diag/resv-errno.dtrace   # errno=22

# 2. isolate the cause (fd vs size/hint)
printf '#include <sys/mman.h>\n#include <stdio.h>\n#include <errno.h>\nint main(void){void*p;errno=0;p=mmap((void*)0x11800000000UL,0x800000000UL,0,MAP_PRIVATE|MAP_ANON,0xfd000000,0);printf("fd=0xfd000000 errno=%%d\\n",errno);errno=0;p=mmap((void*)0x11800000000UL,0x800000000UL,0,MAP_PRIVATE|MAP_ANON,-1,0);printf("fd=-1 errno=%%d p=%%p\\n",errno,p);return 0;}\n' > /tmp/mm.c
cc -O0 -o /tmp/mm /tmp/mm.c && /tmp/mm   # 22 then 0
```
