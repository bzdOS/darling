# dlsym park: the primitive inside the host dlsym path

Question: variant (a)'s parked lane blocks inside the per-call
`__lazy` -> `elfcalls->dlsym_fatal` -> host `dlsym` chain (previous
slice). Which primitive inside `dlsym` takes the state-dependent block?

## Step 1 — the path, read from the rtld build (no tracing)

The dynamic linker is unstripped enough to map the whole path by address
(`/usr/lib/debug/libexec/ld-elf.so.1.debug` + `addr2line`); the source
tree it was built from is `/usr/src/libexec/rtld-elf` on the build host.

`dlsym` (0xc650) tail-jumps into `do_dlsym` (0xc660), whose callees are:

| callee            | site                              |
|-------------------|-----------------------------------|
| `rlock_acquire`   | rtld_lock.c:251                   |
| `symlook_global`  | rtld.c:4771                       |
| `symlook_default` | rtld.c:4815                       |
| `symlook_list`    | rtld.c:4879                       |
| `symlook_obj`     | rtld.c:4975                       |
| `lock_upgrade`    | rtld_lock.c:301                   |
| `lock_release`    | rtld_lock.c:281                   |
| `sigsetjmp`       | (search error path)               |
| `_rtld_error`     | rtld.c:1113                       |

So `do_dlsym` performs the symbol search under rtld's own read lock and
upgrades it around the hit path — the block, if the park is a lock wait,
is in the `rtld_lock.c` family. `def_lock_acquire` (rtld_lock.c:154)
disassembles to an atomic `lock addl` against a GLOBAL lock word inside
ld-elf's data segment (link-time offset 0x20920), with the contended path
going to a umtx wait — i.e. the candidate primitives, in order:

1. **the rtld global lock word (bind/globals lock family)** — a thread
   that holds it (across its own dlsym/dlopen, or wedged while holding)
   parks every later `dlsym` caller in `rlock_acquire`'s wait;
2. the `dlerror` TLS machinery (`def_dlerror_loc`, rtld_lock.c-adjacent
   rtld_libc TLS accessors) — only if the wait is not the lock;
3. libthr bookkeeping for the DARLING-created thread — only if neither.

## Step 2 — the measurement that closes it

`dtrace` with the pid/syscall providers, TIME-GATED by tick so the hot
`dlsym` path is never probed globally (the 03:34 lesson), watching for the
parked tid's stop state:

- `syscall::_umtx_op:entry` gated on window + pid: prints tid, `op`,
  `uaddr` — a park inside the rtld lock shows the uaddr equal to the lock
  word's runtime address (rtld base from the run's
  `ld-elf.so.1 is initialized, base address = ...` line + 0x20920 +
  per-lock offset), with the caller stack naming `rlock_acquire`;
- `syscall::_ppoll:entry/return` for the read-path control;
- a `profile` sampling probe for the spin case (a lock word that spins
  without umtx never syscalls).
