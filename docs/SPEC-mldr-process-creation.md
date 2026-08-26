# mldr process creation (fork/vfork/execve/posix_spawn) — spec

Status: new files written (`src/startup/mldr/process_spawn.h`,
`src/startup/mldr/process_spawn.c`). Nothing existing was edited — the
caller still needs to wire these in (see "Where to wire this in" below)
and build/test on the dev VM (185). No build was run in this session.

## Why raw `fork()` alone was wrong

Today `freebsd_syscall_trap.c:549-550` does exactly this for
`MACOS_SYS_fork`:

```c
case MACOS_SYS_fork:
    return freebsd_raw_syscall(SYS_fork, 0, 0, 0, 0, 0, 0);
```

darlingserver identifies a process by the real PID it reads off the RPC
socket's credentials, and lazily creates a `Process` record for any PID it
hasn't seen yet on the first call that PID makes
(`darlingserver/src/call.cpp:78`, `processRegistry().registerIfAbsent`).
So a fork()ed child was never going to be *invisible forever* — but:

1. Until the child makes some RPC call, and for that first call, it's
   using the exact same UNIX-domain socket fd the parent still holds —
   `fork()` duplicates fd *table entries*, not the underlying socket
   object. Two processes reading/writing one socket race, and
   darlingserver cannot tell which process a given inbound message is
   really from via the socket alone. This project already hit and fixed
   the equivalent bug for two *threads* sharing one socket — see
   `elfcalls/threads.c:446-470`'s comment on `__darling_thread_rpc_socket()`.
   Fork introduces the same failure mode at the process level.
2. `darlingserver::Process::notifyCheckin()`
   (`darlingserver/src/process.cpp:410-505`) is what actually wakes a
   parent blocked on a macOS fork-wait primitive (`waitForChildAfterFork()`,
   `process.cpp:527`) and fires the `NOTE_FORK` kqueue event
   (`process.cpp:500-503`). A process that never calls checkin never
   triggers either.
3. mldr's own bookkeeping globals (`__dserver_main_thread_socket_fd`,
   `__dserver_process_lifetime_pipe_fd`, `mldr_load_results.kernfd`) are
   plain C globals; after `fork()` the child's copies still name the
   parent's kernel objects until something closes and replaces them.
   Nothing about calling libc/raw `fork()` does that.

**The `is_fork` field is a red herring.** `dserver_rpc_checkin(bool is_fork,
...)` (`generated-rpc/include/darlingserver/rpc.h:280`) looks like the
mechanism that tells darlingserver "this is a fork, not a fresh start" —
but `DarlingServer::Call::Checkin::processCall()`
(`darlingserver/src/call.cpp:311-329`) never reads `_body.is_fork` at all.
The fork-vs-exec distinction darlingserver actually acts on is
**server-side state**: `Process::_pendingReplacement`, set by
`setPendingReplacement()` from `Checkout::processCall()`
(`call.cpp:377`, only reached when a *previous* `checkout` call had
`executing_macho = true` and its exec pipe later got closed by a
successful `execve()`) — see `Process::notifyCheckin()`
(`process.cpp:410-505`), which branches on `_pendingReplacement`, not on
anything the checkin call itself carries.

So: `mldr.c:1001`'s existing `dserver_rpc_checkin(false, ...)` — called
unconditionally from `setup_space()`, for both a genuinely fresh process
*and* a post-`execve()` mldr re-invocation — is actually **correct** as
written, precisely *because* `is_fork` does nothing server-side yet. What
was missing was a **second call site**: nothing called checkin with
`is_fork=true` for the fork case at all, because raw `fork()` never called
checkin in the first place. `is_fork` is threaded through in the new code
(`process_spawn.c`'s `dserver_rpc_explicit_checkin(new_kernfd, true, ...)`)
for protocol-shape correctness and in case a future darlingserver revision
starts reading it, not because today's server needs the value to be right.

## What a forked process must do to be recognized (summary)

Implemented in `psp_fork_and_register()` (`process_spawn.c`), shared by
`mldr_sys_fork()`/`mldr_sys_vfork()` and the fork+exec path of
`mldr_sys_posix_spawn()`:

1. `fork(2)` (raw syscall, unchanged from today).
2. In the child: close the **inherited** RPC socket and process-lifetime
   pipe fds (they still point at the parent's kernel objects).
3. Create a fresh RPC socket (`__mldr_create_rpc_socket()`, existing
   function) and a fresh process-lifetime pipe
   (`__mldr_create_process_lifetime_pipe()`, existing function).
4. `dserver_rpc_explicit_checkin(new_socket, /*is_fork=*/true,
   &stack_hint, lifetime_pipe_read_end)`.
5. Update the process-global mirrors (`__dserver_main_thread_socket_fd`,
   `__dserver_process_lifetime_pipe_fd`, `mldr_load_results.kernfd`) so
   every *later* RPC call from this process (Mach traps, further
   fork/exec, ...) uses the new socket.

If step 3 or 4 fails, the child `_exit(127)`s rather than continuing
unregistered — matches Linux Darling's own `sys_fork()`
(`xnu_syscall/bsd/impl/process/fork.c:59-63`, which `__simple_abort()`s in
the equivalent spot).

## execve: why it re-execs *mldr itself* for Mach-O targets

Same principle as fork, different mechanism: `mldr_sys_execve()`
distinguishes a Mach-O/FAT target (magic-byte sniff,
`psp_path_is_macho()`) from everything else FreeBSD's own kernel can run
natively (ELF, `#!` scripts — FreeBSD's `imgact_shell` already handles
those, unlike the Linux reference which hand-parses shebangs itself
because Linux's binfmt needs help there too for this project's purposes).

- **Non-Mach-O target:** plain `execve(2)` of the resolved path, with
  `dserver_rpc_explicit_checkout(socket, pipe_read_end,
  executing_macho=false)` before it. A successful exec here means this
  process has left Darling's world entirely — darlingserver's own
  `Checkout::processCall()` treats `executing_macho=false` + EOF-on-pipe
  as process death (`call.cpp:378-382`, `process->notifyDead()`), which is
  correct: it's no longer a Mach-O binary mldr is running.
- **Mach-O/FAT target:** argv[0] is rewritten to `"<mldr_path>!<target>"`
  and `execve(2)`'d against **mldr's own binary** — the exact convention
  `mldr.c`'s own `main()` already parses at startup
  (`mldr.c:148-166`, `strchr(argv[0], '!')`), pre-existing and unrelated to
  this change. `dserver_rpc_explicit_checkout(..., executing_macho=true)`
  is sent first. When darlingserver sees that pipe close (execve
  succeeded), it calls `setPendingReplacement()`
  (`call.cpp:377`) — which is what makes the *next* checkin
  (the freshly-re-exec'd mldr's own `setup_space()` →
  `dserver_rpc_checkin(false, ...)` at `mldr.c:1001`, completely unchanged
  by this patch) get treated as "this process was replaced" instead of "a
  new process" in `Process::notifyCheckin()` (`process.cpp:415-489`, the
  `didExec` branch: keeps the process identity, tears down and rebuilds
  the duct-tape task/thread, fires `NOTE_EXEC` instead of `NOTE_FORK`).

`mldr's own binary path` is fetched via `dserver_rpc_explicit_mldr_path()`
— the same call the Linux `execve.c` reference uses
(`xnu_syscall/bsd/impl/process/execve.c:49`), and it's OS-agnostic (server
just remembers a configured path), so no FreeBSD-specific work was needed
there.

## Where to wire this in (NOT done in this change)

1. **Build:** add `process_spawn.c` to `mldr_sources` in
   `src/startup/mldr/CMakeLists.txt:19-25` (alongside `mldr.c`,
   `freebsd_syscall_trap.c`, etc.). Without this the new file is not part
   of the `mldr` executable at all.

2. **`freebsd_syscall_trap.c`:**
   - Add `#include "process_spawn.h"` near the other local includes
     (top of the file, after the `#ifdef DARLING_FREEBSD` block that
     starts around line 98).
   - Add syscall-number constants next to the existing
     `MACOS_SYS_fork` (line 154) in the `MACOS_SYS_*` block
     (`freebsd_syscall_trap.c:152-189`ish):
     ```c
     #define MACOS_SYS_execve        59
     #define MACOS_SYS_vfork         66
     #define MACOS_SYS_posix_spawn  244
     ```
     (values taken from `Developer/.../MacOSX.sdk/usr/include/sys/syscall.h`,
     same source the rest of that block already cites).
   - In `dispatch_macos_bsd_syscall()`
     (`freebsd_syscall_trap.c:537-...`), in the "process lifecycle"
     section right where `MACOS_SYS_fork` already lives (line 549-550):
     ```c
     case MACOS_SYS_fork:
         return mldr_sys_fork();   /* was: freebsd_raw_syscall(SYS_fork, ...) */

     case MACOS_SYS_vfork:
         return mldr_sys_vfork();

     case MACOS_SYS_execve:
         return mldr_sys_execve((const char *)a1,
                                 (char *const *)a2,
                                 (char *const *)a3);

     case MACOS_SYS_posix_spawn:
         return mldr_sys_posix_spawn((pid_t *)a1,
                                      (const char *)a2,
                                      (const struct mldr_posix_spawn_args_desc *)a3,
                                      (char *const *)a4,
                                      (char *const *)a5);
     ```
     `a1..a6` are the already-recovered macOS syscall arguments
     `dispatch_macos_bsd_syscall()` receives — no new argument-recovery
     work needed, this is exactly the same pattern every other case in
     that function already uses.

## What I could NOT verify (be skeptical of these until checked on 185)

1. **Nothing here was built or run.** Per this task's instructions, no
   `cc`/`cmake`/`make` was invoked. Type sizes, struct layouts, and even
   whether this compiles cleanly against the actual `darlingserver/rpc.h`
   and `loader.h` on 185 are unverified.

2. **The `mldr_posix_spawnattr`/`mldr_psfa_action`/
   `mldr_posix_spawn_args_desc` structs in `process_spawn.h` are a hand-
   written mirror** of `src/external/xnu/bsd/sys/spawn_internal.h`'s
   `_posix_spawnattr`/`_psfa_action`/`_posix_spawn_args_desc`, not that
   header `#include`d directly (see the struct-level comment in
   `process_spawn.h` for why: unverified whether that header's own
   dependency chain — `mach/coalition.h`, `mach/task_policy.h`,
   `os/overflow.h`, ... — builds cleanly in mldr's own FreeBSD-native
   compilation unit). If field order, types, or padding drift from the
   real one, `posix_spawn()` will silently misparse its arguments. Worth
   adding a `static_assert(sizeof(...) == ...)` / `offsetof` check against
   the real header once it's confirmed to build, or switching to
   `#include`-ing the real header if it turns out to Just Work.

3. **The biggest open risk: guest fsbase during the SIGSYS handler.**
   `dispatch_macos_bsd_syscall()` runs inside `sigsys_handler()`
   (`freebsd_syscall_trap.c:2597`), which does not save/restore
   `%fs`/fsbase. mldr rewrites the guest's fsbase via the emulated
   `arch_prctl(ARCH_SET_FS)` (see that file's own comment at
   `freebsd_syscall_trap.c:2385-2411`), so by the time a real macOS
   process is far enough into its bootstrap to call `fork()`/
   `posix_spawn()`, `%fs` during the handler is very likely the **guest's**
   TLS base, not mldr's own.
   - This code was written to never touch a `__thread`-qualified variable
     for exactly this reason (uses `dserver_rpc_explicit_*()` +
     `__dserver_main_thread_socket_fd` throughout, never
     `__darling_thread_rpc_socket()`/`t_server_socket`
     (`elfcalls/threads.c:54,435-487`) — see `process_spawn.c`'s own file
     header for the full reasoning).
   - **Not resolved:** `__mldr_create_rpc_socket()` (`mldr.c:770`) calls
     `socket_bitmap_get()`, which takes a `pthread_mutex_t` and may
     `realloc()`. If FreeBSD's libthr resolves the calling thread (mutex
     fast path, `errno`) via a TCB reached through `%fs` on amd64 — which
     is my understanding of typical libthr amd64 implementations, but
     **not verified against this repo/host's actual libthr**, no libthr
     source is vendored here to check — then this call is *also* suspect
     under guest fsbase, independent of the `__thread` issue. Used anyway
     because it's the only existing, tested path that produces a
     correctly bound-and-registered socket, and because the SIGSYS trap
     here is synchronous (raised by the very `fork`/`execve`/
     `posix_spawn` instruction the calling thread itself executed, not an
     asynchronously-delivered signal — see `freebsd_syscall_trap.c`'s own
     "Background" comment), which rules out same-thread mutex
     self-reentrancy as a cause. **Recommended real fix (out of scope
     here):** have the SIGSYS handler save the live fsbase on entry,
     restore mldr's own fsbase for the duration of dispatch, and restore
     the guest's fsbase immediately before `sigreturn`. This would also
     retroactively make the pre-existing Mach-trap RPC calls
     (`task_self_trap`/`host_self_trap`/`thread_self_trap`/
     `mach_reply_port`/`mach_msg`, `freebsd_syscall_trap.c:699-770`) safe
     against the same class of bug — they have the identical exposure
     today and are unrelated to this change.
   - **Also unverified:** whether `execve()`/`posix_spawn()` called from a
     guest thread that is *not* the process's actual main thread works
     correctly here. This implementation always addresses
     `__dserver_main_thread_socket_fd` directly (the only TLS-free option
     available) — correct by construction for fork/vfork (only the
     calling thread survives), but an assumption for execve/posix_spawn
     from a secondary thread. Not exercised in this session.

4. **`psp_apply_sigmask()`'s FreeBSD `sigset_t` layout and
   `SIG_SETMASK == 3` assumption** (`process_spawn.c`) — no FreeBSD system
   headers were available to check against in this sandbox. Darwin↔FreeBSD
   signal *numbers* 1..31 were cross-checked against this repo's vendored
   Darwin header and confirmed identical; the FreeBSD-side `sigset_t` wire
   struct and the `SIG_SETMASK` value were not.

5. **Syscall numbers used throughout `process_spawn.c`**
   (`SYS_pipe2`, `SYS_dup2`, `SYS_chdir`, `SYS_fchdir`, `SYS_setuid`,
   `SYS_setgid`, `SYS_setpgid`, `SYS_sigprocmask`, `SYS_execve`,
   `SYS_vfork` is NOT used — see below) are the standard FreeBSD
   `<sys/syscall.h>` names, consistent with how the rest of
   `freebsd_syscall_trap.c` already uses `SYS_*` constants, but — same as
   that file's own `#198` disclaimer — not dynamically verified with a
   live build+truss pass in this session.

6. **`MACOS_SYS_vfork` (66) is intentionally implemented identically to
   `mldr_sys_fork()`**, not a real vfork — see the contract comment in
   `process_spawn.h`. This is a deliberate simplification (POSIX-legal,
   loses only the copy-avoidance performance benefit), not a gap, but
   flagging it since "vfork" in the name might suggest otherwise.

7. **Overlay path resolution (`psp_resolve_overlay_path()`) does no `../`
   canonicalization** — matches `loader.c:355-370`'s existing behavior for
   `LC_LOAD_DYLINKER` (same simple prefix-concatenation, no such check
   there either), but a guest-controlled exec target escaping the overlay
   root is more consequential than a dylib load path doing so. Not fixed
   here (would need a real path canonicalizer, out of scope), just
   inherited unchanged from the existing pattern.

8. **Every `fork()` leaks one `/tmp/darling-mldr-<pid>-<fd>` socket file.**
   `psp_fork_and_register()` closes the child's inherited RPC socket via
   the existing `__mldr_close_rpc_socket()` (`mldr.c:855-868`) rather than
   a raw `close()`, to keep the child's own (fork-copied)
   `socket_bitmap` accounting consistent. But that function's FreeBSD
   unlink path is built from `getpid()` **at close time**
   (`mldr.c:865`), while the socket being closed was bound under the
   **parent's** pid (`mldr.c:821-822`, also `getpid()`-based, at bind
   time). By the time the child calls it, `getpid()` already returns the
   child's own different pid, so the unlink targets a path that was never
   bound — the parent's actual socket file is left behind in `/tmp`
   forever. Traded deliberately against the alternative (a raw `close()`,
   which leaves a stale "reserved" bit in the child's bitmap that a later
   `__mldr_create_rpc_socket()` call in that same child could hand out a
   second time — a functional collision risk, judged worse than a leaked
   tmp file). Not fixed here; see the comment at the call site in
   `process_spawn.c`'s `psp_fork_and_register()` for the exact reasoning
   and what a real fix would need (either the unlink needs the *binder's*
   pid threaded through, or the naming scheme needs to stop embedding pid
   at all).

9. **`posix_spawn`'s `POSIX_SPAWN_CLOEXEC_DEFAULT` and `SETSIGDEF` flags,
   the `MLDR_PSFA_FILEPORT_DUP2` file action, and any non-NULL
   `port_actions`/`mac_extensions`/`coal_info`/`persona_info`/
   `posix_cred_info`/`subsystem_root_path`** are explicitly rejected
   (`-ENOTSUP`) rather than implemented — see the contract comments in
   `process_spawn.h` and `process_spawn.c` for the reasoning behind each.
   None of these are known to be required for `NSTask`'s common path;
   if something later needs one, treat the `-ENOTSUP` as the signal to
   come back and implement it rather than a mistake to work around.
