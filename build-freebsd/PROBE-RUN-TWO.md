# Second root run of the window probe: the Onyx2D refusal is gone, and the fault moved

One run was authorized and **two guest launches happened**, because the first
one turned out to have tested a stale binary and produced no information. Both
are reported here, the wasted one first, because the way it was wasted is the
more useful finding.

Base: `pr-arm64` at `c9d5c94ee` ("derive the staging list from the closure, and
make 3b walk it").

## Verdict up front

**The probe did not reach the shm buffer.** No `RESULT: window created`, no
`wl_display_connect` anywhere in the log.

But the staging fix did exactly what it was built to do, and the fault is now
somewhere else entirely:

- all **59** images of the transitive closure loaded, which is the same 59 the
  closure walk predicted;
- **Onyx2D loaded** — mapped, read, rebound — where the first run died asking
  for it;
- **zero** occurrences of `Library not loaded` or `image not found` in a
  10,273,068-byte log.

The new failure is a **guest SIGSEGV during the `dlopen` of the Wayland
backend**. That is a different class of problem, not a deeper staging miss.

## What the closure fix actually bought

The first run stopped at image 49 of the closure with an ENOENT. The new run
stops after image 59 — the whole closure, and then some way past it. The
loader got all the way through binding Foundation, AppKit, CoreFoundation and
libobjc, and reached the point of loading the very backend that talks to sway:

```
dyld: loaded: <4C4C44CA-5555-3144-A186-74318AFA1BCB> /System/Library/PrivateFrameworks/Onyx2D.framework/Versions/A/Onyx2D
...
dyld: rebase: Onyx2D:*0x282A4F91E090 += 0x282A4F8A8000
```

and the staging did what the preflight said it would:

```
staging trees: derived from the closure -- System/Library/Frameworks:System/Library/PrivateFrameworks:usr/lib
cached locally: /tmp/darling-local-overlay/System/Library/Frameworks
cached locally: /tmp/darling-local-overlay/System/Library/PrivateFrameworks
cached locally: /tmp/darling-local-overlay/usr/lib
etc cached locally: /tmp/darling-local-overlay/etc
```

`System/Library/PrivateFrameworks` is now in the cache, and it is there because
the closure walk derived it, not because anyone remembered to add it.

## The fault now

```
dlopen_internal(/System/Library/Frameworks/AppKit.framework/Versions/C/Resources/Backends/Wayland.backend/Contents/MacOS/Wayland, 0x00000115)
dlopen_internal(/System/Library/Frameworks/AppKit.framework/Versions/C/Resources/Backends/Wayland.backend/Contents/MacOS/Wayland, 0x00000109)
[darling-mldr] FATAL signal 11 (code=1) at addr=0x7fffffdedf1c
  rip=0x0000000825e96759  rax=0x479fe80cb00cd60d  rbx=0x00007f87ce67c1d0
  backtrace (3 frames):
    #00 0x220e2f <crash_debug_handler+0x11f> at .../dserver/mldr-real/mldr
    #01 0x82307f45a <_pthread_sigmask+0x50a> at /lib/libthr.so.3
    #02 0x82307ea5b <pthread_signals_unblock_np+0x5bb> at /lib/libthr.so.3
  guest stack dump at rsp=0x00007fffffdedd10:
```

Read plainly: the backend dylib is opened twice and the guest segfaults on the
second. The stack is the crash handler and two libthr frames, i.e. it is the
handler's own stack and the guest stack dump below it is empty in the log —
so this log says *where the guest died*, not *why*. Attribution has not been
done on it, and I am not claiming a cause.

Not ruled in, not ruled out, and stated as such: whether the fault is in the
backend's own initialisers, in `dlopen` of it, or in the host-side bridge it
loads. `0x7fffffdedf1c` is a stack address near the handler's own frames
(`rsp=0x7fffffdedd10`), which is consistent with a bad frame on a corrupt or
mismatched stack, and equally consistent with a wild jump. One log cannot
separate those.

`--recompute` is clean and the loader is not implicated:

```
$ python3 build-freebsd/attribution-from-log.py \
    "$DARLING_BUILD_DIR/wayland-window-probe.log" --dyld "$DARLING_OVERLAY/usr/lib/dyld" --recompute
slice sha256    : 23dfaa85011f89d07abba455ae5905d8b161492cd2344912097438811843eac0
trieWalk entry  : 0x376a0 (baked table says 0x376a0)
PASS: 0 disagreement(s) between the baked table and this binary
```

Same six anchors, same slice hash as the first report, so dyld is byte-for-byte
the binary that report was written about, and the trieWalk wall is untouched.

## The wasted run, and the hole it found

The first launch was invalid, and the reason is worth more than the run was.

`$DARLING_BUILD_DIR/launch-dynamic` was built on **17 September**. The probe
script rebuilt the probe binary and `conjure-wayland-input` as needed, but
never the harness — so the staging change had not been compiled into the binary
that does the staging. The run dutifully received
`DARLING_STAGING_TREES=System/Library/Frameworks:System/Library/PrivateFrameworks:usr/lib`
on its command line and staged the old three trees, because it was an old
program. It failed with the identical pre-fix message, and that failure said
nothing at all.

The tell was available in the log and I did not read it first: the wasted run's
log contained `usr/lib cached locally:` and `Frameworks cached locally:`,
which are the *old* code's strings. The new code prints
`staging trees: derived from the closure` and `cached locally: <path>`. A
mtime check on the log would have caught it too — the log was 12:35 while the
clock said 20:26.

A second, smaller thing blocked the first attempt outright: the script writes
its log with `>` as the invoking user, and the previous root run had left that
file owned by root, so the redirect failed with `Permission denied` and the
script went on to print the *stale* log in its failure message. That is its own
trap: a failed run whose error message is a previous run's log.

### Fix, in this branch

The preflight now rebuilds the harness whenever the source is newer than the
binary, and says which of the two happened:

```
  [PASS] launch-dynamic rebuilt from $DARLING_SRC_DIR/tests/launch-dynamic-smoke.c
```

or

```
  [PASS] launch-dynamic: $DARLING_BUILD_DIR/launch-dynamic (newer than its source)
```

A preflight whose job is to make a run's outcome mean something cannot be the
thing that lets a stale binary decide it. The `>log` ownership trap is **not**
fixed here — it is noted, not repaired, and the repair belongs with whoever owns
the harness's run directory.

## Reproduce

Sway has to be running; the preflight finds it by its ipc socket:

```sh
export PATH=/usr/local/bin:/usr/local/sbin:/usr/bin:/bin:/sbin:/usr/sbin
export DARLING_SRC_DIR=<src> DARLING_BUILD_DIR=<build> DARLING_OVERLAY=<overlay>

mkdir -p /tmp/wayland-test
XDG_RUNTIME_DIR=/tmp/wayland-test WLR_BACKENDS=headless sway -d 2>/tmp/sway_debug.log &

DRY_RUN=1 sh build-freebsd/run-wayland-window-probe.sh     # 6/6, new gate in step 3
sh build-freebsd/run-wayland-window-probe.sh                # the run
python3 build-freebsd/attribution-from-log.py \
  "$DARLING_BUILD_DIR/wayland-window-probe.log" \
  --dyld "$DARLING_OVERLAY/usr/lib/dyld" --recompute
```

If the log cannot be created because a previous root run owns it, move it aside
first — the script will otherwise fail at the redirect and then print the stale
log as if it were this run's.

## Not checked, not done

- **Not** reached: the shm buffer, the seat receiving a frame, anything in this
  run that says the plan's milestone is met. It is not.
- **Not** attributed: the SIGSEGV. No cause is claimed, and the empty guest
  stack dump means this log cannot supply one on its own.
- **Not** reproduced: one valid run, one guest launch. The wasted launch is
  counted separately above and is not a second data point.
- **Not** verified: that the 59 loaded images are *correct*, only that they
  loaded. Nothing here says the run would have painted correctly had it
  survived.
- **Not** fixed: the root-owned-log redirect failure.
- The log is at `$DARLING_BUILD_DIR/wayland-window-probe.log`, 10,273,068 bytes
  and 156,455 lines, outside the source tree and not committed: it is a
  machine-local artefact full of addresses and paths.
