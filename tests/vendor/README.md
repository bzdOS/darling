# tests/vendor/

## macosx-sdk-flat.tar.gz

A flattened copy of `Developer/Platforms/MacOSX.platform/Developer/SDKs/MacOSX.sdk/usr/include`
(every symlink replaced with a real copy of its target), used as an `-I`
path when cross-compiling real macOS C source with clang on the FreeBSD dev
VM.

**Why flattened, not used in place:** that SDK tree is almost entirely
symlinks pointing back into other `src/external/*` submodules (e.g.
`usr/include/stdio.h -> ../../../../../../../../src/external/libc/include/stdio.h`).
`readlink()` on the dev VM's virtiofs mount (`/path/to/workspace`) is broken — the
host's virtiofsd returns a malformed FUSE_READLINK reply, and every attempt
returns `EIO` (see the project's `dev-vm-virtiofs-fix` memory / `docs/DEV-VM.md`).
Regular file reads work fine over the same mount; only symlink resolution is
broken. Since the *host* filesystem has no such bug, the fix is to resolve
the symlinks once on the host and ship the result as regular files.

**How it was produced** (run on the host, from the repo root, with the
following submodules initialized: `src/external/libc`, `src/external/Libinfo`,
`src/external/libmalloc`, `src/external/libunwind`, `src/external/cctools`):

```sh
git submodule update --init \
  src/external/libc src/external/Libinfo src/external/libmalloc \
  src/external/libunwind src/external/cctools

cd Developer/Platforms/MacOSX.platform/Developer/SDKs/MacOSX.sdk/usr/include
mkdir -p /tmp/sdk-flat/usr/include
tar -chf - --warning=no-file-changed . 2>/dev/null \
  | tar -xf - -C /tmp/sdk-flat/usr/include
tar czf tests/vendor/macosx-sdk-flat.tar.gz -C /tmp sdk-flat
```

`tar -chf -` (`-h` = dereference symlinks) silently drops any symlink whose
target doesn't exist (further uninitialized submodules — ARM/PPC/sparc
architecture headers, libxslt, etc.) rather than aborting, which is exactly
what's wanted here: only the subset actually reachable from the currently
initialized submodules gets flattened. If a future compile needs a header
that isn't in the tarball, initialize the submodule that provides it and
regenerate with the command above.

**Do NOT** try to fix this by dereferencing symlinks *from the guest* (`cp -a`,
`cp -RL`, `rsync -L`, `tar -h` run on the VM against `/path/to/workspace`) — every
broken-symlink `readlink()` leaks a `fuse_msgbuf` in the FreeBSD FUSE client,
and walking thousands of them (this SDK has ~2600) reliably wires enough RAM
to OOM-kill the guest. This exact incident happened once already; see the
`dev-vm-virtiofs-fix` memory for the recovery procedure if it happens again.

## fakesdk/

`stdarg.h` and `stdbool.h` — this FreeBSD-packaged clang ships only the
modular `__stdarg_*.h`/`__stddef_*.h` fragments (meant to be assembled by the
target libc's own umbrella header), and the vendored macOS SDK doesn't
provide those umbrellas either (a real Xcode install ships them alongside
its own clang, not via the SDK). These two files are the standard portable
compiler-builtin implementations, filling that gap for any target.

## Usage

See `build-freebsd/build-real-macho-tests.sh`, which uses both to
cross-compile real Mach-O test binaries (as opposed to the hand-generated
ones from `gen-hello-*.py`) with `clang -target x86_64-apple-macos` +
LLVM's `ld64.lld` against the actual `libSystem.B.dylib` from the darling
overlay.
