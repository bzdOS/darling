# STACK-LIMIT — the guest stack is sized by the rlimit, and run №4 does not crash

Base: `pr-arm64` = `ea397bbe` (the merged `task/probe-run-three`).
One root run, the one this task is about.

**Short answer: `dyld::loadPhase6`'s 33 KB frame fits, the loader boots to
completion, the guest runs, and the run now stops on a plain `nil` in
`-[NSBundle principalClass]` instead of a SIGSEGV. The crash that
`PROBE-RUN-THREE.md` ended on is gone, and `mh = 0x…` for dyld is now a
measurement instead of three agreeing constants.**

## 1. What changed

`src/startup/mldr/mldr.c`, `setup_space()` — the guest stack was 16 pages,
always, unless `RLIMIT_STACK` was *smaller*:

```c
unsigned long size = PAGE_SIZE * 16;
if (limit.rlim_cur != RLIM_INFINITY && limit.rlim_cur < size) {
        size = limit.rlim_cur;
}
```

which does the opposite of the comment sitting above it ("otherwise, allocate
the limit"): on a host with the usual 8 MiB limit the guest still got 64 KiB.
`dyld::loadPhase6` declares `firstPages[0x8220]` on that stack
(`src/external/dyld/src/ImageLoader.h:102`) and run №3 faulted on its first
argument spill, 7,860 bytes below the bottom.

The rule now: **honour a larger limit, never go below 16 pages, never exceed a
cap.**

```c
const unsigned long min_stack = PAGE_SIZE * 16;
const unsigned long max_stack = 8UL * 1024 * 1024;
unsigned long size = min_stack;

if (limit.rlim_cur != RLIM_INFINITY) {
        if (limit.rlim_cur > size)
                size = (unsigned long) limit.rlim_cur;
        if (limit.rlim_cur < min_stack) { /* WARNING, keeps the old floor */ }
}
if (size > max_stack)
        size = max_stack;
lr->stack_size_mapped = size;
```

A limit below the floor is no longer a silent override: it prints a WARNING
naming the limit, the floor and the value used, because a small
`RLIMIT_STACK` reproduces exactly the crash this change exists to remove and
has to be findable in the log when it does.

**Why 8 MiB is the cap.** It is what a Darwin main thread actually gets, and
this is a Darwin compatibility layer: a guest written against macOS is
entitled to assume 8 MiB, and giving it less is the bug above. It is also the
default `RLIMIT_STACK` on both hosts, so in the ordinary case the *limit*
decides and the cap is only the backstop for `RLIM_INFINITY` or a hand-raised
limit. And 8 MiB bounds the `MAP_FIXED` mapping placed just below the
commpage, which is the only address available: guest images in a real run sit
at `0x2C944400000`, more than 4 TB below the commpage, so a cap of this size
can reach neither them nor the commpage.

**Why the range is probed before it is taken.** The stack has to go at a
fixed address — there is no other one below the commpage — and `MAP_FIXED`
unmaps whatever it finds without asking. At 16 pages the risk was small; at
8 MiB it is not, and a clobbered host mapping surfaces much later and
elsewhere. `mincore()` answers "is this page resident" without touching it: a
resident page belongs to somebody, a non-resident one is free to map. If
anything in the range is resident the run stops with the offset and does *not*
unmap — an arbitrary smaller stack would be a second way to reproduce the
crash being removed.

The two hosts spell "not resident" differently and both mean *free*: Linux
answers `0` with the bit clear, the BSDs answer `-1`/`ENOMEM` for a range with
no `vm_entry` under it. Only another `errno` fails the probe.

Two fields were added to `struct load_results` (`loader.h`) and are now
printed before the guest starts:

- `dyld_mh` — where dyld's own mach header was mapped, set in `loader.c` at the
  dylinker mapping. It is not derivable from anything else in this struct:
  `dyld_all_image_location` is the address of `dyld_all_image_infos`, which
  lives *inside* the image, and `mh` is recorded only for `MH_EXECUTE`. Without
  it a fault *inside* dyld cannot be turned into an offset in it.
- `stack_size_mapped` — the size that actually got mapped, as opposed to
  `stack_size`, which is the `LC_MAIN` request. A crash log needs it to tell a
  stack overflow from anything else.

## 2. The new DEBUG lines

```
[darling-mldr] stack region 0x7fffff600000..0x7fffffe00000 (8388608 bytes) is free (no vm entry under it at all)
[darling-mldr] DEBUG dyld mh=0x828ef7000
[darling-mldr] DEBUG pre-start: mh=0x8275f1000 entry=0x828ef8000 stack_top=0x7fffffdffdd0 stack_size=0x800000
```

`entry - dyld_mh = 0x1000` — exactly the relation `PROBE-RUN-THREE.md §7` had
to *infer* from three agreeing constants, and this run measures it. Run №4a of
this task measured the same `0x1000` (`mh=0x826ed4000`, `entry=0x826ed5000`),
so it is not a coincidence of one run.

That line is what `decode-crash.py` was already waiting for: `add_pseudo_segments()`
greps for `DEBUG dyld mh=` and builds a pseudo-segment for `/usr/lib/dyld` out
of it. Fed this log, the tool now places run №3's fault on its own:

```
$ python3 - <<'PY'   # decode-crash.py's own load_segments/add_pseudo_segments
segments: 61 real, 1 pseudo appended
resolve(dyld_mh + 0x6759) ->
    /usr/lib/dyld+0x6759   __ZN4dyldL10loadPhase6EiRK4statPKcRKNS_11LoadContextE+0x19
```

which is the attribution run №3 reached by hand. The pseudo-segment's length
comes from the dyld file's own segment table, so it is derived, not invented.

## 3. The run

```
sh build-freebsd/build-mldr-only.sh                              # [100%] Built target mldr, installed
sh build-freebsd/crash-dump-selftest.sh                          # RC=0, "PASS: the dump cannot take the process with it"
DRY_RUN=1 sh build-freebsd/run-wayland-window-probe.sh           # RC=0, preflight: 8 check(s) passed
sh build-freebsd/run-wayland-window-probe.sh                     # the one root run
python3 build-freebsd/decode-crash.py "$DARLING_BUILD_DIR/wayland-window-probe.log" "$DARLING_OVERLAY" tests
python3 build-freebsd/attribution-from-log.py "$DARLING_BUILD_DIR/wayland-window-probe.log" --recompute
```

`decode-crash.py`: **`no crash found in log`** — there is no `FATAL signal` in
157,311 lines. `attribution-from-log.py --recompute`: `PASS: 0 disagreement(s)`
(the baked `trieWalk` table still matches the binary; the trie hypothesis is
untouched by this change, and still not the fault).

Where the run stops now:

```
[step 01] dyld: lazy bind: wayland-window-create-macho:0x8275F4078 = libsystem_c.dylib:_vprintf …
[step 02] bundle path = /System/Library/Frameworks/AppKit.framework/…/Wayland.backend
  dlopen_internal(…/Wayland.backend/Contents/MacOS/Wayland) ==> 0x7ff7b8280e91
         bundle loaded=1
         principalClass=(nil)
[step 03] FATAL: bundle has no NSPrincipalClass
```

The loader got all the way through: dyld booted, every image bound, the
`Wayland.backend` bundle `dlopen`ed, the guest's own `main` ran and reached
step 01. That is the milestone this task was gated on — but **not** the window:
the run stops *before* `wl_display_connect`, so nothing was drawn and there is
no screenshot.

The plist is not the problem:

```
$DARLING_OVERLAY/System/Library/Frameworks/AppKit.framework/Versions/C/Resources/Backends/Wayland.backend/Contents/Info.plist:
        <key>NSPrincipalClass</key>
        <string>WaylandDisplay</string>
```

so `-[NSBundle principalClass]` returning nil for a bundle that has it is the
next wall, and it is a Foundation/NSBundle question, not a dyld one.

## 4. A bug this run found in its own new code

Worth recording because the first version of the probe **reported a false
pass**. The loop treated any `mincore()` failure as "occupied", and then used
`occupied_at == 0` to mean "clean" — but a failure on the *first* page also
leaves `occupied_at == 0`, so `ENOMEM` on page 0 printed `is free`. The fixed
version distinguishes the two: `ENOMEM` means unmapped means free, and the
message says so (`no vm entry under it at all`). Run №4a — the one whose output
above first said `is free` — therefore proved nothing, and its result is kept
only because run №4b re-ran it with the corrected binary.

## 5. Not verified, not done

- **Not verified** that the window ever appears. `principalClass` is nil, so
  nothing reached `wl_display_connect`/`shm`; the removal of the crash reveals
  the next failure, it does not deliver a window.
- **Not verified** on Linux. The `ENOMEM` handling is written for both hosts
  and the Linux side of it (Linux answers `0`) is the branch that was *not*
  exercised here; this machine only ran the FreeBSD one.
- **Not fixed** the `principalClass` failure, and not diagnosed it. It is the
  next thing to look at.
- **Not touched:** dyld sources, the overlay (including the plist above), the
  staging path, the probe binary, `crash_dump.c`.
- **Not reproduced outside this machine**, and only two root runs were spent on
  this change (`4a` discarded per §4, `4b` the one above).
- The `stack region … is free` line is a per-boot check on a host whose commpage
  sits where it sits. On a host that maps something resident below the commpage
  it will now *stop the boot* instead of clobbering it — intended, but it means
  this check is a new way for a guest not to start.
