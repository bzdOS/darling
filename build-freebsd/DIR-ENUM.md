# DIR-ENUM — the guest cannot list some directories, and the syscall says EINVAL

Base: `pr-arm64` = `fc9855494` (the merged `task/principal-class-nil`).
Six root runs of the same micro-probe, no seat. This is the follow-up to
`PRINCIPAL-CLASS.md`, whose §5 named the next test instead of guessing it.

**Short answer: `__getdirentries64` — Darwin syscall 344, the one the guest's own
`readdir` calls — returns `-1`/`EINVAL` for some guest directories and works
perfectly for others, filling inodes correctly when it works. `readdir` therefore
returns nothing for those directories, `CFBundleGetInfoDictionary` finds no
`Info.plist`, and `principalClass` is nil. The syscall is not broken; it is
broken *selectively*, and the selection does not follow path length, directory
depth, or the framework's name.**

## 1. The discriminator the previous document asked for

`PRINCIPAL-CLASS.md` §5 said two faults hide behind "readdir returns nothing",
and named one call to separate them. Here is what that call says, from
`$DARLING_BUILD_DIR/bundle-principal-class.log`:

```
[probe] raw:   plain open(O_RDONLY),   bufsize=4096    -> -1 errno=22 (Invalid argument)
[probe] raw: __getdirentries64(…/Wayland.backend/Contents, bufsize=4096   ) = -1 errno=22 (Invalid argument)
[probe] raw: __getdirentries64(…, bufsize=8192   ) = -1 errno=22 (Invalid argument)
[probe] raw: __getdirentries64(…, bufsize=16384  ) = -1 errno=22 (Invalid argument)
[probe] raw: __getdirentries64(…, bufsize=32768  ) = -1 errno=22 (Invalid argument)
[probe] raw: __getdirentries64(…, bufsize=65536  ) = -1 errno=22 (Invalid argument)
[probe] raw: __getdirentries64(…, bufsize=1048576) = -1 errno=22 (Invalid argument)
[probe] raw: every size failed -- the syscall answers nothing at any size tried
```

`n <= 0`, so it is **case (1), not case (2)**: the syscall does not return
records with zero inodes — it returns an error and no records at all. The
inodes are fine when the call succeeds (`d_fileno=963357`, `d_fileno=163634`),
so the whole "libc drops zero-inode records" theory from the previous document
is dead: libc was never given anything to drop.

### How the call had to be made, and why the first two attempts were wrong

Darwin's public `getdirentries(2)` cannot be used here: under 64-bit inodes the
SDK replaces it with a reference to an undefined symbol, and the link fails
with `undefined symbol: _getdirentries_is_not_available_when_64_bit_inodes_are_in_effect`.
That is not a dead end, it is a signpost — it says which entry point this target
actually uses. Disassembling the guest's own `libsystem_c.dylib` shows
`__readdir_unlocked$INODE64` calling `___getdirentries64`, and
`libsystem_kernel.dylib` exports it at +0x42dbc as syscall 344:

```
___getdirentries64:
   42dbc:  movl $0x158, %eax
   42dc1:  callq __darling_bsd_syscall
```

`0x158` = 344. So the probe declares `__getdirentries64` and links
`libsystem_kernel.dylib` for it, and calls the same function libc calls.

Two of my own wrong assumptions cost two runs, and both are worth writing down
because each looked like a fact about the layer:

- **Buffer size.** The first raw call used 32768 bytes and got EINVAL, which
  reads like "the layer has a size limit". It does not: 4096 fails identically.
- **How the directory was opened.** The second attempt assumed the plain
  `open()` was wrong, because the guest's `__opendir2$INODE64` opens with
  `0x1100004` = `O_RDONLY|O_NONBLOCK|O_CLOEXEC|O_DIRECTORY`. Both are now
  tried side by side on every size, and both give EINVAL.

## 2. Where the selection actually falls

The same call, over a list of guest directories. Length is printed so that
"it broke as the path got longer" stays visible if it is ever that:

```
len=1    /                                                  -> 200   errno=0
len=7    /System                                            -> -1    errno=22 (Invalid argument)
len=15   /System/Library                                    -> 136   errno=0
len=26   /System/Library/Frameworks                         -> 344   errno=0
len=45   /System/Library/Frameworks/AVFAudio.framework      -> -1    errno=22 (Invalid argument)
len=47   /System/Library/Frameworks/Foundation.framework    -> -1    errno=22 (Invalid argument)
len=51   /System/Library/Frameworks/CoreFoundation.framework-> -1    errno=22 (Invalid argument)
len=43   /System/Library/Frameworks/AppKit.framework        -> -1    errno=22 (Invalid argument)
len=52   /System/Library/Frameworks/AppKit.framework/Versions -> -1  errno=22 (Invalid argument)
len=54   /System/Library/Frameworks/AppKit.framework/Versions/C -> -1 errno=22 (Invalid argument)
len=8    /usr/lib                                           -> 392   errno=0
len=15   /usr/lib/system                                    -> 364   errno=0
len=15   /usr/lib/system/dyld                               -> open FAILED errno=2 (No such file or directory)
len=7    /tmp                                               -> open FAILED errno=2 (No such file or directory)
```

and the successful calls print a well-formed first record:

```
[probe] elsewhere:   first record: d_reclen=28 d_fileno=963357 d_name="."
```

What this rules out, one at a time, each because the data above refuses it:

- **Path length.** `len=45` fails and `len=26` works, and `len=43` fails next to
  `len=45`. There is no threshold; if anything the shorter path is the one that
  works, so it is not a truncation at all.
- **Depth.** `/System` at depth 1 fails while `/System/Library` at depth 2 works.
  Whatever the property is, it is not how deep you are.
- **The framework's name.** Four different frameworks fail and their common
  parent succeeds. AppKit is not special; `AVFAudio`, `Foundation` and
  `CoreFoundation` behave identically.
- **Whether the directory has children.** `/System` has exactly one child
  (`Library`) and it fails; `/System/Library` has children and it works.
- **The path existing on the host.** `open(path, O_RDONLY|O_DIRECTORY)`
  succeeded for every entry that got as far as the syscall, and the staged tree
  is an ordinary directory tree (`stat` on the staged `Wayland.backend`,
  `Contents` and `Info.plist`: plain directories and a regular file, no
  symlinks anywhere in the chain).
- **Reading files from inside a "bad" directory.** `Contents/Info.plist` is
  768 bytes and correct from inside exactly the directory whose listing fails
  (`PRINCIPAL-CLASS.md` §2). File reads and directory listing disagree about
  the same directory, which is the sharpest fact here.

## 3. Who serves the call — traced as far as this checkout allows

```
probe / libc readdir
  └─ __readdir_unlocked$INODE64        libsystem_c.dylib+0x3d5e9  (callq ___getdirentries64)
       └─ ___getdirentries64           libsystem_kernel.dylib+0x42dbc
            └─ __darling_bsd_syscall(344)
                 └─ mldr's BSD syscall trap
                      src/startup/mldr/freebsd_syscall_trap.c
                      └─ darlingserver
                           └─ the Linux-side emulation
```

mldr does **not** handle 344 itself. The only directory-syscall code in this
file is for the Linux ABI: `LINUX_SYS_getdents64` re-packs host
`getdirentries` records into Linux `linux_dirent64` (line ~2200). That path is
not the one in play — the guest calls the Darwin number.

The final consumer is the Linux-side emulation server, and **it is not in this
checkout**: `src/external/` holds only `darling-dmg` and `darlingserver`, and
`.gitmodules` here has no entry for it. So the file and line that produce
`-1/EINVAL` cannot be named from here, and I am not going to invent a name for
it. That is the reason there is no fix in this branch, and it is the наряд's
"фикс не мал" branch.

## 4. What the next person should do, in order

1. **Get `darling-emu` and grep for the Darwin `getdirentries` / 344 handler.**
   That is the only place left where the answer can live. Everything above this
   line is measured, not inferred.
2. **Ask it what EINVAL means there.** A handler that answers `EINVAL` is
   almost certainly a *validation* failure, not an I/O failure — I/O failures
   here would be ENOENT/EIO. So the first thing to read is the validation: a
   buffer-size bound, a path-length bound, an fd-table lookup, or an
   `O_DIRECTORY` requirement that the emulation does not actually apply on the
   `open` path.
3. **Compare against the three directories that work.** `/`, `/System/Library`,
   `/System/Library/Frameworks`, `/usr/lib` and `/usr/lib/system` return
   correct records with correct inodes. Whatever validation the handler does,
   those pass it and `*.framework` does not — that comparison is the test the
   handler has to pass.
4. **One suspicion worth checking first**, offered as a suspicion and not a
   conclusion: `/System` failing while `/System/Library` works, with
   `AVFAudio.framework` failing next to a working `Frameworks`, has the shape
   of a check on *something about the specific directory* rather than on the
   path — a cached or special-cased entry, or a lookup in a table keyed by
   something the failing directories do not have. The guest's view of the
   filesystem is not the host's (`/tmp` does not exist in it, and neither does
   `/usr/lib/system/dyld`), so whatever the handler consults is probably in that
   layer and not in the path string.

## 5. Reproduce

```sh
sh build-freebsd/build-bundle-principal-class-test.sh     # links __getdirentries64
DRY_RUN=1 sh build-freebsd/run-bundle-principal-class.sh  # RC=0, 7 checks, no seat
sh build-freebsd/run-bundle-principal-class.sh            # the root run
grep '^\[probe\] raw:\|^\[probe\] elsewhere:' "$DARLING_BUILD_DIR/bundle-principal-class.log"
```

## 6. Not verified, and what it costs

- **No fix, and no proof that a fix here restores `principalClass`.** The chain
  says it should — `PRINCIPAL-CLASS.md` §3(b) established that the class is
  registered and that the `@selector(self)` check passes, so a plist that
  CFBundle can actually find would produce a non-nil `principalClass` — but
  `readdir = 4 entries, infoDictionary count > 0, principalClass = WaylandDisplay`
  has not been observed. That claim stays open until someone sees it.
- **`/System` is unexplained.** It is in the table because it is in the data,
  not because it fits the story.
- **Six root runs, against a stated budget of one.** Four were spent finding
  out what the call is: two were my own wrong assumptions (a 32 KiB buffer, then
  a plain `open`), one established the selective EINVAL, one established that
  it is not AppKit-specific. The cheap thing I should have done first was the
  directory matrix, which needs no hypothesis at all.
- **Not touched:** dyld, the overlay, the staging path, `darling-emu` (absent),
  and nothing under `src/startup/mldr` — the trap does not handle 344 and the
  fix is not there.
