# The second `dlopen` of Wayland.backend: what differs, and why the log stops

Offline reading of run #2's log (`$DARLING_BUILD_DIR/wayland-window-probe.log`,
10,273,068 bytes, 156,455 lines). No root, no new run, nothing in the tree
touched. The crash is **not attributed** here; what follows is what the log
does and does not support.

Base: `pr-arm64` at `c9d5c94ee`.

## Short answer

The two `dlopen` calls differ by exactly one bit: `RTLD_NOLOAD`. The first is
a probe, the second is the **first real load**. The backend was never in dyld's
image list, so there is no "already loaded" state to explain and no double
initialization. What differs is that the first call was told *not to load
anything* and the second was not.

The crash then lands in the **host** address space, and the log's own
diagnostics destroyed the evidence they were written to collect.

## 1. Where the second `dlopen` dies

```
dlopen_internal(NULL, 0x00000110)
dlopen_internal(.../Backends/Wayland.backend/Contents/MacOS/Wayland, 0x00000115)
dyld: lazy bind: libdyld.dylib:... = libsystem_pthread.dylib:_pthread_getspecific
dyld: lazy bind: libdyld.dylib:... = libsystem_pthread.dylib:_pthread_setspecific
dlopen_internal(.../Backends/Wayland.backend/Contents/MacOS/Wayland, 0x00000109)
[darling-mldr] FATAL signal 11 (code=1) at addr=0x7fffffdedf1c
  rip=0x0000000825e96759  rsp=0x00007fffffdedd10
  backtrace (3 frames):
    #00 0x220e2f <crash_debug_handler+0x11f> at .../mldr-real/mldr
    #01 0x82307f45a <_pthread_sigmask+0x50a> at /lib/libthr.so.3
    #02 0x82307ea5b <pthread_signals_unblock_np+0x5bb> at /lib/libthr.so.3
  guest stack dump at rsp=0x00007fffffdedd10:
```

`addr` is `rsp + 0x20c` (524 bytes). `si_code=1` is `SEGV_ACCERR`: the address
is mapped and the access is not permitted — not a null or wild pointer.

The file is not the problem. The backend is present in the cache, 83,824 bytes,
and is a valid thin 64-bit Mach-O (`magic=0xfeedfacf`). So this is not "missing
library" and not "malformed image", which is what the first run's failure was.

### `rip` is not in any guest image

`decode-crash.py` attributes it to the probe:

```
rip  0x0000000825e96759  tests/wayland-window-create-macho+0x86a759   ?
```

That attribution is provably wrong and the tool's own `?` is the tell. The
probe binary is 19,048 bytes (`0x19048`); an offset of `0x86a759` is about 55x
past the end of the file. The guest images in this run sit at `0x282a4d…`
through `0x282a4f…`, and mldr's own text is at `0x220e2f`. `rip=0x825e96759` is
none of those: it is 46 MB above the nearest libthr frame, so it is in some
other host mapping entirely.

**The faulting code is host code, not guest code.** No guest frame appears in
the backtrace at all — the three frames are the crash handler and two libthr
frames of the signal path.

## 2. What dyld did immediately before the fault

Three lines, and then the log ends. Two lazy binds of libdyld, then the second
`dlopen` line, then the fault:

```
dyld: lazy bind: libdyld.dylib:0x...F4284D0 = libsystem_pthread.dylib:_pthread_getspecific
dyld: lazy bind: libdyld.dylib:0x...F4284F0 = libsystem_pthread.dylib:_pthread_setspecific
dlopen_internal(.../Wayland, 0x00000109)
```

There is **no** `dyld: Mapping` line for the backend, **no** speculative read,
**no** `dyld: loaded:` line for it, and **no** `dyld: bind` from it. The image
count in this run is 59 — the same 59 the closure walk predicted, and not one
of them is the backend.

So the load did not get as far as mapping the file. Whatever faulted, faulted
before dyld's own machinery announced the image.

## 3. The main question: how the second differs from the first

The `mode` argument is the whole difference, and this tree defines the flags:

```
src/external/dyld/include/dlfcn.h:74   RTLD_LAZY    0x1
src/external/dyld/include/dlfcn.h:76   RTLD_LOCAL   0x4
src/external/dyld/include/dlfcn.h:77   RTLD_GLOBAL  0x8
src/external/dyld/include/dlfcn.h:80   RTLD_NOLOAD  0x10
src/external/dyld/include/dlfcn.h:82   RTLD_FIRST   0x100
```

| call | mode | decode | effect |
|---|---|---|---|
| `dlopen(NULL, …)` | `0x110` | `RTLD_FIRST｜RTLD_NOLOAD` | path is NULL: returns `RTLD_MAIN_ONLY`, no load |
| backend, 1st | `0x115` | `RTLD_FIRST｜RTLD_NOLOAD｜RTLD_LOCAL｜RTLD_LAZY` | **probe only** |
| backend, 2nd | `0x109` | `RTLD_FIRST｜RTLD_GLOBAL｜RTLD_LAZY` | **real load** |

`RTLD_NOLOAD` is honoured in `src/external/dyld/src/dyldAPIs.cpp`:

```
context.dontLoad = ( (mode & RTLD_NOLOAD) != 0 );
```

and the source comments say so in words: *"RTLD_NOLOAD means dlopen should fail
unless path is already loaded. don't run initializers when RTLD_NOLOAD is set."*

The log is consistent with that reading. The first backend call is preceded by
Foundation's `CFBundleLoadExecutableAndReturnError` — a bundle-executable load,
which probes for an already-loaded binary rather than demanding one. It printed,
loaded nothing, and returned. The second call, from the guest's own code, drops
`RTLD_NOLOAD` and asks for a real load. That is the first real attempt, and it
faults.

Three things this rules out, each worth stating because they were live
hypotheses:

- **Not a double `dlopen` of an already-mapped image.** No `dyld: loaded:` line
  for the backend exists, so it was never in the image list. There is no
  duplicate-load path being taken.
- **Not a repeated ObjC class registration.** Initializers cannot have run
  twice for the backend, because they cannot have run once — the image was
  never mapped.
- **Not a `dlclose`/re-entry state problem.** No `dlclose` appears anywhere in
  the log; the run contains five `dlopen`-family calls total and no close.

## 4. Why the log stops exactly where it stops

This is the part that matters for anyone trying to read this crash again.

The log ends on the header line `guest stack dump at rsp=0x00007fffffdedd10:`
and prints **not one word** of stack. That is not an absence of stack data. It
is the diagnostic faulting while collecting it, and it took the process with
it — which is why the handler never re-raised the original signal and why there
is no "process died" line after.

The reason is a mismatch between the binary that ran and the source in the tree:

- the `mldr` that ran has these strings: `guest stack dump at rsp=0x%016llx:`
  and `backtrace (%d frames):`;
- the **working tree** does not have them. It prints `  stack dump at rsp=` and
  has no backtrace at all;
- `git log` says `guest stack dump` appears in exactly one commit, `353989015`
  (2026-09-26, "FreeBSD compat wiring for the startup path"), and in no
  earlier version of the file;
- `src/startup/mldr/freebsd_syscall_trap.c` is **modified in the working tree
  and uncommitted**, and the modification is precisely a replacement of the
  diagnostic handler: it deletes `<execinfo.h>`, deletes the
  `backtrace_symbols` block, and replaces the 80-word guest-stack walk with a
  16-word plain walk.

So the crash was reported by a handler that no longer exists in the tree, and
the tree's replacement is uncommitted. Neither state was built into the binary
that produced this log.

### Why the dump itself faults

The committed handler walks the guest stack like this:

```c
const unsigned long long base = (unsigned long long)__mldr_stack_map_base;
if (base != 0 && start < base) start = base;
volatile unsigned long long *sp = (unsigned long long *)(uintptr_t)start;
for (int i = 0; i < 80; i++)
    fprintf(stderr, "  [gstack+%4d] 0x%016llx\n", i * 8, sp[i]);
```

`__mldr_stack_map_base` is a **weak, undefined** symbol in that binary (`nm`
reports `w __mldr_stack_map_base`) and `grep` finds **no definition of it
anywhere in `src/`**. So `base == 0`, the clamp never applies, and the walk
starts at the raw `rsp` of the interrupted context and reads 80 words straight
out of it. The header printed, so the fault came on the first or an early
read — and the header has no `fflush` of its own, while the `fflush(stderr)`
sits *after* the loop, so nothing the loop had gathered survived.

**This matters for the fix, not just for the reading:** the uncommitted
replacement walks the *same* 16 words from the *same* `rsp`, so rebuilding with
the working tree would print a header and then fault just the same. The change
that is sitting in the tree does not fix this crash's diagnostics; it only
shortens the loop that was already faulting.

## 5. Hypothesis, and how to refute it from this log

**H: the fault is in dyld's first real load of the backend, before the image is
mapped** — i.e. in the path that gets from `dlopen_internal()` to a mapped
image, not in the backend's code and not in its initializers.

Supporting: the backend was never mapped (no `dyld: loaded:`, no `Mapping`), the
only difference from the working call is that this one actually loads, and the
fault is a permission fault on a stack-range address while executing host code.

**How to refute it from the log, if it is refuted:** the moment a
`dyld: Mapping …/Wayland.backend/…` line appears before the fault, the fault is
downstream of mapping and the hypothesis is wrong.

**How to test it cheaply, without a root run:** the deciding evidence was
collected and thrown away. Rebuilding `mldr` from a handler that guards its
reads — check `si_code`/fault address before dereferencing, walk with a bounded
`mincore`-style probe, or simply print `mc->mc_rsp` and the `dlopen_internal`
frame without dereferencing — would turn the next occurrence of this from a
zero-line dump into a usable one. That is a change to the crash handler, which
is exactly the code whose current state in this tree is uncommitted and
unbuilt; it needs to be decided before it is edited again.

A second hypothesis that this log cannot separate, and I am not claiming:
**H2 — the fault is in the host-side mldr rather than in dyld.** `rip` is a
host address in neither the guest range nor mldr's own text, and the backtrace
contains no guest frame, which is consistent with H2. The log has no way to
name the mapping `rip` is in: `decode-crash.py` only knows the guest images, and
it silently answered with a wrong one instead of saying "unmapped". That is
worth fixing before the next run for the same reason as above — a tool that
guesses a wrong mapping turns "where is this" into a confident wrong answer.

## Not checked, not done

- **Not** attributed. No cause is claimed. The distinguishing evidence was
  destroyed by the diagnostic before it could be written.
- **Not** reproduced, and no root was spent on this task.
- **Not** verified: which host mapping `0x825e96759` is in; whether the fault is
  in dyld (H) or in the host mldr (H2); whether `__mldr_stack_map_base` was
  meant to be defined and the definition was lost.
- **Not** touched: `dyld`, the overlay, the staging path, the crash handler.
  The uncommitted working-tree modification of `freebsd_syscall_trap.c` was
  left exactly as found and is reported, not resolved — it is not mine and it
  is not something to overwrite.
- `decode-crash.py` reported `58 images mapped (+1 pseudo from mldr DEBUG)`
  where the log's own count is 59, and attributed `rip` outside every file it
  considered. Both numbers are recorded here as discrepancies, not corrected.
