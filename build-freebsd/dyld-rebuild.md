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
accepts the flag, emits `LC_ID_DYLINKER`, and then forgets to set the filetype.
So the target cannot be dropped into the overlay as it stands: a dynamic linker
has to be `MH_DYLINKER`, and an `MH_EXECUTE` file will not be accepted as one.

The filetype is a four-byte field at offset 12, so it can be corrected after the
link. Doing that produces a file that reads as a dynamic linker:

    $ file dyld.patched-dylinker
    Mach-O 64-bit x86_64 dynamic linker, flags:<NOUNDEFS|DYLDLINK|TWOLEVEL|PIE>

**This is not verified.** Nobody has loaded the patched binary: it has never run.
Whether a dylinker with `LC_MAIN` (rather than `LC_UNIXTHREAD`) and no
`LC_LOAD_DYLIB` is accepted is exactly the question the run below has to answer,
and the answer is not known yet.

## Trying it

Keep the current overlay binary, then put the patched build in its place and run
the usual Chrome smoke test:

    cp -p $DARLING_OVERLAY/usr/lib/dyld $DARLING_OVERLAY/usr/lib/dyld.keep
    cp <dyld.patched-dylinker> $DARLING_OVERLAY/usr/lib/dyld
    WAYLAND_DISPLAY=wayland-1 XDG_RUNTIME_DIR=/tmp/wayland-test \
      WAIT_SECS=60 sh build-freebsd/launch-chrome.sh

The line to look for, in the log that script prints:

    calling sNotifyObjCMapped=... first=...

`first=` names the first image whose `sNotifyObjCMapped` is called; the question
this whole exercise is about is which image still has the broken callback in
`notifyBatchPartial`.

`launch-chrome.sh` drives `launch-dynamic`, which needs root to stage the
application bundle, and the operator has not cleared that. Everything above the
run is done and verified; the run itself is the part that is missing.
