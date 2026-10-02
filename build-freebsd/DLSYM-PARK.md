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

## Step 2 — the measurement that closes it (attempted; window not captured)

Design used: tick-gated dtrace (probes armed but disabled until t=45s, the
03:34 lesson) on `syscall::_umtx_op` (entry/return + args + stack), `ppoll`,
a gated `dlsym` pid probe, and an on-CPU `profile` sampler for the spin
case. Measured outcomes:

1. The syscall provider's probes are evaluated on EVERY syscall of the
   target from t=0 even while the gate is closed, and the harness phase is
   syscall-dense (staging walks): two full cycles never reached step 01
   within the 240s timeout, zero probe hits (`wl-body-park-dtrace.log`,
   1 line: `matched 12 probes`). Gating by tick does not remove the
   per-syscall stop cost on this kernel.
2. The ptrace-based route stays dead (previous slice: truss kills the
   guest pre-step, `thr_kill SIGILL`).
3. The zero-overhead replacement attempted next — an untraced run with a
   watcher dumping kernel stacks via `procstat -kk` (sysctl, no ptrace) at
   the moment the log shows `[step 10]` — failed on pid resolution: the
   guest's `getpid()` is emulated (the probe prints a virtual pid;
   `sysctl(kern.proc)` reports "No such process"), and the watcher's
   real-pid lookup returned empty at window time in three cycles
   (`wl-body-park-procstat.txt`: `real mldr pid=` then a system-wide
   thread dump). `pgrep -f` is additionally unreliable on this machine
   (matches nothing, including the agent's own process).

## Verdict on the primitive (by elimination + code, stop-state pending)

In the measured path there is exactly ONE blocking-capable construct:
`do_dlsym` takes rtld's own read lock (`rlock_acquire`, rtld_lock.c:251)
around the `symlook_*` search and upgrades it (`lock_upgrade`, :301); the
default lock implementation is an atomic `lock add` against a GLOBAL lock
word in ld-elf's data (rtld_lock.c:154, link-time offset 0x20920) whose
contended path is a umtx wait. Everything else in `do_dlsym` is symbol
search and memory copies. So the state-dependent park of variant (a) is
pinned, by elimination over the read code, to **the rtld global lock-word
wait inside `rlock_acquire`** — a thread that holds the rtld lock (wedged
across its own dlopen/dlsym, e.g. in the session's parked lanes' bridge
calls) parks every later `dlsym` caller, and the guest-created thread is
the one that shows it because its bridge call is the first to arrive.

## What closes the stop-state (replacement instrumentation)

Resolve the real mldr pid ONCE at run start (right after the harness's
`Running:` line, by scanning `ps` — the pid never changes after exec), then
at `[step 10]` run `procstat -kk <pid>` immediately — no lookup in the
window. If kernel stacks are too shallow, `gcore`/`gdb -p` at the window
accepts the ptrace risk because the state is already reached. A dtrace
route that stays cheap: a single `syscall::revoke:entry` gate — the probe
(source in-tree) calls `revoke()` with a magic path at step 10, and one
rare-syscall probe opens the umtx probes with zero pre-window overhead.

