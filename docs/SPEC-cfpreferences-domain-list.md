# SPEC: why `defaults read` sees zero domains

## Status: inconclusive by static analysis — full call chain traced, no single breaking
line found. Three prior hypotheses (cfprefsd/XPC, `getattrlistbulk`, generic "CF can't
enumerate directories") are now all either refuted or refined below. This document
records what was actually verified, with symbol/offset citations, and lists the
concrete next step (live trace) that static disassembly cannot substitute for.

All addresses below are **file offsets inside the extracted x86_64 slice**, i.e.
`vmaddr` for this non-PIE-ish Mach-O dylib (`__TEXT` starts at 0), obtained via:

```
dd if=artefacts/darling-overlay/.../CoreFoundation of=CF_x86_64 bs=1 skip=2928640 count=3089548
llvm-nm-18 --defined-only CF_x86_64
llvm-objdump-18 -d --macho --disassemble-symbols=<sym> CF_x86_64
```

(paths outside the `hal/darling-freebsd` root, read-only, per task instructions)

## 1. Confirmed: no cfprefsd, no XPC, no `getattrlistbulk`

`defaults` (`src/external/foundation/tools/defaults.m` ~332-370) enumerates via
`[defs persistentDomainNames]`
(`src/external/foundation/src/NSUserDefaults.m:458-462`), which calls CF's
`_CFPreferencesCreateDomainList(userName, kCFPreferencesAnyHost)`.

Disassembly of `__CFPreferencesCreateDomainList` (CF_x86_64 offset `0xb7b30`) shows:

- calls `__preferencesDirectoryForUserHost` (0xb7b55) to get a `CFURL` for the
  Preferences directory
- calls `_CFURLCreatePropertyFromResource` (0xb7c5d) with property key
  `kCFURLFileDirectoryContents` (symbol `_kCFURLFileDirectoryContents`, referenced at
  0xb7c5d) to get the directory listing as an array of `CFURL`s
- for each entry, strips the `.plist`/`.<host>.plist` suffix (0xb7d65-0xb7e64) to
  produce the domain name array

No XPC, no mach ports, no `cfprefsd`-style IPC anywhere in this function or its
callees (traced 4 levels deep, see §2). No call to `getattrlistbulk` either — this
confirms and extends the already-rejected hypothesis: CF here is doing a completely
ordinary POSIX directory listing.

## 2. Confirmed: `kCFURLFileDirectoryContents` → plain `opendir()`/`readdir()`

Call chain (all offsets in `CF_x86_64`, symbol name → offset):

```
_CFURLCreatePropertyFromResource        0x155290
  → _CFURLCreateDataAndPropertiesFromResource   0x154a60
      → (scheme == "file") __CFFileURLCreateDataAndPropertiesFromResource   0x154b19-call site
          → __CFFileURLCreatePropertiesFromResource   0x155d30
              → __CFGetFileProperties            0x75550
                  → __CFGetPathProperties         0x75280
                      → __CFCreateContentsOfDirectory   0x74930
```

`__CFCreateContentsOfDirectory` (0x74930) does, in order:

1. an `open()` on `/dev/autofs_nowait` (0x74c3d) — this is an unrelated autofs-stall
   workaround, gated by `___CFProphylacticAutofsAccess`, and is **not** an open of the
   target directory (it's `/dev/autofs_nowait`, hardcoded literal at 0x74c32); harmless
   here since darwin's autofs doesn't exist and the flag defaults off (fd = -1 path).
2. `opendir$INODE64` (symbol-stub call at **0x74c70**) on the actual target directory
   path.
3. if `opendir()` returns `NULL`: releases the extension string, sets the result to
   `nil` and **jumps straight to the function's tail-return at 0x75243** (0x74c84-0x74cc0)
   — i.e. the whole property dictionary comes back nil, `CFURLCreatePropertyFromResource`
   fails, and `_CFPreferencesCreateDomainList` silently treats it as "no directory
   contents property" (its own check at 0xb7c83 `je 0xb7cc0` — the "no listing" branch
   that still returns an *empty* array, not an error). This exactly matches the observed
   symptom: no crash, no error, just nothing printed.
4. on success: `_CFArrayCreateMutableCopy`/`_CFArrayCreateMutable` (0x74ca3/0x74cd2),
   then loops `readdir_r$INODE64` (symbol-stub call at **0x74d03**) building the entry
   array.

So the single load-bearing question is: **does `opendir()` on the Preferences
directory succeed, and does `readdir_r()` return the entries it should?**

## 3. `opendir`/`readdir` implementation traced to source

`opendir$INODE64`/`readdir_r$INODE64` resolve (Darwin symbol-variant convention) to
Darling's own compiled libc:

- `src/external/libc/gen/FreeBSD/opendir.c`
  - `opendir()` (line 84-89) → `__opendir2(name, DTF_HIDEW|DTF_NODUP)`
  - `__opendir2()` (line 112-132): `_open(name, O_RDONLY|O_NONBLOCK|O_DIRECTORY|O_CLOEXEC)`
    (line 121-122), returns `NULL` on failure — this is the `_open` a plain
    `opendir()` fails through if the underlying `open()` fails.
- `src/external/libc/gen/FreeBSD/readdir.c`
  - `_readdir_unlocked()` (line 51-125) calls `__getdirentries64(dirp->dd_fd, dirp->dd_buf, dirp->dd_len, &dirp->dd_td->seekoff)`
    (line 91-93) to refill the buffer, and returns `NULL` if `dd_size <= 0` (line 100-101).

## 4. Confirmed: the *active* runtime path for CF/`defaults` is the LINUX_SYSCALL
translation layer, not mldr's `dispatch_macos_bsd_syscall`

There are **two independent macOS→FreeBSD syscall translation mechanisms** in this
tree. It matters which one actually executes for CF (built against Darling's own
`libsystem_kernel.dylib`), so this was checked directly in the compiled artefact:

`artefacts/darling-overlay/usr/lib/system/libsystem_kernel.dylib` (x86_64 slice,
extracted the same way as CoreFoundation):

- `___open` (0x43f4c): `mov eax, 5` ; `call __darling_bsd_syscall`
- `___getdirentries64` (0x42dbc): `mov eax, 0x158` (=344) ; `call __darling_bsd_syscall`
- `__darling_bsd_syscall` (0x5002c): indexes `__bsd_syscall_table[rax]` (a function
  pointer table, `src/external/xnu/darling/.../bsd_syscall_table.c:221`) and calls it
  **in-process**, no signal/trap involved. `[5] = sys_open` (wired implicitly via
  `sys_openat_nocancel`), `[344] = sys_getdirentries64`
  (`bsd_syscall_table.c:412`).

So the call is a plain in-process C function call into:
- `sys_open` → `sys_openat_nocancel`
  (`src/external/xnu/darling/darling/src/libsystem_kernel/emulation/src/xnu_syscall/bsd/impl/fcntl/openat.c:36-83`),
  which does `oflags_bsd_to_linux(flags)`
  (`.../emulation/src/conversion/fcntl/open.c:7-41`) then
  `LINUX_SYSCALL(__NR_openat, ...)` (a *raw Linux-numbered* `syscall` instruction).
- `sys_getdirentries64`
  (`.../emulation/src/xnu_syscall/bsd/impl/dirent/getdirentries.c:86-139`), which does
  `LINUX_SYSCALL(__NR_getdents64, fd, buf, sizeof(buf))`, then repacks the result from
  `struct linux_dirent64` into `struct bsd_dirent64`
  (`.../emulation/include/conversion/dirent/getdirentries.h`).

Those raw `syscall` instructions (with Linux ABI numbers) fault on FreeBSD and are
caught by `mldr`'s `sigsys_handler` (`src/startup/mldr/freebsd_syscall_trap.c:2603`),
which classifies `raw_eax` — since it's a bare Linux number `< 1024` with no class
byte, it goes to `dispatch_linux_syscall()` (not `dispatch_macos_bsd_syscall()`),
which has explicit, checked cases:
- `LINUX_SYS_openat` (line 1458-1465): translates flags via
  `mldr_open_flags_linux_to_freebsd()` (line 322-334), which handles
  `LINUX_O_DIRECTORY` (`0x10000`, matches the x86_64 value of
  `LINUX_O_DIRECTORY` defined in
  `emulation/include/conversion/fcntl/open.h:27` — `0200000` octal = `0x10000`).
  **Verified numerically consistent across both definition sites.**
- `LINUX_SYS_getdents64` (line 2199-2242): reads real FreeBSD dirents via
  `SYS_getdirentries`, repacks into Linux `dirent64` layout (offset 19 for
  `d_name`, matching `struct linux_dirent64` in the conversion header) — matches
  what `sys_getdirentries64` expects to parse back out.

**Both of these layers were read end-to-end and are individually self-consistent**
(flag bit values agree across all three definition sites checked; struct field
offsets agree between the mldr repacking code and the `struct linux_dirent64`/
`struct bsd_dirent64` readers). No off-by-one, wrong constant, or swapped field was
found in this chain by static reading.

## 5. A confirmed-separate, confirmed-unused-here landmine

`mldr`'s *other* dispatcher, `dispatch_macos_bsd_syscall()`
(`freebsd_syscall_trap.c:539`), handles raw macOS-class (`0x02000000|nr`) syscall
instructions — reached only for binaries whose syscall trampolines were **not**
routed through Darling's own `libsystem_kernel.dylib` (per its own comments: patched
raw-syscall sites in "upstream dyld overlay binary", #198). This dispatcher:

- has **no case for `getdirentries`/`getdirentries64`** at all — falls through to
  `default: -ENOSYS` (line 661).
- `case MACOS_SYS_open` (line 583-584) passes the flags word **straight through
  untranslated** to FreeBSD's native `open()` — Darwin's `O_DIRECTORY` (`0x100000`)
  is not FreeBSD's `O_DIRECTORY` bit value, so any caller landing in this path with
  `O_DIRECTORY` set would get the wrong FreeBSD flags.

This is confirmed **not** the path CF/`defaults` take (§4 shows they go through
`__darling_bsd_syscall`'s in-process table instead, never faulting with a
`0x02000000`-class `eax`). Flagging it here because it's a real latent bug for
whatever binaries *do* hit it, and because it was the first plausible-looking
candidate before the trace in §4 ruled it out.

## 6. What was NOT checked (explicitly unverified)

- **No live trace was done** — no ktrace/strace/gdb, no build, no VM interaction, per
  the task's read-only constraint. Everything above is static disassembly + source
  reading. It is possible for two independently-correct-looking layers to still
  interact badly at runtime (buffer sizing races, a wrong errno mapping in
  `errno_linux_to_bsd()` that turns a real error into a code the caller treats as
  "empty" rather than "failed", a `SIGSYS` re-entrancy issue if `opendir()`'s
  `open()` and `getdirentries()` land back-to-back inside the same altstack, etc.)
  — none of these were ruled out, only the "obvious wrong constant" class of bug.
- **`vchroot_expand()` path resolution for the *directory* was not traced**, only
  confirmed it's the same function used for file opens. It was not verified that
  `__preferencesDirectoryForUserHost` produces byte-for-byte the same resolved path
  as whatever `defaults write` used to create `com.bsdos.demo.plist` (e.g. a missing
  trailing slash, a stale/short vchroot cache, or the write path going through a
  different helper that doesn't call `vchroot_expand` at all — not checked).
- **`errno_linux_to_bsd()`** (`emulation/src/conversion/errno.c` or similar) was not
  read — if `LINUX_SYSCALL(__NR_openat, ...)` on the real directory returns some
  Linux errno that this function maps to `0`/success or to an errno the libc opendir
  path treats as "not a real failure", that would reproduce the symptom without any
  of the flag/struct-layout code above being wrong. **This is the single most likely
  remaining candidate** and is the natural next static check.
- **`round_to_4()` / actual byte contents of a live directory listing were not
  simulated** — the struct-layout consistency argument in §2 is a paper argument
  (matching field types/order implies matching `offsetof`), not a byte-level
  hexdump comparison against a real captured buffer.
- Did not check whether the Preferences directory (`root/Library/Preferences/`)
  itself has some permission or mount-flag quirk (e.g. created with different
  ownership than the process opening it for read) that would make `open(...,
  O_DIRECTORY)` fail with `EACCES` specifically on read while a separate write-path
  (that never opens the directory itself, only creates a file inside it) succeeds.
- Did not check the i386 slice of either binary — only x86_64 was disassembled
  throughout, on the assumption the runtime under test is x86_64 (Squirrel dev VM,
  per project conventions); if the failing binary is actually running as i386 this
  entire trace would need repeating against `CF_i386`/`lsk_i386`.

## Recommendation

Since static reading of every layer in the chain (CF → libc opendir/readdir →
libsystem_kernel syscall stubs → mldr's Linux-syscall SIGSYS trap → real FreeBSD
syscalls) turned up no contradiction, the fastest way to actually find the bug is a
live trace of one `defaults read` invocation, watching for:

1. Whether `openat(..., O_DIRECTORY)` on the Preferences directory returns a valid
   fd or an error (and which errno).
2. If it succeeds, whether `getdirentries` returns a non-zero byte count for a
   directory known to contain at least `com.bsdos.demo.plist`.
3. If both look correct at the FreeBSD syscall level, add one `fprintf(stderr, ...)`
   at the `errno_linux_to_bsd()` call sites in `openat.c`/`getdirentries.c` to catch a
   silent success-coded failure.

This is out of scope for the current (read-only, no-build) investigation.

## 7. 2026-08-26 — live trace: all five prior hypotheses are moot, the guest dies in
dyld's own startup before CF is ever reached

`DARLING_TEST_BINARY=defaults-macho DARLING_TEST_ARGS='read' truss -f -s 90 /tmp/dynsmoke`
was re-run (`/tmp/tr4.log` on the dev VM, guest pid **11696**, the pid that survives
`execve("/var/darling-build/dserver/mldr-real/mldr", ...)` at file line 11217 — pids
11697-11721 that appear alongside it are `launch-dynamic-smoke.c`'s own `fork()`
calls plus the pax-based overlay-staging harness, not the traced target). The
`defaults write com.bsdos.demo Greeting Hello` trace from the same session
(`/tmp/tr3.log`, guest pid **8740**, `argc=5` confirming the write-with-4-args
invocation vs. `read`'s `argc=2`) was also re-examined for comparison.

**Finding, stated plainly: neither run ever reaches `_CFPreferencesCreateDomainList`,
`opendir()` on any Preferences directory, or `getpwuid()`/`master.passwd` at all.**
Grepping the *entire* post-`execve` trace for pid 11696 (576 lines) for
`Library|passwd|HOME|getenv` returns **zero** matches outside two bare
`issetugid()` calls. The only files ever `open()`/`openat()`'d by 11696 after
`execve` are: `/etc/libmap.conf`, `/usr/local/etc/libmap.d` (ENOENT), `/lib/libc.so.7`,
`/lib/libthr.so.3`, `/lib/libsys.so.7`, `/tmp/darling-local-overlay/defaults-macho`
(the target Mach-O itself, opened by mldr's own loader) and
`/tmp/darling-local-overlay/usr/lib/dyld` (dyld itself, likewise opened by mldr).
**No CoreFoundation, no Foundation, no libobjc, no libdispatch dylib is ever
opened.** This directly falls out of the trace — it is not an inference. The
"~41 guest dylibs map per process" status line in `README.md` does not describe
this build/run.

Instead, both traces show the exact same terminal sequence, byte-for-byte
identical in shape, right after mldr hands control to dyld's entry point
(`write(2,"[darling-mldr] DEBUG pre-start: ...")` in the log, marking mldr's
last message before jumping in):

```
11696: sigaction(SIGSEGV, ...) = 0                          ; mldr installs its own crash handlers
11696: write(2,"[darling-mldr] DEBUG pre-start: mh=... entry=...")
   ... jump to dyld entry point; every subsequent syscall from here is a raw
       Linux-ABI syscall caught by mldr's ud2/SIGILL trap (the "SIGNAL 4
       (SIGILL) code=ILL_PRVOPC ... sigreturn" pairs bracketing every line
       below are that trap mechanism, not application code) ...
11696: getrandom(...) x2, getpid(), thr_self(), sigprocmask(BLOCK)
11696: sendmsg(234638, {AF_UNIX ".darlingserver.sock"}, ... 32 bytes) = 32
11696: recvmsg(234638, ...) = 8
11696: sigaction(SIGHUP..SIGIO, one syscall per signal, all installing the same
       handler at the same address — dyld installing its own crash-reporter
       signal handlers)
11696: mprotect(guard page, PROT_NONE)
11696: sigaltstack(0x23ae90,0x23bea0)  ERR#1 'Operation not permitted'
```

then, a few more `getpid`/`thr_self`/RPC-request-reply cycles later, the **very
last** exchange before death:

```
11696: sendmsg(234638, ..., [{"\a\0\0\0...\xff\xff\xff\xff\xff\xff\xff\xff",24}], ...) = 24
11696: recvmsg(234638, ..., [{"\a\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0...",48}], ...) = 8
11696: sigprocmask(SIG_SETMASK,{},{SIGILL}) = 0
11696: write(2,"dyld: dyld std::__terminate()\n\n") = 31
11696: sigprocmask(SIG_UNBLOCK,{SIGABRT},{SIGILL}) = 0
11696: write(1,"abort_with_payload: reason: dyld std::__terminate()\n; code: 9\n") = 62
11696: kill(0,SIGABRT) = 0
11696: SIGNAL 6 (SIGABRT) code=SI_USER pid=11696 uid=0
11696: SIGNAL 4 (SIGILL) code=ILL_PRVOPC trapno=1 addr=0x825a96749
11696: process killed, signal = 4 (core dumped)
```

`tr3.log` (the `write` run, pid 8740) has the **identical** sequence — same
`sigaltstack(...) ERR#1`, same RPC-then-terminate tail, same
`write(2,"dyld: dyld std::__terminate()\n\n")`, same `process killed, signal = 4`
(not 6 — the abort's own `SIGABRT` delivery races with another `SIGILL` from the
raw-syscall trap and the process dies to the *trap signal*, not the intended
abort; a secondary artifact, not the root cause). 728 lines total for pid 8740,
essentially matching 11696's 576 (the small difference is `argc` — write's 5
argv slots vs. read's 2 mean a few more early startup bytes, not more work
done). **`read` and `write` do not diverge in this trace. Both die identically,
before either one does anything CF-related.** This falsifies the working
premise that `write` succeeds and only `read` is broken — that claim (also in
`README.md`'s status table) is not reproduced by this build's current binaries;
either the binaries are stale relative to when that observation was made, or it
was observed against a different build. Not established which — flagged in §8.

**The RPC tag identification is exact, not guessed:** the last RPC before
`std::__terminate()` is call number 7. `src/external/darlingserver/scripts/
generate-rpc-wrappers.py` lists calls in emission order starting at 1
(`checkin`=1, `checkout`=2, `vchroot_path`=3, `kprintf`=4, `started_suspended`=5,
`get_tracer`=6, **`uidgid`=7**, params `(int32_t new_uid, int32_t new_gid)` →
returns `(int32_t old_uid, int32_t old_gid)`, script lines 139-145). The 24-byte
send payload's last 8 bytes are `\xff\xff\xff\xff\xff\xff\xff\xff` = two
`int32_t(-1)` = exactly `dserver_rpc_uidgid(-1, -1, &stored_uid, &stored_gid)`,
the *pure-read* call documented in `SPEC-getpwuid-guest.md` §4
(`src/external/darlingserver/.../unistd/getuid.c:41`). The 8-byte reply is all
zero bytes = `old_uid=0, old_gid=0` — the zero-initialized virtual-root value
from `duct-tape/src/task.c:78`, exactly as that spec predicted. **This same
call (identical 24-byte payload) is also made once earlier, by mldr itself,
before it ever jumps into dyld** — so `uidgid` succeeding and returning 0/0 is
not itself the crash; dyld calling it again right at the end of its own
init and then immediately terminating is the observed juxtaposition, not a
proven causal link. Do not read more into this than the trace shows: the
**only** hard fact is that `uidgid()→(0,0)` is the last RPC dyld makes, and
`std::__terminate()` is the very next thing it does — nothing application-level
happens in between (one `sigprocmask` restore, no other syscall).

**A ready-made tool exists in-tree to identify the actual C++ exception type
and throw site**, and was not yet used because the transport dropped mid-session
(see below): `MLDR_TRAP_AT=<image>+<hexoff>`, documented at `README.md:101` and
implemented at `src/startup/mldr/freebsd_syscall_trap.c:1213-1239`
(`mldr_report_trap()` — for a trap labeled `c++abi...`, it reads
`std::type_info` off `rsi` and prints the exception's demangled-ish name string,
plus the caller and a best-effort backtrace). The exact invocation for
`__cxa_throw` is already spelled out in a code comment at
`freebsd_syscall_trap.c:1118`: `MLDR_TRAP_AT='libc++abi.dylib+0x2c670' for
__cxa_throw`. This was launched
(`DARLING_TEST_BINARY=defaults-macho DARLING_TEST_ARGS='read'
MLDR_TRAP_AT='libc++abi.dylib+0x2c670' /tmp/dynsmoke > /tmp/trapread.log 2>&1`,
output on the guest at `/tmp/trapread.log`) but the guest virtio-console agent
(`bsdos.agent`, `/tmp/bsdos-agent-vport-x86.sock`) went `disconnected`
immediately after that run and did not come back before this session ended, so
**`/tmp/trapread.log`'s content was not read back and is not reported here as
fact** — it is real output sitting on the dev VM's disk, unread. This is the
single most promising unread artifact for finishing this investigation: it
should contain the exact exception type, its throw site, and a symbol-resolved
backtrace for the `std::__terminate()` seen above.

## 8. What was NOT checked in this pass (2026-08-26 live-trace addendum)

- **`/tmp/trapread.log` was never read** — the vport agent transport dropped
  (`virsh dumpxml build-vm` shows channel `bsdos.agent` state `disconnected`)
  right after the `MLDR_TRAP_AT` run completed in the background, and repeated
  `agent_exec`/`PING` retries over ~1 minute did not recover it. This is the
  known recurring bug in `docs/DEV-VM.md` (agent opens the wrong chardev after
  a restart) — was not fixed here per this task's no-SSH constraint. **Whoever
  picks this up next: read `/tmp/trapread.log` on the guest first, before
  re-running anything** — the data likely already answers "what exception, what
  throw site."
- **Did not determine why `write` (per `README.md` and presumably an earlier,
  working session) is claimed to succeed while this trace shows it dying
  identically to `read`.** Two live possibilities, neither checked: (a) the
  currently-staged `/var/darling-build/dserver/mldr-real/mldr` /
  `/tmp/darling-local-overlay/usr/lib/dyld` are stale/rebuilt since the
  write-success observation and now crash universally regardless of args; (b)
  the write-success observation was against a different overlay/build
  directory than the one these two traces ran against. Did not check build
  timestamps or git history around `mldr`/`dyld overlay` to distinguish these.
- **Did not disassemble `libc++abi.dylib+0x2c670`** or otherwise confirm by
  static means what throws there — deferred entirely to the (unread)
  `MLDR_TRAP_AT` run.
- **Did not check whether the `sigaltstack(...) ERR#1 'Operation not
  permitted'` seen in both traces (right after dyld installs its
  per-signal crash handlers, before the final `uidgid` call) is related to the
  termination.** It returns an error dyld may or may not check; not traced
  whether dyld's own code path branches on this return value at all. Flagged
  as a real, reproducible anomaly, not claimed as the cause.
- **Did not re-run under plain `truss` a third time to confirm the exact tail
  sequence is fully deterministic** (only two independent captures — one per
  argv variant — were compared; both already existed on disk from the prior
  session per the task brief, "не запускай без необходимости").
- **Did not check the i386 slice or any other test binary** (e.g.
  `hello-foundation-macho`, which `README.md` claims does map ~41 dylibs) to
  see whether the dyld-startup crash is universal to every guest binary in the
  current build or specific to `defaults-macho`. If it is universal, that
  reframes this whole investigation away from CFPreferences entirely.
