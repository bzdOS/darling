# Attribution of the window-probe run: not a trieWalk fault

One root run of the window probe was authorized and spent. It did not reach
the shm buffer. This report says what actually stopped it, from the run log
alone, and what that does and does not mean for the next front.

The short version: **there was no fault.** dyld did not crash. It refused a
load, cleanly, with an `ENOENT`, and the run is the very first time this
milestone has produced a log whose fault is *not* `trieWalk`.

## What was run

Preflight, no root, expected 6/6:

```sh
export DARLING_SRC_DIR=<src> DARLING_BUILD_DIR=<build> DARLING_OVERLAY=<overlay>
cd "$DARLING_SRC_DIR" && DRY_RUN=1 sh build-freebsd/run-wayland-window-probe.sh
```

Result: 6/6 PASS — sway alive, conjure-wayland-input built, probe binary
present, 8/8 dylib dependencies at a high enough compat version, probe is an
x86_64 EXECUTE with LC_MAIN and no `@rpath`, and the vendored Wayland backend
hash `a4797cdd…` matches vendored, overlay and the committed hash.

The combat run, default `WAIT_SECS`, log at `$DARLING_BUILD_DIR/wayland-window-probe.log`:

```sh
sudo env DARLING_BUILD_DIR=<build> DARLING_OVERLAY=<overlay> \
  sh build-freebsd/run-wayland-window-probe.sh
```

Result: preflight 6/6 again, seat raised to `capabilities=3`, then step 6
failed — "the run did not reach the shm buffer". Log: 443 lines, 42531 bytes.
49 images loaded before the failure.

## VERDICT

dyld aborted because it could not `stat()`
`/System/Library/PrivateFrameworks/Onyx2D.framework/Versions/A/Onyx2D`, which
AppKit links. The guest's `DYLD_ROOT_PATH` was the local staging cache, and
that cache never gets `System/Library/PrivateFrameworks` staged into it.

The offending lines, verbatim from the log:

```
dyld: loaded: <4C4C44D9-…> /System/Library/Frameworks/LaunchServices.framework/Versions/A/LaunchServices
dyld: Library not loaded: /System/Library/PrivateFrameworks/Onyx2D.framework/Versions/A/Onyx2D
  Referenced from: /System/Library/Frameworks/AppKit.framework/Versions/C/AppKit
  Reason: image not found
abort_with_payload: reason: dyld: No shared cache present
```

## The chain, step by step, each step checked

**1. `Reason: image not found` means exactly one thing: `stat()` returned
`ENOENT`.** This is worth pinning down before anything else, because the
phrase reads like "the file is not a valid image" and it is not. In
`src/external/dyld/src/dyld2.cpp`, `loadPhase5load()` does
`existsOnDisk = (dyld3::stat(path, &statBuf) == 0)` (line 3645) and then
records an exception **only if the errno is neither `ENOENT` nor 0** (lines
3668-3673). An `ENOENT` therefore returns NULL *silently*. Back in `load()`,
that lands on `else if (exceptions.size() == 0)` → `throw "image not found"`
(lines 4152-4157), which `ImageLoader.cpp:820` re-wraps into the three lines
quoted above. So: dyld never even opened the file, and a malformed Mach-O, a
compat-version rejection or an export-trie problem would all have produced
*different* text. The phrase is a pure `ENOENT`.

**2. The guest's root path was the staging cache, not the overlay.** The log
carries dyld's own environment dump (`DYLD_PRINT_ENV=1`), so this is
attested by the loader itself, not inferred:

```
opt[0] = "/wayland-window-create-macho"
DYLD_ROOT_PATH=/tmp/darling-local-overlay
```

`loadPhase0()` prepends `DYLD_ROOT_PATH` to any absolute path
(dyld2.cpp:4071-4083), so that is the prefix the failing `stat()` used.

**3. The cache has Frameworks and no PrivateFrameworks.**

```
$ ls -d $LOCAL/System/Library/*/      ->  $LOCAL/System/Library/Frameworks/     (60 entries)
$ ls -d $OVERLAY/System/Library/*/
   DirectoryServices/  Fonts/  Frameworks/  LaunchAgents/  LaunchDaemons/
   OpenSSL/  PrivateFrameworks/  Security/  User Template/
```

**4. The file dyld asked for does exist — in the overlay.**

```
PRESENT  $OVERLAY/System/Library/PrivateFrameworks/Onyx2D.framework/Versions/A/Onyx2D (1026912 bytes)
ABSENT   $LOCAL/System/Library/PrivateFrameworks/Onyx2D.framework/Versions/A/Onyx2D
```

It is a real image, not a stub: `MH_MAGIC_64`, `X86_64`, filetype `DYLIB`,
23 load commands, `__text` of 0x5b800 bytes. So the overlay is not missing
anything; the cache is.

**5. Something must ask for it.** AppKit's `LC_LOAD_DYLIB` list contains
`/System/Library/PrivateFrameworks/Onyx2D.framework/Versions/A/Onyx2D`
(compat 0.0.0/0.0.0), and it is the one entry in that list outside
`Frameworks/` and `usr/lib/`. Those two *are* staged, which is why the 49
images before it all loaded and the failure landed exactly here.

**6. Who stages what.** `tests/launch-dynamic-smoke.c` builds the local cache
from exactly three trees:

| tree | line |
|---|---|
| `usr/lib` (whole, ~80MB) | 200-202 |
| `System/Library/Frameworks` | 227-230 |
| `private/etc` | 248-251 |

`System/Library/PrivateFrameworks` is not in that list, and the three blocks
are not table-driven, so nothing picks it up by accident. The file's own
comment at lines 182-186 predicts this exact failure mode for the closure it
*does* copy: "every miss surfaces as an opaque `image not found` from dyld."
This is that sentence, one directory out.

## Why the preflight could not have caught it

Step 3b runs `check-guest-dylib-compat.py` and it passed, 8/8. Those 8 are
the **probe's own** `LC_LOAD_DYLIB` entries: Foundation, CoreFoundation,
AppKit, libobjc, libicucore, libc++, libc++abi, libSystem.B. Onyx2D is not
among them — it is a dependency *of AppKit*, one level down. The preflight
validates the first ring; the fault is in the second. A closure walk (re-export
chains included — the tool for that already exists in this directory) would
have turned a spent root run into a preflight failure.

## `--recompute` cross-check

Required, and it passes, so the address map is sound even though this run
does not need it:

```
$ python3 build-freebsd/attribution-from-log.py --recompute
dyld            : $OVERLAY/usr/lib/dyld
slice           : fat slice at offset 0x276000 size 2641232
slice sha256    : 23dfaa85011f89d07abba455ae5905d8b161492cd2344912097438811843eac0
trieWalk entry  : 0x376a0 (baked table says 0x376a0)
ok : line 1837 +0x45    movzbl (%rax),%eax  after p is bumped
ok : line 1841 +0x6b    callq read_uleb128
ok : line 1847 +0xa4    movq %rax,-0x38(%rbp)  then cmpq -0x18(%rbp),%rax
ok : line 1848 +0xac    cmpq -0x18(%rbp),%rax / jbe
ok : line 1849 +0xba    leaq of the complaint string
ok : line 1853 +0xe4    movb (%rax),%al  after children is bumped
PASS: 0 disagreement(s)
```

All six anchors re-derived by instruction shape from the dyld actually in the
overlay. `--self-test` also passes, 3/3.

On the fat-dylib trap flagged for this task: **it did not fire here.** The
`dyld` in the overlay is a fat binary, and `--recompute` handles that
correctly because the slice-1 method extracts the x86_64 slice first and
disassembles that. The false `VERDICT: FAIL` belongs to the offline chain
resolvery's `_resolve_chain` deep-lookup path, which this run never invoked.
Worth recording that the check is clean rather than assumed.

## The image in flight: a proxy, and here an actively misleading one

The tool reports the last `dyld: loaded:` line before the failure:

```
last image loaded : /System/Library/Frameworks/LaunchServices.framework/Versions/A/LaunchServices
```

That is the tool's documented proxy, not a conclusion, and the tool says so.
It should be marked as weaker than usual here, for a specific reason: in a
`ENOENT` refusal the loader never begins the failing image at all. The last
successful load says only where the *dependency walk* got to — LaunchServices
is simply the previous entry in AppKit's list. Attributing this to
LaunchServices would be wrong, and the "image in flight" question this task
asked has, correctly, **no answer**: nothing was in flight.

For the same reason the tool's own advice on this log — "need the mldr
backtrace" — does not apply. There is no backtrace to get; the process exited
through `abort_with_payload`, not a signal.

What the tool says about the absence of the loader's bounds complaint is
right but not load-bearing: the absence of
`trieWalk() malformed trie node` does rule out the guard at
`ImageLoader.cpp:1848`, and it is evidence rather than absence of evidence —
but here it is not evidence of an unchecked read either, because no trie walk
was in progress. The lines 1837 / 1862 / 1876 named in the task are ruled
out, positively, by the absence of any signal at all.

## Resolved discrepancy — the root path, and who sets it

An earlier draft of this section left the root path open: read one way, the
source says every site sets the overlay, while the log says the staging
cache. That is a misreading of the source. The two agree, and the code
settles it.

`od = LOCAL_OVERLAY;` is at `tests/launch-dynamic-smoke.c:380`. It is **not**
inside `if (strcmp(test_bin, "chrome-macho") == 0)` (line 279) — that branch
closes at line **377**. Line 380 stands in the *unconditional* block whose
bare brace is at line **168**, so it runs for every test binary, this probe
included. The probe is `wayland-window-create-macho`, not `chrome-macho`, and
the reassignment happens regardless.

That block is the local staging copy, and its motive is in the comment at
lines 162-167: the overlay sits on a virtiofs/9p mount where FreeBSD's driver
returns `BUS_OBJERR` on page-fault reads from mmap'd files, and where a
malformed `FUSE_READLINK` reply fails every symlink walk with `EIO` while
leaking one `fuse_msgbuf` per step until the guest OOMs (lines 191-199). So
the overlay is mirrored to local tmpfs for the run, and the comment gives the
reason the paths are mirrored exactly: "so `__mldr_DYLD_ROOT_PATH` still
works". The cache is the intended root, not an accident.

Every consumer of `od` runs after line 380, so each one reads the cache:

- `tests/launch-dynamic-smoke.c:405` — `setenv("DARLING_VCHROOT_PATH", od, 1)`,
  the darlingserver vchroot. Set to the cache.
- `tests/launch-dynamic-smoke.c:453` — `setenv("__mldr_DYLD_ROOT_PATH", od, 1)`.
  Set to the cache, and this is the value the log prints.
- `src/startup/mldr/mldr.c:269-273` renames `__mldr_DYLD_ROOT_PATH` to
  `DYLD_ROOT_PATH`; `mldr.c:599-606` takes it as `lr->root_path`. The
  `vchroot_path` fallback at `mldr.c:1058-1073` yields the same string, so
  dyld receives the cache on either branch.

So `DYLD_ROOT_PATH=/tmp/darling-local-overlay` is exactly what this harness
is written to produce. **Nothing is unresolved here.** The alternative
reading — the mldr under test is not the source above — is dead, and so is
the idea that the honest fix might be in the root-path logic: that logic is
correct and deliberate. The root path is not the defect. The staging list is.

## What this means for the next front

1. The window probe is further from the wall than it was, and further from
   dyld. There is no `trieWalk` evidence in this log at all. The dyld line
   this milestone was built to repair is **not** implicated by this run, and
   should not be repaired on the strength of it.
2. The blocker is now the host-side harness and a preflight gap, both
   inspectable without spending another root run:
   - make the staging list cover what the closure actually demands, or derive
     it from the closure instead of hardcoding three trees;
   - extend preflight 3b from the probe's direct deps to the transitive
     closure, so this class of miss is caught before a root prompt.
3. Only after 3b walks the closure is a second root run worth spending, and
   it should be spent on a run that can plausibly reach the shm buffer.

## Not checked, not done

Stated plainly so nothing here is over-read:

- **Not** reproduced a second time. One root run was authorized and one was
  spent; no repeat.
- **Not** fixed. The on-screen defect (a missing directory in a staging list)
  is left in place, and `dyld` and the overlay were not touched, per the task.
- **Not** verified: whether the closure has any *further* unstaged dependency
  behind Onyx2D. The run stopped at the first one, so anything Onyx2D itself
  needs is unknown — its own `LC_LOAD_DYLIB` list was not walked.
- **Not** checked: whether the 49 images that did load are correct, only that
  they loaded.
- The full log stays at `$DARLING_BUILD_DIR/wayland-window-probe.log`, outside
  the source tree, and is not committed: it is a machine-local artefact full
  of addresses and paths.
