# Rebuilding dyld from this tree (FreeBSD host)

Verified on a FreeBSD 15.1 host, 2026-09-26, with clang + ld64.lld. Everything
below was checked against a file, an exit code or a log line — nothing is
inferred.

## The sources are already here

`src/external/dyld` is a submodule, and its pinned base commit is not available
offline, so it cannot be checked out or diffed. The working tree of that
submodule is, however, the September state, and it is what the build reads:

| File | md5 |
|---|---|
| `src/external/dyld/src/dyld2.cpp` | `4852f20ecd28a1449ef01664ecffe4cb` |
| `src/external/dyld/src/dyldFreeBSDRebase.c` | `d96b710da667f37b85b47f8aee7495d2` |
| `src/external/dyld/src/dyldInitialization.cpp` | `5c9313e3a0bdcf06ee8fa3488370cfd9` |
| `src/external/dyld/darling/src/sandbox-dummy.c` | `1f3e244379548c3479c13349dda396d5` |

`build-freebsd/dyld-salvage/` holds byte-identical copies of the same four files,
so nothing depends on the submodule's git state.

The log patch is in `dyld2.cpp` and compiles into the binary:

    dyld::log("calling sNotifyObjCMapped=%p first=%s\n", (void*)sNotifyObjCMapped,
              objcImageCount > 0 ? paths[0] : "(none)");

    $ strings <built dyld> | grep sNotifyObjCMapped
    calling sNotifyObjCMapped=%p first=%s

## The build

    cmake -G Ninja -B $DARLING_BUILD_DIR/dyld-only .
    ninja -C $DARLING_BUILD_DIR/dyld-only system_loader

`cmake/FreeBSD.cmake` detects the host with `uname -s` and turns the compat layer
on by itself; no `-D` flags are needed. Configure takes about two seconds. The
build links the static `libc_static`, `libsystem_static`, `compiler_rt_static`
and friends that are already in the tree, so it is a couple of minutes, not an
hour. `ninja` exits 0 and a second run is a no-op.

Output: `$DARLING_BUILD_DIR/dyld-only/src/external/dyld/dyld` (the
`system_loader` target sets `OUTPUT_NAME "dyld"`), Mach-O 64-bit x86_64, about
2.2 MB.

## The catch: it is not a dynamic linker yet

    $ file <built dyld>
    Mach-O 64-bit x86_64 executable, flags:<NOUNDEFS|DYLDLINK|TWOLEVEL|PIE>
    $ file $DARLING_OVERLAY/usr/lib/dyld
    Mach-O 64-bit x86_64 dynamic linker, flags:<...|BINDS_TO_WEAK|PIE>

`filetype` in the Mach-O header is 2 (`MH_EXECUTE`) where the overlay's dyld has
7 (`MH_DYLINKER`). The cause is in the build log, once:

    ld64.lld: warning: Option `-dylinker' is not yet implemented. Stay tuned...

The link line asks for `-Wl,-dylinker` (see `target_link_libraries(system_loader
... -Wl,-dylinker ...)` in `src/external/dyld/CMakeLists.txt`) and ld64.lld
accepts the flag, emits `LC_LOAD_DYLINKER`, and then forgets to set the
filetype. (`LC_ID_DYLINKER` is 0xf; 0xe, which is what shows up here, is
`LC_LOAD_DYLINKER` — and mldr refuses precisely that, see below.)
So the target cannot be dropped into the overlay as it stands: a dynamic linker
has to be `MH_DYLINKER`, and an `MH_EXECUTE` file will not be accepted as one.

The filetype is a four-byte field at offset 12, so it can be corrected after the
link. Doing that produces a file that reads as a dynamic linker:

    $ file dyld.patched-dylinker
    Mach-O 64-bit x86_64 dynamic linker, flags:<NOUNDEFS|DYLDLINK|TWOLEVEL|PIE>

### Upgrading the linker does not help

The obvious thing to try first is a linker that implements the option. All
three linkers installed here accept `-dylinker` and ignore it:

    $ for v in 19 20 21; do /usr/local/llvm$v/bin/ld64.lld -arch x86_64 \
        -platform_version macos 11.0 11.0 -dylinker -e _start -o /dev/null x.o; done
    ld64.lld: warning: Option `-dylinker' is not yet implemented. Stay tuned...   (19.1.7)
    ld64.lld: warning: Option `-dylinker' is not yet implemented. Stay tuned...   (20.1.8)
    ld64.lld: warning: Option `-dylinker' is not yet implemented. Stay tuned...   (21.1.8)

So correcting the field after the link is the only route with the toolchain
available, not a workaround for an old linker.

### The kernel does check the filetype, so this is required, not cosmetic

`bsd/kern/mach_loader.c` switches on it, and the depth at which an image is
loaded is what tells the two apart:

    829  case MH_EXECUTE:
    830      if (depth != 1 && depth != 3) {
    831          return LOAD_FAILURE;
    ...
    848  case MH_DYLINKER:
    849      if (depth != 2) {
    850          return LOAD_FAILURE;
    852      is_dyld = TRUE;

A dynamic linker is loaded through `load_dylinker()` at depth 2 (mach_loader.c
:1443), which is the depth `MH_EXECUTE` rejects. So the unpatched build cannot
work as a dylinker: it fails header validation, before any of its own code
runs. The four-byte correction is what makes the image pass that gate.

### The entry command is wrong too, and this one is not fixable in four bytes

The kernel takes the entry point from `LC_UNIXTHREAD`, and takes it from
`LC_MAIN` only for the main executable:

    1199  case LC_UNIXTHREAD:
    1200      if (pass != 1) break;
    ...
    1212  case LC_MAIN:
    1213      if (pass != 1) break;
    1215      if (depth != 1) {
    1216          break;          /* ignored for a dylinker, which is depth 2 */
    1218      ret = load_main(...);

The two builds differ exactly there:

    overlay dyld     LC_UNIXTHREAD  LC_DYLD_INFO_ONLY  4x LC_LOAD_DYLIB
    this build       LC_MAIN        LC_DYLD_CHAINED_FIXUPS  no LC_LOAD_DYLIB

So at depth 2 the kernel walks past `LC_MAIN` and never sets an entry point at
all — the image would be mapped and then started at nothing. Same root cause as
the filetype: ld64.lld did not know it was linking a dylinker, so it produced
the two things an *executable* gets (`MH_EXECUTE`, `LC_MAIN`) instead of the two
things a *dynamic linker* gets (`MH_DYLINKER`, `LC_UNIXTHREAD`).

Correcting the filetype is a four-byte edit. Correcting the entry command means
rewriting a load command into a different load command — `LC_MAIN` carries
`entryoff`/`stacksize`, `LC_UNIXTHREAD` carries a thread-state flavour and a
count of registers plus a PC — which is real Mach-O surgery, and something to do
deliberately with a run behind it, not as a side effect of a rebuild.

### And mldr refuses a dylinker that names one

    src/startup/mldr/loader.c:346:
        "Dynamic linker can't reference another dynamic linker"

ld64.lld emits `LC_LOAD_DYLINKER` unconditionally, and this build has it, so
that check fails too — after the filetype and after the entry point.

## The fixup pass

All three of these are one script now: `build-freebsd/fixup-dylinker.sh`. It
takes a freshly linked `system_loader` and writes an image with

* filetype 7,
* no `LC_LOAD_DYLINKER`,
* `LC_UNIXTHREAD` instead of `LC_MAIN`, carrying the entry address as an
  x86_64 thread state (flavour 4, count 42, rip in slot 16),

and it keeps the file the same length, so no segment offset moves. Run on the
current build it reports:

    filetype 2 -> 7 (MH_DYLINKER)
    LC_MAIN -> LC_UNIXTHREAD (x86_THREAD_STATE64, 42, rip=0x100000770)
    LC_LOAD_DYLINKER: removed
    load commands: 15 -> 10, sizeofcmds 1840 -> 1824 (-16)
    wrote ... 2289824 bytes, size unchanged

Two independent checks on that output: `llvm-nm` puts `__dyld_start` at
`0x100000770`, which is exactly the address the script derived from `__TEXT`
vmaddr plus `LC_MAIN` entryoff; and the thread command it writes is byte-for-byte
the same shape as the one in the overlay's working dyld (184 bytes, flavour 4,
count 42, rip in slot 16, twenty zero registers). The log-patch string is still
in the binary afterwards.

### What the kernel does with that thread state

`load_threadentry()` (bsd/kern/mach_loader.c:2898) does not read fixed offsets.
It walks the blob as a list of entries, each a flavour, a count, then that many
32-bit words, and hands each one to `thread_entrypoint()`. One entry is
therefore the right shape, and that is what the fixup writes: flavour 4
(`x86_THREAD_STATE64`), count 42, 21 64-bit slots.

`thread_entrypoint()` is declared in `osfmk/kern/thread.h` and used in
`mach_loader.c`, but its x86_64 body is not in this tree — only the arm, arm64
and i386 `status.c` variants are here. So what it does with the other twenty
slots cannot be read from the source available. What can be said: the overlay's
working dyld has exactly the same shape, with every slot except `rip` zero, so
zeros are what the kernel is being fed today and it copes.

The address in the command is real code, which is at least a check that the
derivation did not point somewhere silly:

    $ llvm-objdump -d --start-address=0x100000770 <fixed-up dyld>
    100000770: 5f                    popq   %rdi
    100000771: 6a 00                 pushq  $0x0
    100000773: 48 89 e5              movq   %rsp, %rbp
    100000776: 48 83 e4 f0           andq   $-0x10, %rsp

and in the unfixed build that same address is labelled `__dyld_start`.

Note the first instruction: the entry pops an argument off the stack. What the
kernel puts there for an image loaded at depth 2 is one of the things only a run
can answer.

**Still not verified by loading.** Everything above is checked against the
kernel's rules, against the reference dyld and against the symbol table. Whether
the image runs is still the open question, and the run below is the only thing
that answers it.

## Trying it

Build, fix up, install, run:

    cmake -G Ninja -B $DARLING_BUILD_DIR/dyld-only .
    ninja -C $DARLING_BUILD_DIR/dyld-only system_loader
    sh build-freebsd/fixup-dylinker.sh \
      $DARLING_BUILD_DIR/dyld-only/src/external/dyld/dyld \
      $DARLING_BUILD_DIR/dyld-only/src/external/dyld/dyld.patched
    cp -p $DARLING_OVERLAY/usr/lib/dyld $DARLING_OVERLAY/usr/lib/dyld.keep
    cp $DARLING_BUILD_DIR/dyld-only/src/external/dyld/dyld.patched \
       $DARLING_OVERLAY/usr/lib/dyld
    WAYLAND_DISPLAY=wayland-1 XDG_RUNTIME_DIR=/tmp/wayland-test \
      WAIT_SECS=60 sh build-freebsd/launch-chrome.sh

The line to look for, in the log that script prints:

    calling sNotifyObjCMapped=... first=...

`first=` names the first image whose `sNotifyObjCMapped` is called; the question
this whole exercise is about is which image still has the broken callback in
`notifyBatchPartial`.

`launch-chrome.sh` drives `launch-dynamic`, which needs root: the harness itself
refuses to start otherwise (`tests/launch-dynamic-smoke.c`, `getuid() != 0` ->
"Must run as root"), and so does `darlingserver`
(`src/external/darlingserver/src/darlingserver.cpp`, `getuid() != 0 ||
getgid() != 0` -> "darlingserver needs to start as root"). Running the pair by
hand as an ordinary user gets the same answer, and `mldr` then fails the
check-in with `BAD SEND STATUS: -13`.

## What the runs showed, once root was available

Three runs of the same harness, same day, same target where noted. Logs are
`$DARLING_BUILD_DIR/smoke-chrome-<date>.log`.

| dyld | target | result |
|---|---|---|
| overlay's, `ad4850c1` | chrome-macho | `FATAL signal 11 at 0x10005e6d4`, 0 images loaded |
| overlay's, `ad4850c1` | hello-dynamic-macho | same crash, same address, 0 images |
| rebuilt + fixed up, `0d77b081` | hello-dynamic-macho | gets past it, then `FATAL signal 11 at addr=0x0` inside mldr, rip `0x220ee8` |

Two things follow, and the second one is not good news.

The first: the three header defects really were what stopped the image, because
the failure moved. With the shipped dyld the guest dies at `0x10005e6d4` before
anything is mapped; with the fixed-up rebuild it does not die there, gets to
mldr's pre-start, and fails later from inside mldr's own signal handler. No
loader rejection — no bad-Mach-O, no "dynamic linker can't reference another
dynamic linker" — so the filetype, the entry command and the stripped
`LC_LOAD_DYLINKER` were accepted.

The second: the overlay's dyld is broken on its own. It crashes at `0x10005e6d4`
on every guest binary, including the trivial one, and that predates any of this
work. The logs in the build directory show when: the last runs that loaded
anything were on 09-09 (41 images each), and everything from 14-09 onwards is
`0 images` with a fatal signal. So "the rebuild does not work" is not a verdict
on the rebuild — the thing it is being compared against does not work either,
and has not for over a week.

### Where the shipped dyld actually dies, and why the log line never printed

The shipped dyld is not a mystery binary: it is byte-for-byte the output of the
raw build, `md5 ad4850c1` — the same file as the `dyld-final` left in the build
tree by that day's script. The intermediate `dyld-nocmd` from the same run kept
its symbol table, and its `__TEXT` is byte-identical to the shipped file (every
differing byte is inside the load-command area), so its symbol map can be used
to read the crash.

`rip 0x10005e6d4` is not inside a function at all. It is a lazy binding stub:

    10005e6d4: 68 9c 07 00 00    pushq  $0x79c
    10005e6d9: e9 26 fc ff ff    jmp    0x10005e304 <__stub_helper>

So the shipped dyld dies resolving a binding, inside `__stub_helper` /
`_stub_binding_helper`. Not in the bootstrap, not in the rebase fallback, not in
the log patch.

And the log patch is in that binary — `strings` finds
`calling sNotifyObjCMapped=%p first=%s` in the shipped file exactly as in the
rebuilt one, and `sNotifyObjCMapped`, `notifyBatchPartial`, `rebaseDyldClassic`
and `sigexc_setup` are all present as symbols. The line never printed because
dyld never got as far as calling it: the binding path kills it first. The
symbols sit at `sNotifyObjCMapped 0x100075ba8` and
`notifyBatchPartial 0x1004ed10`, well past the crash.

The rebuilt binary gets further and then dies from inside mldr's
`crash_debug_handler` (`rip 0x220ee8`, i.e. `+0x1d8`) — a null dereference in the
crash reporter itself, after the guest has already faulted.

So the next thing to look at is the binding path, and specifically whether the
"ld64.lld emits an empty chained-starts payload" problem that
`dyldFreeBSDRebase.c` works around on the rebase side has a twin on the binding
side. That is where the guest stops making progress, in both builds.

### The two builds ask for opposite fixups, and only one side has a fallback

The raw build that produced the shipped dyld links with `-no_fixup_chains`. The
CMake `system_loader` target links with `-Wl,-fixup_chains`
(`src/external/dyld/CMakeLists.txt`, the `LINK_FLAGS` line). That is not a
cosmetic difference — it decides which tables the image carries:

    shipped dyld      LC_DYLD_INFO_ONLY                        classic opcodes
    cmake build       LC_DYLD_CHAINED_FIXUPS + EXPORTS_TRIE    chained, and empty

and the September diagnosis is that this linker emits an empty chained-starts
payload. So the CMake build asks for the one form that comes out empty, and the
asymmetry is visible in the source: there is a `rebaseDyldClassic()` for the
rebase side and nothing like it for the binding side — no `bindDyldClassic`, no
classic bind-opcode walker anywhere next to it.

Changing the flag to `-Wl,-no_fixup_chains` and relinking puts `LC_DYLD_INFO_ONLY`
in the image, and the run changes again:

    76a98007  cmake build, classic opcodes  -> signal 11, rip 0x1000e0d10

That address is inside the image's own `__TEXT` (0x100000000 to 0x100113000), so
the loader jumped to a code address in its own text and faulted there; the crash
decoder calls it unmapped only because the loader is not in its image map. A jump
to a wrong offset inside its own text is what a mis-applied rebase looks like,
which is the first symptom that points at the rebase path rather than the
binding path.

So the three builds fail in three different places, and each run moved the
failure somewhere new:

| build | failure |
|---|---|
| shipped, classic | dies in a lazy binding stub, `__stub_helper` |
| cmake, chained | reaches mldr's crash handler and null-derefs there |
| cmake, classic | jumps to a bad address inside its own `__TEXT` |

None of them prints the log line yet. The `CMakeLists.txt` with the flag changed
is in `build-freebsd/dyld-salvage/`, next to the other dyld files, because it
lives in the submodule and the superproject cannot carry it any other way.

### The classic build actually runs, and dies on its first trapped syscall

`rip 0x1000e0d10` in that last run is not a wild address after all. It is exactly
the first instruction of a real function:

    00000001000e0d10 <_task_self_trap_impl>:
    1000e0d10: 55                 pushq  %rbp
    1000e0d11: 48 89 e5           movq   %rsp, %rbp
    1000e0d14: 48 83 ec 10        subq   $0x10, %rsp
    1000e0d1c: e8 2f b2 00 00     callq  0x1000ebf50 <_dserver_rpc_task_self_trap>

and mldr's side of the same log has a line the other runs do not have:

    [darling-mldr] patched raw-syscall trampolines to ud2:
        generic-thunk=1 sigreturn-tramp=1
    [darling-mldr] macOS BSD syscall trap installed (SIGSYS/x86-64)

So with classic opcodes the loader gets as far as running and making its first
trapped syscall — `task_self_trap` is how it asks darlingserver to do one — and
faults there. The decoder calls the address unmapped only because the loader is
not in its image map; it is the loader's own code.

That moves the remaining failure out of the loader's startup and into the
syscall-trap path, which is a different subsystem again: mldr's trap, the
`dserver_rpc_task_self_trap` round trip, or what darlingserver answers.

The vchroot warning that is in every log turns out not to be it —

    Cannot open /usr/local/libexec/darling/usr/libexec/darling/vchroot

— it is in the two 09-09 logs that loaded 41 images just as much as in the broken
ones. Noise, not the cause. (The path in the dserver binary is
`mldr!/usr/local/libexec/darling/usr/libexec/darling/vchroot`, i.e. the prefix
applied to itself, so it is never going to resolve in this harness.)

### The dyld that boots is still on disk, and it boots

The runs that loaded anything were on 09-09. The raw build made on 14-09
replaced the dyld that was in the overlay, and since then every run has loaded
zero images and taken a fatal signal. The one it replaced was not thrown away:

    $DARLING_OVERLAY/usr/lib/dyld.June-backup   (also dyld.bak)
    md5 b8df2a76420adc9c6bfbc36282cdc6f5, 5221712 bytes, universal i386 + x86_64

Put that back and the guest works, on the same machine, with the same overlay:

    hello-dynamic-macho   prints "hello-dynamic"
    chrome-macho          41 images loaded, 0 fatal signals

Forty-one is the same count as the 09-09 logs, and the run ends where it always
ended — at Chrome's framework `dlopen` failing with "image not found", which is
the next problem and not a crash. So everything around the loader is healthy,
and the September loader is the only thing that broke.

The catch is the one that matters for this exercise: that binary has no log
patch. `strings` finds no `calling sNotifyObjCMapped` in it, though it does have
the `sNotifyObjCMapped` symbol. Every dyld that carries the log line cannot
boot; the one that boots does not carry it.

Getting to the line therefore means adding it to a loader that boots, and the
reference that boots is a June revision whose base commit is not obtainable
here — the submodule's object store does not contain it, and the remote does not
exist. So it cannot be done by rebuilding from the base, and rebuilding the
September sources does not boot. That is the wall, and it is a source problem
rather than a build-flag one.

### Is the September rebase what stops it booting? No.

Worth testing, because it is the most suspicious thing in the tree: the September
`rebaseDyld()` throws the analyzer away and calls only `rebaseDyldClassic()`.

    // walk all fixups chains and rebase dyld
    const dyld3::MachOAnalyzer* ma = (dyld3::MachOAnalyzer*)dyldMH;
    ...
    (void)ma;
    {
        if ( !rebaseDyldClassic(dyldMH) ) {
            // no classic opcodes either — nothing we can do; continue and
            // hope the image was mapped at its preferred address (slide 0)
        }
    }

Both symbols are in the build (`rebaseDyld` inlined into a block,
`rebaseDyldClassic` at `0x1000a0fa0`), and the June build has neither. So: make
`rebaseDyldClassic()` return `false` immediately, relink, fix up, run.

    rebase enabled    rip = _task_self_trap_impl + 0x0   (at 0x1000e0d10)
    rebase disabled   rip = _task_self_trap_impl + 0x0   (at 0x1000e0410)

Same function, same offset, only the absolute address moves because the code
layout changed. The classic rebase is exonerated: whatever the first trapped
syscall is doing wrong, it is not the rebase.

Incidental, and useful to whoever adds a switch to that file: it is freestanding
— no libc, no `getenv`. A first attempt at an environment-variable switch there
does not compile:

    error: call to undeclared function 'getenv'; ISO C99 and later do not
    support implicit function declarations

So the rebase path is off the list of suspects, and the syscall-trap path — mldr's
SIGSYS handling and the `dserver_rpc_task_self_trap` round trip — is what's
left.

### Except the trap path is not September's

Before blaming the trap, check whether the working build even goes through it.
It does, and by the same route:

    _task_self_trap_impl            june 1   september 1
    _dserver_rpc_task_self_trap     june 1   september 1
    dserver_rpc* symbols            june 147 september 147

Same functions, same RPC client, in both. So the trap is not something the
September tree introduced.

What is left as a difference between the loader that boots and every loader built
since is the link itself:

    June build (boots)        universal i386+x86_64, 5.2 MB,
                              4x LC_LOAD_DYLIB, links the shared
                              libSystem/libc from the SDK
    every build since         x86_64 only, ~2.2 MB, no LC_LOAD_DYLIB at all,
                              links libc_static / libsystem_static /
                              compiler_rt_static straight out of the tree

**That hypothesis was wrong, and it is withdrawn.** It rested on my reading of
the June binary's `LC_LOAD_DYLIB`s, and that reading was a bad parse — I walked
the fat header as if it were a Mach-O header. Parsed properly, with `otool -l`:

    dyld.June-backup (boots)   4 segments, NO LC_LOAD_DYLIB,
                                LC_DYLD_CHAINED_FIXUPS + LC_DYLD_EXPORTS_TRIE,
                                LC_SEGMENT_SPLIT_INFO, LC_UNIXTHREAD
    dyld.pre-salvage (807K)    4 segments, 4x LC_LOAD_DYLIB,
                                LC_DYLD_INFO_ONLY, LC_UNIXTHREAD
    this build                 5 segments, no LC_LOAD_DYLIB, LC_MAIN,
                                LC_DYLD_CHAINED_FIXUPS or _INFO_ONLY by flag

The four `LC_LOAD_DYLIB`s belong to the September build, not to June. So the
loader that boots links nothing dynamically, exactly like these do, and it uses
chained fixups — which means the `-Wl,-no_fixup_chains` change described above
was made for the wrong reason, and moving the build to classic opcodes moved it
*away* from the configuration that boots.

The flag is therefore back to `-Wl,-fixup_chains`, and the chained build was
rerun: it fails the same way the first chained run did (signal 10, from inside
mldr's crash handler). So chained versus classic is not the discriminator
either.

What is actually left between the loader that boots and this one: the
September source changes, and `LC_SEGMENT_SPLIT_INFO`, which the June build has
and this one does not.

### The log statement is not what breaks it either

That is worth separating, because it is the one line this whole exercise is
about. Take it out, rebuild, fix up, run:

    with    dyld::log("calling sNotifyObjCMapped=...")   signal 10
    without                                          signal 10

Same failure. So the log statement is exonerated, and the goal is not blocked by
the patch itself — it is blocked by the loader not booting.

### And neither is sigexc_setup

The other September file that changes behaviour at link time is
`darling/src/sandbox-dummy.c`, which replaces `sigexc_setup()` with a weak no-op
on the grounds that the real one comes from libsystem_platform, an empty
submodule. In this build the no-op is inert — the link picks up a real one:

    _sigexc_setup  ->  calls _darling_sigexc_self, _sigexc_setup1, _sigexc_setup2

And the loader that boots does something worse:

    00000000000e9480 <_sigexc_setup>:
    128ff4: 89 7d fc    movl %edi, -0x4(%rbp)
    128ff7: 0f 0b       ud2

It traps deliberately, and boots anyway — so this path is not reached during
startup in either build.

That leaves the September source changes as a whole, which cannot be bisected
any further from here: the submodule's base commit is not in the object store
and the remote does not exist, so there is no way to build the same tree with
those four files reverted.

### A build-system trap worth writing down

Restoring a file with `cp -p` preserves its modification time, so ninja
considered the target up to date and did not relink:

    [4/4] cd $DARLING_SRC_DIR && true

The fixup then ran over the previous binary and produced a byte-identical result,
which would have looked like "the change made no difference" — a conclusion drawn
from a build that never happened. `touch` the restored file, or restore without
`-p`, and check that the linker actually ran.
