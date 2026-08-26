# darling-freebsd

FreeBSD port of Darling — a macOS compatibility layer. Runs real, unmodified
macOS Mach-O x86-64 binaries on FreeBSD 15.1.

No such port exists elsewhere: upstream Darling targets Linux only, and
ravynOS — the other project aiming at macOS binary compatibility on a BSD base
— [moved off FreeBSD to Darwin/XNU in October 2025](https://github.com/ravynsoft/ravynos/discussions/529).
That is worth knowing before filing a bug: almost everything found here is a
first encounter, and there is no upstream to inherit a fix from.

Components:
- **darlingserver** — Mach IPC, task/thread emulation (uses `libepoll-shim`)
- **duct-tape** — XNU-derived Mach code, built separately as `libdtape.a`
- **mldr** — the Mach-O loader; maps and runs guest images in its own address space

## Two interception paths

Worth understanding before reading the code, because the second one is not
obvious and is where most porting bugs live.

1. **macOS syscalls** (class `0x2000000`) → `SIGSYS` handler, which recovers the
   syscall number from the `MOV EAX,imm32` preceding the `SYSCALL` opcode.
2. **Raw Linux-ABI syscalls.** Darling's `libsystem_kernel` is a *Linux* build
   and issues Linux syscalls directly. `SIGSYS` cannot catch these: most low
   Linux numbers collide with real FreeBSD syscalls, so the kernel runs its own
   syscall instead of trapping. mldr therefore rewrites the fixed raw-syscall
   trampolines to `ud2` in every executable image as it is mapped, and services
   them from a `SIGILL` handler. See the header comment in
   `src/startup/mldr/freebsd_syscall_trap.c`.

## Status

- [x] darlingserver, duct-tape and mldr build on FreeBSD 15.1
- [x] Static Mach-O x86-64 loads and runs
- [x] **Dynamic linking** — dyld runs; ~41 guest dylibs map per process
      (Foundation, CoreFoundation, libobjc, libSystem, libdispatch, …)
- [x] macOS BSD syscall ABI intercepted via SIGSYS
- [x] Raw Linux-ABI syscalls intercepted via ud2/SIGILL
- [x] Guest threads (libdispatch workers run; per-thread signal stacks and RPC
      sockets)
- [x] **Real third-party CLI apps run.** Unmodified upstream `sqlite3`:
      `echo "select 21*2;" | sqlite3` → `42`
- [x] **CFPreferences writes.** `defaults write com.bsdos.demo Greeting Hello`
      creates `Library/Preferences/com.bsdos.demo.plist` with the right key
- [ ] `defaults read` — CF returns an empty domain list although the plists
      exist; cause not yet identified
- [ ] Process creation — no `execve`/`posix_spawn`; `fork` does not check in
      with darlingserver. Blocks `NSTask` and anything multi-process
- [ ] GUI — the Cocotron-based AppKit in-tree has never been compiled in this
      port (`COMPONENT_gui` is not invoked by the FreeBSD build scripts)
- [ ] aarch64

## Building on FreeBSD 15.1

```
pkg install cmake ninja llvm git bison flex epoll-shim
```

Build order matters: `libdtape.a` is a prebuilt static library that
`build-darlingserver.sh` links against, so editing anything under
`src/external/darlingserver/duct-tape/` requires rebuilding dtape *first* —
otherwise the change silently does not take effect.

```
sh build-freebsd/build-dtape.sh          # only when duct-tape changed
sh build-freebsd/build-darlingserver.sh
sh build-freebsd/build-mldr-only.sh
sh build-freebsd/build-real-macho-tests.sh   # Foundation + Mach-O test binaries
```

Output goes to `${DARLING_BUILD_DIR:-/var/darling-build}` — the same default
`tests/launch-dynamic-smoke.c` reads from. Overriding it for one script and not
another means the tests run a stale binary.

## Running

```
cc -O0 -ggdb -o /tmp/dynsmoke tests/launch-dynamic-smoke.c

DARLING_TEST_BINARY=hello-foundation-macho /tmp/dynsmoke
echo "select 21*2;" | DARLING_TEST_BINARY=sqlite3-real-macho /tmp/dynsmoke
DARLING_TEST_BINARY=defaults-macho \
  DARLING_TEST_ARGS='write com.bsdos.demo Greeting Hello' /tmp/dynsmoke
```

`DARLING_SMOKE_REFRESH=1` re-stages the local overlay cache after rebuilding a
framework.

### Diagnostics

mldr executes guest images directly in its own address space, so they are not
host linker modules and a debugger resolves no symbols in them. These exist
because of that:

| Variable | Effect |
|---|---|
| `MLDR_LOG_MAPPINGS=1` | log every file-backed mapping as `base+size path` — the address→image map needed to attribute a crash address (feed `base+offset` to `llvm-nm`) |
| `MLDR_LOG_EPOLL=1` | log what the epoll-over-kqueue shim returns to the guest |
| `MLDR_LOG_RPC_SOCKET=1` | log which darlingserver socket each thread uses |
| `MLDR_TRAP_AT=<image>+<hexoff>` | plant a one-shot `ud2` at that address; on hit, report registers, the exception type for an `__cxa_throw` site, the caller, and every stack slot resolving into a known image. Fatal by design |

darlingserver has its own logging: `DSERVER_LOG_STDERR=1 DSERVER_LOG_LEVEL=debug`
(it also writes `<prefix>/private/var/log/dserver.log`). The guest's RPC layer
writes its own diagnostics to `/tmp/dserver-client-rpc.log`.

Do **not** use libkqueue's `KQUEUE_DEBUG=1`: guest debug output travels to the
host through darlingserver's `Kprintf` RPC, and two of those from one thread
trip `Thread::setPendingCall`'s "pending call overwritten while active" throw,
killing darlingserver before whatever is under investigation happens.

## FreeBSD differences from Linux Darling

- No `/proc/task/` — thread IDs via `thr_self()`
- No abstract UNIX sockets, so no autobind. Each RPC socket binds a filesystem
  path `/tmp/darling-mldr-<pid>-<fd>`. The fd matters: darlingserver replies to
  the address a request came from, and a pid-only name gave every thread the
  same address, so replies reached the wrong thread
- `MAP_FIXED_NOREPLACE` = `MAP_FIXED|MAP_EXCL`
- Flag and struct values diverge far more than they appear to. `open` flags,
  `sigaction`/`sigprocmask`, `statfs`, `dirent`, AT_* and signal numbers above
  15 all need translation, not passthrough — several of this port's bugs were
  exactly a passthrough that compiled fine and meant something else
- FreeBSD errno values differ from Linux ones, and the darlingserver↔guest
  protocol carries *Linux* errno: `-ENOSYS` returned by a server built on
  FreeBSD reaches the guest as Linux `EREMCHG`

FreeBSD-specific sources live in `src/startup/` (`freebsd_syscall_trap.c`,
`bsdos_posix_compat.c`); build scripts in `build-freebsd/`.

## Documentation

- `docs/PLAN-roadmap.md` — where this is going and in what order
- `docs/SPEC-appkit-display-backend.md` — the seam for a display backend
- `docs/SPEC-wlstream-frame-source.md` — the screen-streaming pipeline contract
- `docs/SPEC-signal-abi-bridge.md` — signal ABI translation

## License

GPL-3 (see `LICENSE`). Upstream Darling components are LGPLv2.1.
