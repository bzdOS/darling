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
