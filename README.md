# darling-freebsd

FreeBSD port of Darling — a macOS compatibility layer for BSD systems.

Runs static Mach-O x86-64 binaries on FreeBSD 15.1 using:
- darlingserver (Mach IPC, thread management)
- mldr (Mach-O loader)
- SIGSYS-based macOS syscall interception (~25 BSD syscalls translated)

## Status

- [x] darlingserver compiles on FreeBSD 15.1
- [x] mldr compiles on FreeBSD 15.1
- [x] Static Mach-O x86-64 loads and runs
- [x] macOS BSD syscall ABI intercepted (write, exit, mmap, sysctl, ...)
- [ ] dyld / dynamic linking
- [ ] aarch64

## Building on FreeBSD 15.1

### Prerequisites

```
pkg install cmake ninja llvm git bison flex epoll-shim
```

### duct-tape (Mach IPC emulation — build first)

```
su -m root -c 'sh build-freebsd/build-dtape.sh 2>&1 | tee /tmp/dtape-build.log'
```

### darlingserver + mldr

```
sh build-freebsd/build-darlingserver.sh
```

Build output goes to `${DARLING_BUILD_DIR:-/var/darling-build}`.

To use a custom build directory:

```
export DARLING_BUILD_DIR=/your/build/dir
sh build-freebsd/build-darlingserver.sh
```

To build against a checkout at a non-default location:

```
export DARLING_SRC_DIR=/path/to/darling-freebsd
sh build-freebsd/build-darlingserver.sh
```

### mldr only (darlingserver already built)

```
sh build-freebsd/build-mldr-only.sh
```

## Running the smoke test

```
cd tests
cc -o /tmp/launch-smoke launch-smoke.c
sudo /tmp/launch-smoke
# Expected: "hello" printed to stdout
```

Or use the shell wrapper:

```
sudo sh tests/run-smoke.sh
```

Both scripts respect `DARLING_BUILD_DIR` and `DARLING_SRC_DIR`.

## Architecture

FreeBSD differences from Linux Darling:

- No `/proc/task/` — thread IDs via kqueue/`thr_self()`
- No abstract UNIX sockets — filesystem socket `/tmp/darling-mldr-<pid>`
- No seccomp/ptrace on live process — SIGSYS handler recovers macOS syscall
  number by scanning `MOV EAX,imm32` bytes preceding `SYSCALL` opcode
- `MAP_FIXED_NOREPLACE` = `MAP_FIXED|MAP_EXCL` on FreeBSD — WSL1 hack bypassed

FreeBSD-specific sources live in `src/startup/` (`bsdos_posix_compat.c`,
`freebsd_syscall_trap.c`) and build scripts in `build-freebsd/`.

## License

GPL-3 (see `LICENSE`). Upstream Darling components are LGPLv2.1.
