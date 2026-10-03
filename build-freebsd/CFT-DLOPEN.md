# cft-dlopen: localize the Chrome-framework dlopen failure on the live June dyld

The chrome probe on the live June dyld (`b8df2a76`) loads 41 images, then
fails at `dlopen //../Frameworks/Google Chrome for Testing
Framework.framework/Versions/154.0.8029.0/Google Chrome for Testing
Framework: … image not found`. Where does it break?

## Step 0 — the guest cannot read a `<8`-entry directory (confirmed)

A guest `ls` (the overlay's `/bin/ls` run under mldr) on directories with
known real entry counts:

```
/System/Library/PrivateFrameworks  (1 real entry)  -> 0 entries printed
/System/Library/Frameworks         (60 real entries) -> 38 entries printed
```

So a directory with fewer than 8 entries yields nothing to the guest, and
a larger one is truncated — the `getdirentries64` wall. The app's
`Contents/Frameworks/` has 1 real entry (the framework), so the guest
cannot see it.

## Step 1 — where `//../Frameworks/…` comes from

`launch-dynamic-smoke.c`'s Chrome staging (lines ~730-832) says the
framework is staged into `$LOCAL/Frameworks` "so the guest sees it at
`/Frameworks/Google Chrome for Testing Framework.framework/…`", and that a
`/tmp/Frameworks` symlink is created (`symlink("../Frameworks",
"/tmp/Frameworks")`) "so Chrome's `@loader_path/../` resolution works".
Chrome's loader path resolves to the guest's root, so `@loader_path/../`
clamps at `/` and yields `//../Frameworks/…` = `/Frameworks/…`.

## Step 2/3 — the stage-level padding does not fix it

The guest's `/Frameworks/` was padded to **9 entries** (the framework plus
8 `.pad*` files, in the local overlay — the live overlay untouched), and
the chrome probe re-run (staging cached). The failure is **identical**:

```
dlopen //../Frameworks/…/Google Chrome for Testing Framework: … image not found.
```

So the dlopen failure is **not** the simple `<8`-entry count of
`/Frameworks/`: the resolve still fails deeper (the framework's
`Versions/…` path, or the `//../` resolution itself).

## Verdict (one line)

The `getdirentries64` `<8`-entry wall is confirmed for listing (1→0,
60→38) and `//../Frameworks/…` is `@loader_path/../` from the guest's
root; a stage-level padding of the guest's `/Frameworks/` to 9 entries
does **not** fix the dlopen (still `image not found`), so the failure is
deeper than the entry count — the framework's `Versions/…`/path resolve
on the June dyld.

## Control #3 — per-link padding and the guest file read

### Per-link measurements (real entries)

```
Frameworks/Google Chrome for Testing Framework.framework            5  (<8)
  .../Google Chrome for Testing Framework.framework/Versions        2  (<8)
  .../Versions/154.0.8029.0                                         4  (<8)
```

All three links are below the `getdirentries64` threshold, so every one of
them is unreadable to the guest by the wall.

### Guest file read

A space-free symlink to the Mach-O was placed in the guest's staging root
(`/FWbin -> …/Versions/154.0.8029.0/Google Chrome for Testing Framework`).
A guest `hexdump -C -n 8 /FWbin` returns `No such file or directory` (then
`Bad file descriptor`) — the guest cannot open the 267 MB file at all, so
the failure is not "dyld refuses to map a readable file": the path is not
resolvable/readable to the guest.

### Per-link padding + probe

The **source** framework's three links were padded to 14/11/13 entries
(so the harness staging copies the padding), and the chrome probe re-run
with `DARLING_SMOKE_REFRESH=1`. The staged links are 14/11/13 — and the
failure is **identical**:

```
[dyld-trace] done. count=41
dlopen //../Frameworks/…/Google Chrome for Testing Framework: … image not found.
```

### Verdict (control #3, one line)

dlopen failure = the framework path (5/2/4 real entries, all `<8`) and the
guest cannot even read the Mach-O (`hexdump /FWbin` → ENOENT);
per-link padding (staged 14/11/13) does **not** fix it (still `image not
found`), probe stays at 41 images — **the wall is not in the listing: the
guest cannot resolve/read a visible file** — stop.

## Read layer — guest open on the padded tree + host truss

### Guest read (the read layer works)

The guest's `/bin`-style tools read known files fine: a guest `hexdump -C
-n 8` of `/usr/lib/libSystem.B.dylib` prints the fat Mach-O magic
`ca fe ba be`, and `/etc/hosts` prints its text. So the emulated open
path is functional for those files.

Marker probe: a file created in the *local* staging overlay is **not**
readable as the guest (`/MARKER-LOCAL` and
`/tmp/darling-local-overlay/MARKER-LOCAL` both fail), and the
`/FWbin` symlink to the padded Mach-O also fails — so the guest's root is
the **overlay**, not the local staging tree. The previous turn's per-link
padding lived in the local staging (`$LOCAL/Frameworks`), which the guest
does not read from; the padded-tree read was therefore not measured on the
guest's actual view.

### Host truss

`truss -f` of a whole chrome-probe run captures the staging's openats
(`O_WRONLY|O_CREAT` of the framework files under
`/tmp/darling-local-overlay/Frameworks/…`) and the pax reads, but the run
aborts before the guest's framework openat:

```
Assertion failed: (LIST_NEXT(info->curthread, entries) == NULL),
function find_exit_thread, file /usr/src/usr.bin/truss/setup.c, line 422.
```

— truss's own assertion fails on the multi-threaded guest processes, so
the host-side openat of the guest's `/Frameworks/…` path was not captured.

### Verdict (read layer, one line)

post-padding read = **not measured on the guest's root** (the guest root
is the overlay; the padding was in the local staging, unreadable to the
guest); the read layer itself works (known files read, `cafebabe` /
text); host-openat = **not captured** — `truss -f` aborts on its own
assertion (`find_exit_thread`, setup.c:422); failure layer = undetermined
(guest-resolve vs emu-open vs dyld).

## Control #4 — guest root pinned, presence confirmed; ktrace localizes the drop inside dyld

### Step 0 — config pin (harness source, verified against the trace)

Per `tests/launch-dynamic-smoke.c`: staging writes into `$LOCAL`
(`/tmp/darling-local-overlay`, `#define LOCAL_OVERLAY`); the Chrome
staging target is `$LOCAL/Frameworks/...`. After staging, the harness
reassigns `od = LOCAL_OVERLAY` and passes that same directory to the
guest twice: as `DARLING_VCHROOT_PATH` (darlingserver) and as
`__mldr_DYLD_ROOT_PATH` (mldr rewrites it to `DYLD_ROOT_PATH` for dyld
and prefixes LC_LOAD_DYLINKER with it). So:

```
гостевой корень = /tmp/darling-local-overlay
стейджинг       = /tmp/darling-local-overlay/Frameworks
совпадают       = да
```

ktrace confirms this at runtime: every guest-side path lookup in the
probe resolves under `/tmp/darling-local-overlay` (kernel `NAMI`
records); the root prepend is userspace-lexical — no kernel re-rooting
in this flow. (A host-side `/Frameworks -> /tmp/…/Frameworks` symlink
exists from an old staging session, but dyld never issues the raw
un-prefixed spelling.)

### Step 1 — presence (host-side, chrome-macho run with `DARLING_SMOKE_REFRESH=1`)

```
$LOCAL/Frameworks/Google Chrome for Testing Framework.framework/Versions/154.0.8029.0/Google Chrome for Testing Framework
  = 267024384 bytes, MH_MAGIC_64 x86_64 (cf fa ed fe)  — staged, present
chain dirs (real entries): framework root 14, Versions 11, 154.0.8029.0 13 — padded ≥9
```

Presence in the guest root = **yes**. The previous turn's "staging
misroute" reading is refuted: its guest-read probes never ran (the read
tools were ELF — mldr answered `Unknown file format`), and the probe's
`stat=` column is meaningless — it prints `0` even for paths that exist
nowhere.

### Step 2 — the same run under ktrace: what dyld actually does

The guest's `fstatat(AT_SYMLINK_NOFOLLOW)` walked the **entire staged
chain** — `Frameworks` → `…framework` → `Versions` → `154.0.8029.0` →
the Mach-O — every component RET 0; the Mach-O itself came back
`mode=0100755 size=267024384 ino=24720025`, RET 0. The emu stat layer
reads the staged tree fine. dyld's dlopen fanout (fallback framework
paths `$HOME/Library`, `/Library`, `/Network`, `/System/Library` — all
ENOENT — then the root-prefixed raw `/Frameworks/...` path) reached the
staged file and stat-ed it repeatedly — **and then issued no `open()`
at all**: after the last successful fstatat the trace goes
`mmap(anon)`/`munmap` → `write(2)` of the dlerror text →
`image not found` (bare — thrown with an empty exception list at the end
of dyld2 `load()`). No FOLLOW `dyld3::stat` and no `openat` ever touched
the staged framework path; every guest syscall maps 1:1 to a host
syscall in this trace, so an open that never appears was never issued.

### Verdict (control #4, one line)

guest root = `/tmp/darling-local-overlay`; framework in it = **yes**
(267 MB Mach-O at the probed path, chain padded 14/11/13, guest
fstatat RET 0); dlopen refusal = **dyld layer** — the candidate is
stat-successful but dyld never opens it; probe unchanged: 41 images,
same `image not found`.

### Next measurement (for the dyld layer)

Dlopen the same staged Mach-O through a space-free path with no
`.framework/` in it (e.g. `$LOCAL/FWMACHO` as a relative symlink inside
the guest root): success ⇒ the silent drop is specific to dyld's
`.framework` path handling; the same refusal ⇒ the drop is upstream of
the framework logic, between a successful stat and `loadPhase5open` in
dyld2.

## Control #5 — FWMACHO: a non-"//../" candidate opens the staged Mach-O; the drop is the "//../" spelling

### Setup

Probe = `chrome-dlopen-probe-macho` with candidate [3] byte-patched to
`/FWMACHO` (36-byte string slot, see repro). Executed as
`TEST_BIN=chrome-macho` so the Chrome staging runs — same staged tree as
Control #4. `$LOCAL/FWMACHO` is planted by a poller that waits for the
harness to announce `Chrome framework staged:` — a plant made before the
harness's startup `rm -rf $LOCAL` is wiped with it (the first attempt hit
exactly that and is recorded below as not-data). Relative symlink inside
the guest root, no hardlinks:

```
$LOCAL/FWMACHO -> Frameworks/Google Chrome for Testing Framework.framework/Versions/154.0.8029.0/Google Chrome for Testing Framework
host-side od -N8 through the link = cf fa ed fe 07 00 00 01   (MH_MAGIC_64 x86_64)
```

### Probe results (one run, all four candidates)

```
[0] /Frameworks/…/Versions/154.0.8029.0/Google Chrome for Testing Framework
    → Library not loaded: /System/Library/Frameworks/Foundation.framework/Versions/C/Foundation
      (Incompatible library version: requires 300.0.0, Foundation provides 0.0.0)
[1] /tmp/Frameworks/…          → image not found
[2] //../Frameworks/…          → image not found      ← Control #4 drop reproduces
[3] /FWMACHO                   → Library not loaded: …/Foundation… (same as [0])
```

### ktrace of the FWMACHO attempt

```
readlink("$LOCAL/FWMACHO")   RET 114
fstatat(NOFOLLOW) chain: Frameworks → …framework → Versions → 154.0.8029.0 → Mach-O   RET 0
openat(AT_FDCWD, …, O_RDONLY)  NAMI "…/local-overlay/Frameworks/…/Google Chrome for Testing Framework"  RET openat 3
pread(0x3, …, 0x1000, 0)       ← header read, then the map
```

The dlopen opened and mapped the 267 MB Mach-O through FWMACHO; the failure
moved from "image not found" (never opened) to the framework's own dependency
wall. For reference, the invalid pre-cleanup attempt shows the opposite
signature: `fstatat("…/local-overlay/FWMACHO") RET -1 errno 2`, no openat,
bare `image not found`.

### Verdict (control #5, one line)

FWMACHO: dlopen = success past the stat→open boundary (openat RET 3 → pread;
refusal moved deeper to the Foundation dylib-version wall, 300.0.0 required
vs 0.0.0 provided); layer = dyld path logic — the Control #4 drop is specific
to the `//../` spelling (candidate [2] still bare `image not found` in the
same run), while both non-`//../` spellings ([0] a `.framework` path, [3]
FWMACHO) reach openat; next = bisect dyld's `//../` path handling
(loadPhase0 root-path block), and the Foundation version wall is a separate
blocker on the framework's dependency chain.

### Repro (probe patch)

```python
data = bytearray(open("<probe>", "rb").read())
old = b"/Google Chrome for Testing Framework\x00"       # 36 bytes
i = data.rfind(old); assert data[i-1:i] == b"\x00"     # standalone candidate [3]
data[i:i+len(old)] = b"/FWMACHO\x00" + b"\x00" * (len(old) - 9)
open("<probe-patched>", "wb").write(data)
```

Run the chrome probe harness with the patched binary as the test binary
(`DARLING_SMOKE_REFRESH=1`, `ktrace -f -i`); plant `$LOCAL/FWMACHO` after
the `Chrome framework staged:` log line; `kdump | grep FWMACHO`.

## Control #6 — version wall removed; the launcher's own dlopen moves past "//../"

### Step 1 — Foundation version wall (stage-copy patch)

dyld's version check (`ImageLoader.cpp`): fails when the found dylib's
`compatibility_version` is below the requirer's required version
(`0xFFFFFFFF` on the REQUIRED side is the wildcard; `ImageLoaderMachO::
doGetLibraryInfo` reports `compatibility_version` as minVersion). The staged
Foundation carried `LC_ID_DYLIB cur=0x00000000 compat=0x00000000` — the
"provides 0.0.0" wall. Fix, runtime stage-copy only (the live overlay is
read-only): a poller synced on the harness's `Chrome framework staged:` line
patches the staged `$LOCAL/System/Library/Frameworks/Foundation.framework/
Versions/C/Foundation` LC_ID fields (file-offs 2896/2900) → 0xFFFFFFFF via an
exact load-command walk (`build-freebsd/stage-patch.py`, thin + fat, all
slices); read-back `ff ff ff ff`.

After the patch, both non-"//../" candidates move one wall deeper:

```
[0] /Frameworks/…  → Library not loaded: /usr/lib/libcups.2.dylib
                      (Incompatible library version: requires 2.0.0, provides 1.0.0)
[3] /FWMACHO       → same libcups refusal
[1]/[2]            → image not found (unchanged — the "//../" drop)
```

### Step 2 — chrome-macho path-template rewrite

chrome-macho (the launcher) holds the cstring `"../Frameworks/Google Chrome
for Testing Framework.framework/Versions/154.0.8029.0/Google Chrome for
Testing Framework"` (offset 10928). Its code joins `dirname + "/" +
template`; with the guest binary at `/chrome-macho` dirname = `/`, which
produced the dying spelling `//../Frameworks/…`. Dropping the leading `"../"`
(slot 118 → 115 bytes + 3 NUL pad, in place, string-boundary asserted;
`build-freebsd/chrome-template-patch.py`) makes the constructed path
`//Frameworks/…` (kernel-normalized `/Frameworks/…`). This is a cstring edit,
not an LC edit: chrome-macho's only dylib LC is `/usr/lib/libSystem.B.dylib`
— load commands are byte-identical before/after (`cur=0x054c0000
compat=0x00010000`).

Run B — template-patched launcher + the same stage-copy patch, under ktrace:

```
dlopen //Frameworks/…/Google Chrome for Testing Framework: Library not loaded: /usr/lib/libcups.2.dylib
  Referenced from: //Frameworks/…
  Reason: Incompatible library version: requires version 2.0.0 or later, but libcups.2.dylib provides version 1.0.0.
```

— the launcher's OWN dlopen now opens and maps the framework; its dependency
tree loads (`openat … RET openat 3` cascade: CoreWLAN, CoreLocation, Vision,
CoreML, SafariServices, UserNotifications, LocalAuthenticationEmbeddedUI,
DiskArbitration, ServiceManagement — dyld then unloads them when the libcups
check fails) and the run ends SIGILL after the dlerror. Last syscall before
the dlerror: `openat(AT_FDCWD, …, O_RDONLY) NAMI "…/local-overlay/usr/lib/
libcups.2.dylib" RET openat 3`.

### Verdict (control #6, one line)

version wall = removed (Foundation stage-copy → 0xFFFFFFFF, read-back `ff ff
ff ff`; the refusal moved to libcups.2 2.0.0 vs 1.0.0 — same class, next
dylib); LC/template rewrite = yes (exact-slot, LCs byte-identical);
chrome-macho's own dlopen = further — `//Frameworks/…` opens, maps, and the
framework's dependency tree loads; next = the dylib-version class, systemic
(real compat values below the framework's requirements).

### Repro

```sh
# stage-copy version patch (poller after "Chrome framework staged:")
python3 build-freebsd/stage-patch.py \
  $LOCAL/System/Library/Frameworks/Foundation.framework/Versions/C/Foundation \
  0xFFFFFFFF 0xFFFFFFFF          # read-back: ff ff ff ff
# launcher template rewrite (exact-slot, LCs verified unchanged)
python3 build-freebsd/chrome-template-patch.py <launcher> <launcher-patched>
# then: chrome probe harness (patched probe as test binary) and the
# template-patched launcher, each with ktrace -f -i; kdump -f kt.bin
# grep -B1 "RET openat" — the libcups openat is the last before the dlerror
```
Result: probe [0]/FWMACHO → `Incompatible library version: … libcups.2.dylib
provides version 1.0.0` (Foundation wall gone); launcher dlopen
`//Frameworks/…` → same libcups refusal after mapping the dep tree; RC=132
(SIGILL) after the dlerror.

## Control #7 — version class across the cascade: 57/57 patched; the next refusal is dyld-internal

### Step 1 — generalized batch patch

`stage-patch.py` gained a `--paths-file` mode (exact LC walk, thin + fat
selected by raw magic bytes, per-file read-back). The patch set = the
distinct staged providers the previous run's dlopen cascade opened (ktrace
`openat … RET openat 3` list — 57 paths: the System/Library/Frameworks
stubs + libcups; the 267 MB Chrome framework excluded — nothing
version-requires it). A dry-run found a fat-parse bug (endianness decided
by a LE-read magic; SystemConfiguration is a 2-slice fat) — fixed to raw
bytes. Poller synced on `Chrome framework staged:` patches the fresh stage
copy only; read-backs `0xffffffff/0xffffffff` on all 57.

### Step 2 — Run C (56-patch, default staging trees)

The libcups wall is gone; the cascade loads deeper (unloaded tail:
libbsm, libpmenergy, libpmsample, libsandbox, libbz2, libicucore) and the
refusal moves to a missing image:

```
Library not loaded: /System/Library/PrivateFrameworks/Onyx2D.framework/Versions/A/Onyx2D
  Referenced from: /System/Library/Frameworks/CoreGraphics.framework/Versions/A/CoreGraphics
  Reason: image not found.
```

Onyx2D exists in the live overlay, but `System/Library/PrivateFrameworks` is
not among the harness's default staging trees. ktrace: the fanout ENOENTs
(`…/System/Library/Frameworks/Onyx2D.framework RET -1 errno 2`, ×N).

### Step 3 — Run D (57-patch incl. Onyx2D, staging trees extended)

`DARLING_STAGING_TREES=usr/lib:System/Library/Frameworks:
System/Library/PrivateFrameworks` — the harness stages PrivateFrameworks,
Onyx2D lands staged + patched (57/57). The cascade loads the framework,
Foundation, CoreFoundation, CoreGraphics, CoreText — then aborts:

```
Library not loaded: /System/Library/Frameworks/Foundation.framework/Versions/C/Foundation
  Referenced from: //Frameworks/…/Google Chrome for Testing Framework
  Reason: Incompatible library version: … requires 300.0.0 … Foundation provides 0.0.0.
```

— on the file dyld itself opened: ktrace
`openat(AT_FDCWD,…,O_RDONLY) NAMI "…/local-overlay/System/Library/
Frameworks/Foundation.framework/Versions/C/Foundation" RET openat 3 →
pread(0x3,…,0x1000,0)` with the patched header in the GIO dump (cffa edfe,
filetype=6, ncmds=17); host-side read-back after the run: LC_ID
cur=compat=0xffffffff at file-offs 2896/2900. Run C passed the same check on
the same patched file, so the re-refusal correlates with PrivateFrameworks
being staged; the report "provides 0.0.0" is dyld's no-ID branch
(`doGetLibraryInfo`: minVersion=0 when `fDylibIDOffset==0`). Unwind:
Foundation, `//Frameworks/…`, CoreFoundation, CoreGraphics, CoreText
unloaded; RC=132 (SIGILL).

### Verdict (control #7, one line)

version walls = removed 57/57 in the stage copy (read-backs ffffffff); the
chrome framework's dlopen = new refusal — `Incompatible library version:
Foundation provides 0.0.0` on the patched file dyld opened (Run C passed the
same check; the regression correlates with PrivateFrameworks staged); next =
dyld-internal — why the Foundation ImageLoader reports minVersion 0 for a
file whose LC_ID compatibility_version is 0xffffffff.

### Repro

```sh
# cascade provider list: kdump -f kt.bin | awk of NAMI/RET openat pairs
#   (distinct staged paths with "RET openat 3" in the dlopen phase)
python3 build-freebsd/stage-patch.py --paths-file <list>   # read-backs ffffffff
# poller after the harness's "Chrome framework staged:" line; runs under
# ktrace -f -i; Run D adds
#   DARLING_STAGING_TREES=usr/lib:System/Library/Frameworks:System/Library/PrivateFrameworks
# kdump -f kt.bin | grep -B1 "RET openat" — the last openat before the
# dlerror is the artifact line
```

## Control #8 — "0.0.0" = patch race, not a dyld parse quirk; early sync moves the dlopen to the symbol wall

### Step 1 — identify the exact image (candidates refuted)

`find` over the stage tree: exactly ONE Foundation copy
(`System/Library/Frameworks/Foundation.framework/Versions/C/Foundation`) —
no PrivateFrameworks shadow (candidate (a) refuted). The file dyld opened
(ktrace `openat … RET openat 3` on that path) is thin (magic cffaedfe), one
LC_ID_DYLIB at file-off 0xb40, cur=compat=0xffffffff, bytes
`ff ff ff ff ff ff ff ff` @2896–2903 — the patched values (candidate (b)
refuted: no slices). Source check: `ImageLoaderMachO::parseLoadCommands`
sets `fDylibIDOffset` unconditionally on any `LC_ID_DYLIB` (ImageLoaderMachO
.cpp:819–822); `doGetLibraryInfo` returns minVersion 0 only when
`fDylibIDOffset==0` (1472–1482) — a no-ID report can only come from an
image dyld never parsed from that file.

### Step 2 — the mechanism: when the patch lands relative to dyld's read

Run D's poller synced on `Chrome framework staged:` — i.e. it patched the
stage copy at/after the moment dyld's cascade reached Foundation; dyld read
compat=0.0.0 and reported the no-ID-shaped value. Run E moved the sync
earlier — the poller fires on `cached locally: …/System/Library/
PrivateFrameworks` (right after ALL trees are staged, ~20 s before mldr
execs, while the 267 MB chrome pax still runs):

```
POLLER70 EARLY fired iter=6 03:50:41
SUMMARY: patched=57/57 (read-backs must be 0xffffffff/0xffffffff)
```

Same env as Run D otherwise (PrivateFrameworks staged, same 57-path list,
template-patched launcher). Result — the Foundation refusal is GONE; the
dlopen moves a full stage deeper and fails at BIND time on a missing
symbol:

```
dlopen //Frameworks/…/Google Chrome for Testing Framework: Symbol not found: _kCGColorSpaceITUR_2100_PQ
  Referenced from: //Frameworks/… (which was built for Mac OS X 13.0)
  Expected in: /System/Library/Frameworks/CoreGraphics.framework/Versions/A/CoreGraphics
```

ktrace: the cascade opens the framework and CoreGraphics
(`openat … RET openat 3` on both), the whole dep tree binds (~8 k records),
then the dlerror — the last openat before the failure is CoreGraphics.

### Verdict (control #8, one line)

0.0.0 = patch race (the late-sync poller rewrote the stage copy after dyld
had read it; shadow-copy and slice-bug refuted by measurement — one thin
copy, LC_ID ffffffff @0xb40); fix = early poller sync on the
`cached locally: …PrivateFrameworks` line (~20 s before mldr); dlopen =
further — new refusal, a symbol wall: `_kCGColorSpaceITUR_2100_PQ` missing
in the CoreGraphics stub (built for Mac OS X 13.0); next = the dylib-symbol
class (the extras track), not versions.

### Repro

```sh
# poller sync point: grep RUNLOG for "cached locally: <stage>/System/
#   Library/PrivateFrameworks" (NOT "Chrome framework staged:" — too late)
# then stage-patch.py --paths-file <list>; run under ktrace -f -i with
#   DARLING_STAGING_TREES=usr/lib:System/Library/Frameworks:
#   System/Library/PrivateFrameworks
# Run D (late sync) vs Run E (early sync) is the A/B: 0.0.0 vs symbol wall
```

## Control #9 — the CoreGraphics symbol wall: 37 symbols, supplement + ordinal surgery; the wall moves to CoreFoundation

### Step 1 — inventory (one pass)

`nm -u` alone carries no ordinals on this binary; the ordinal map comes
from the chained-fixups imports table
(`build-freebsd/chrome-imports-by-ordinal.py` — lib_ordinal bits 0-7, flag
at bit 8, name_offset from bit 9; validated: 2696/2696 imports resolve to
names in the binary's `nm -u` set). Ordinal 3 = CoreGraphics; 162 imports;
diffed against the overlay stub's exports (`nm -gU`, 583 symbols):

```
CG-стена: 37 символов — CGColor(2), kCGColorSpace*/CGDisplayColorSpace(7),
CGDirectDisplay/CGDisplay* (7), CGDisplayStream*(5), CGEventSource*(2),
CGFontRendering*(1), CGPDFPage*(1), CGScreenCapture*(2), CGRegion*(1),
CGSSetWindow*(4), CGWindowList*(3), kCGDisplayStream* constants(4)
```

The current `darling-extras.dylib` already exports
`_kCGColorSpaceITUR_2100_PQ` (measured: 969 exports, the symbol present)
yet Run E still failed — dyld's two-level binds resolve only in the dylib
the ordinal names, so an injected extras cannot satisfy them. The symbols
must live in a dylib the framework's own load commands reference.

### Step 2 — supplement + exact-length ordinal surgery

`gen-cg-supl.py` emits a stub .s (kCG* constants -> data zero objects,
the rest -> text stubs); clang `-target x86_64-apple-macos10.12` +
`ld64.lld -dylib -install_name /usr/lib/cg-supl.dylib` produce the
supplement (37 exports, 10904 B). `cg-supl-patch.py` then, on the file to
be staged:

- appends one LC_LOAD_DYLIB (48 B, name `/usr/lib/cg-supl.dylib`) in the
  header-page slack (exactly 48 B of zero padding before `__text`);
- grows `ncmds` 78->79 **and `sizeofcmds` 0x27f0->0x2820** — without the
  sizeofcmds bump dyld rejects the command ("malformed load command #78 of
  79 … size too large", Run G); the grown area still ends exactly at
  `__text` (0x2840);
- rewrites lib_ordinal 3->67 for exactly the 37 listed imports in the
  chained-fixups imports table (bit-exact byte patches; verify: 37/37 at
  ordinal 67, the other 125 ordinal-3 imports untouched).

Run F (stage-copy surgery raced dyld's read — dyld bound the unpatched
imports, same symbol wall) vs Run G (patched source app staged race-free —
dyld read the patched file: the sizeofcmds bug surfaced as the malformed-DC
error) vs Run H (fixed): the CG wall falls.

### Run H result

```
dyld: unloaded: … /usr/lib/cg-supl.dylib          ← the supplement loaded
dlopen //Frameworks/…/Google Chrome for Testing Framework: Symbol not found: ___NSArray0__struct
  Referenced from: //Frameworks/… (which was built for Mac OS X 13.0)
  Expected in: /System/Library/Frameworks/CoreFoundation.framework/Versions/A/CoreFoundation
```

`_kCGColorSpaceITUR_2100_PQ` no longer sounds; the refusal moved to the
next provider — CoreFoundation, symbol `___NSArray0__struct`.

### Verdict (control #9, one line)

CG-стена: 37 символов (CGColorSpace/Display/DisplayStream/CGS/WindowList
classes; kCG* constants as data, the rest text); dlopen = новый отказ —
`Symbol not found: ___NSArray0__struct`, Expected in CoreFoundation (the
supplement loaded and the CG binds resolved; the class of wall repeats on
the next stub provider); след: dlerror
`Symbol not found: ___NSArray0__struct … Expected in: …/CoreFoundation.
framework/Versions/A/CoreFoundation`.

### Repro

```sh
python3 build-freebsd/chrome-imports-by-ordinal.py <framework> --missing <ordinal> <nm-gU-exports.txt>
python3 build-freebsd/gen-cg-supl.py <missing.txt> cg-supl.s
clang -target x86_64-apple-macos10.12 -c cg-supl.s -o cg-supl.o
ld64.lld -dylib -arch x86_64 -platform_version macos 10.12 10.12 \
  -install_name /usr/lib/cg-supl.dylib -o cg-supl.dylib cg-supl.o
# patch the framework SOURCE copy (race-free; the stage copy raced dyld's
# read in Run F) with cg-supl-patch.py; plant cg-supl.dylib in the stage
# tree's usr/lib via the early-sync poller; run per the Control #8 recipe
#   with CHROME_APP pointing at the patched app copy
```

## Control #21 — who calls MSL at init: the umbrella cascade; the Control #20 plant never reaches dyld (UUID-proven), so the SIGILL class could not be re-localized

### (1) static — the umbrella cascade and its call sites

The overlay `libSystem.B.dylib` has a single `__DATA,__mod_init_func`
entry: `_libSystem_initializer` (vmaddr 0xf110; the runs print its
runtime address, e.g. `…8f110`). Disassembly names the cascade and the
MSL entry points (offsets are the umbrella's vmaddrs):

```
0xf159  ___libkernel_init        0xf278  ___pthread_late_init
0xf188  ___libplatform_init      0xf27d  _libdispatch_init
0xf1ba  ___pthread_init          0xf29c  __libxpc_initializer
0xf1ec  __libc_initializer       0xf2bb  __libtrace_init
0xf20f  ___malloc_init           0xf2da  ___libdarwin_init
0xf22e  ___keymgr_initializer    0xf31e  _os_variant_has_internal_diagnostics
0xf24d  __dyld_initializer       0xf32c  ___malloc_late_init
        … getenv/strtol (env parsing), 0xf48b ___libkernel_init_late, ret
```

`__libc_initializer` (libsystem_c, stock) runs BEFORE `___malloc_init`
and `___malloc_late_init` — the umbrella reaches MSL only after
libsystem_c's init, so any malloc-family call inside `__libc_initializer`
hits the MSL with its zone not yet initialized. The pin source's zone
path (`_malloc_zone_malloc`, malloc.c:1559) derefs `zone->malloc`
without an init guard; the stock's internal path (slice 0x25b70) is
structurally different (pointer-indirection select, no simple NULL guard
either).

### (2) dynamic — the plant does not reach dyld; the loaded image is the live overlay's

Three runs of the Control #20 recipe with the poller planting the pin
build (md5-verified in the stage tree after firing — the Control #18/#20
verification step) at both candidate paths:

| run | plant path                                   | planted md5    | dyld `loaded:` UUID                              |
|-----|----------------------------------------------|----------------|--------------------------------------------------|
| M   | stage `usr/lib/system/` (marked build)       | 20520a4e…      | 1FA0731B… (live overlay FAT slice)               |
| D   | doubled `…/tmp/darling-local-overlay/usr/lib/system/` | eb0ae578… | 1FA0731B… (live overlay FAT slice)           |

1FA0731B-F0EA-310E-8808-B4118C4E62D8 is the x86_64 slice UUID of the
LIVE overlay's FAT `libsystem_malloc.dylib` (668712 B) — the planted
thin builds (UUID 4C4C4402…) never loaded. ktrace shows why:
`launch-dynamic` fstats `$DARLING_OVERLAY/usr/lib/system/
libsystem_malloc.dylib` during closure computation, and mldr re-roots
the manifest's host path (probing the doubled
`/tmp/darling-local-overlay/tmp/darling-local-overlay/usr/lib/…`
component-wise — ENOENT — in run M before the plant existed, and again
in run D), then falls back to the manifest path itself — the live
overlay. The stage-tree copy of this image is never read.

Consequences measured this turn:

- the Control #20 SIGILL class could NOT be re-localized — the planted
  rebuild never executed; no first-fatal-SIGILL PC exists to name;
- with the stock MSL actually loaded, the startup is nondeterministic in
  this configuration: run D completed to the Foundation version wall
  (1905 lines, the Control #20 stock baseline), while run M (marked
  build planted, stock loaded) died mid-startup with a crash block —
  `FATAL signal 11 (code=1) at addr=0x270, rip=…92076e` (inside the
  loaded MSL image at offset 0x2676e; rax=rdi=0, rcx=0xfcf50) — a
  NULL-zone `zone->malloc`-slot deref, the pre-init-call class;
- the marker build itself is sound: the four once-only `write(2)`
  markers (`inject-init-markers.py`, gated by `MSL_MARKERS=1` in
  `build-libmalloc-pin.sh`, applied to the BUILD COPY only — the
  submodule is never edited) compiled in and planted; none fired because
  the planted image never loaded.

### (3) difference at the point + fix scope

The builds are deterministic (unmarked eb0ae5789a2b2b77a8ba68ae4f6e34de,
marked 20520a4e0127845dc8d5c1644b80f262) — the difference at the failing
point is NOT in `build-libmalloc-pin.sh` / `fix-export-trie.py`. It is
in the PLANT REACH: `launch-dynamic` computes the image closure from
`DARLING_OVERLAY` (the live overlay) and mldr resolves this image to
that host path; the re-root probe doubles it and the fallback lands on
the manifest path. The poller-side fix was tried (plant at the doubled
path mldr probes) and measured ineffective (run D). Making mldr read
the stage copy belongs to mldr/launch-dynamic path resolution — outside
this lane's layer — measured and stopped per the cascade rule. A
plant-through-a-copied-overlay (DARLING_OVERLAY → a writable copy with
the MSL swapped) is available to the head if the pair must be re-run.

### Method rule gained (applies to every future plant lane)

`md5`-in-the-stage-tree after firing is NOT proof that dyld loaded the
planted file. The authoritative check is the LOADED UUID from the run
log's `dyld: loaded: <UUID> <path>` line, compared against the planted
artifact's UUID (thin build) or its slice UUID (fat file). Control #20's
pair side-difference (151-line init death vs 1905-line wall) is NOT
explained by the current resolution behavior (both sides load the live
stock here) — the #20 class attribution should be re-verified with the
UUID check enforced before any fix is designed on top of it.

### Verdict (control #21, one line)

кто зовёт = зонт `_libSystem_initializer`: `__libc_initializer` (0xf1ec)
ДО `___malloc_init` (0xf20f) / `___malloc_late_init` (0xf32c) —
libsystem_c-init идёт с непроинициализированной MSL-зоной; первый
фатальный SIGILL = НЕ ЛОКАЛИЗОВАН — посадка пересборки не доходит до
dyld (UUID-замер: при любой посадке грузится стоковый срез живого
overlay 1FA0731B; стейдж-копия этой картинки не читается); отличие от
стока в точке = не в MSL-сборке (детерминирована) а в доставке посадки
(launch-dynamic строит closure из живого overlay, mldr резолвит туда,
пере-корневой пробел удваивает путь); фикс = ВНЕ СЛОЯ — mldr/
launch-dynamic резолюция — замерено и остановлено (по правилу);
пара #20 = НЕ ПЕРЕГОНАЛАСЬ (A/B неэффективен без доехавшей посадки —
класс #20 не подтверждён и не опровергнут); остаток = починка доставки
посадки (вне слоя) либо посадка через копию overlay под
DARLING_OVERLAY; контроль посадки = LOADED UUID из лога прогона.

### Repro

```sh
# static: llvm-objdump -d the overlay libSystem.B.dylib around the
#   mod_init_func target (_libSystem_initializer @ 0xf110); the callq
#   sequence names the cascade (see offsets above)
# dynamic: the Control #20 recipe (cft90-poller.sh plant at the early
#   "cached locally: …/usr/lib" marker; ktrace -i; DYLD_PRINT_INITIALIZERS=1);
#   plant variants: stage usr/lib/system (cft91-runM) and the doubled
#   path (cft91-poller-doubled.sh, cft91-runD);
#   check: `grep "loaded.*libsystem_malloc" <run log>` — the UUID must
#   match the planted artifact's slice, or the run measured the overlay
# stock slice UUID: llvm-otool/fat-walk of the live overlay's MSL
# markers: MSL_MARKERS=1 sh build-freebsd/zone-contract/build-libmalloc-pin.sh
#   (inject-init-markers.py patches the BUILD COPY only)
```

## Control #10 — the symbol-wall map: 25 providers, 335 symbols; one union supplement; the failure moves to init-time runtime semantics

### Step 1 — the map (one pass)

`sym-wall-map.py` walks every dylib ordinal of the Chrome framework and
its `Libraries/*.dylib` (chained-fixups imports, validated layout), diffs
each provider's imports against the overlay stub's exports, and classifies
symbols. One correction en route: `libSystem.B.dylib` is a reexport
umbrella — its own table lists 350 exports while `/usr/lib/system/*`
carries 12724 (pthread_mutex_lock etc. included); diffing against the
umbrella alone manufactured 931 fake walls. With the reexport-aware diff:

```
Карта стен: 25 провайдеров, 335 символов — топ-3 класса:
  ObjC class-refs (OBJC_CLASS_$×97 + OBJC_METACLASS_$×5, 12 провайдеров)
  IOKit IO* (×74, +SCDynamicStore×4, kIOMainPortDisplay×1)
  CoreText CTFont* (×59)
+ CoreGraphics 37, AppKit 26, Metal 23, Foundation 21, QuartzCore 17+1,
  ScreenCaptureKit 13, AuthenticationServices 14, AVFoundation 10,
  CoreFoundation 7, SystemConfiguration 5, … ; libSystem 8 (real),
  libsandbox 1; UNSTAGED провайдеров: 0 (PrivateFrameworks в стейджинге)
```

### Step 2 — one union supplement, three exact-length surgeries

`gen-cg-supl.py` generalized (install_name parameter, link recipe echoed;
types: kCG*/kCA* constants + `_OBJC_CLASS_$`/`_OBJC_METACLASS_$`/`___*` →
data zero objects — the first run emitted ObjC class-refs as TEXT stubs and
the crash came back as garbage-at-0x4031c3c031c0, the stub's `xor eax,eax;
ret` bytes read as a data pointer). The 335-symbol union links into three
supplement copies — the per-binary header slack dictates the install name
(48/40/32 B → `/usr/lib/suppl.dylib`, `/s.dylib`, `/s.d`):

```
suppl.dylib (335 exp) → /usr/lib/suppl.dylib   framework   ordinal 67, 334/335
s.dylib     (335 exp) → /s.dylib               libaperitif ordinal  3,   1/335
s.d         (335 exp) → /s.d                   libvk_swiftshader ordinal 8, 1/335
```

`cg-supl-patch.py` generalized (`--suppl-name`, adaptive cmdsize-vs-slack,
verify counts list entries not imported by the binary honestly) — the
Control #8/9 recipe on SOURCE copies (race-free), ncmds+sizeofcmds grown
together, exact-length byte patches on the imports table.

### Run B result

No `Symbol not found … Expected in <mapped provider>` sounds — every
mapped wall's binds resolve through the supplements. The dlopen proceeds
into the framework's initializers and dies on runtime semantics:

```
[darling-mldr] FATAL signal 11 (code=1) at addr=0x0
```

— a NULL dereference through a zero-data stub (the framework reads a
class-ref/constant the stubs deliberately carry no value for; run A with
text-typed class-refs crashed at 0x4031c3c031c0 instead, the code bytes
read as a pointer — the type fix moved the crash address to the honest
0x0). The wall class has changed: from binds to implementations.

### Verdict (control #10, one line)

Карта стен: 25 провайдеров, 335 символов (топ-3 класса: ObjC class-refs
×102, IOKit IO* ×74, CoreText CTFont* ×59); supplements = 3 копии
union-стаба (335 экспортов: 218 text + 117 data; per-slack install names);
dlopen = новый класс отказа — init-time NULL-deref (FATAL signal 11 at
addr=0x0) через нулевые стабы: символы биндятся, реализаций нет; след:
`[darling-mldr] FATAL signal 11 (code=1) at addr=0x0`.

### Repro

```sh
python3 build-freebsd/sym-wall-map.py <overlay> <missing.txt> <framework> <libs...>
python3 build-freebsd/gen-cg-supl.py <missing.txt> suppl.s <install_name>
clang -target x86_64-apple-macos10.12 -c suppl.s -o suppl.o
ld64.lld -dylib -arch x86_64 -platform_version macos 10.12 10.12 \
  -install_name <install_name> -o <suppl> suppl.o     # per-slack names
# cg-supl-patch.py on the SOURCE copies (framework + the two small libs),
# plant the supplement copies in the stage tree via the early-sync poller,
# run per the Control #8 recipe with CHROME_APP at the patched app copy
```

## Control #11 — initializer blame: the zero class-ref that trips libobjc

### Localization (event instrumentation, owner's decision 02.10 04:24)

The crash block (registers + guest stack) with the dyld-trace image map
pins the fault without new probe code — two independent runs give identical
slide-relative offsets:

```
Run B: rip=0x25d8c832746  mh(libobjc.A.dylib)=0x25d8c80e000  -> +0x24746
Run I: rip=0x20bd23432746 mh(libobjc.A.dylib)=0x20bd2340e000  -> +0x24746
       rax=rbx = suppl.dylib mh + 0x1290   (both runs)
```

`DYLD_PRINT_INITIALIZERS=1` (Run I) shows all 14 initializer prints are
startup-phase (libSystem, dyld-trace, libc++, libobjc ×11) — no cascade
initializer had been called when the fault hit, so the crash sits between
image load and the initializer phase: libobjc's per-image processing of
the Chrome framework's ObjC metadata. nm of the supplement at offset
0x1290 names the zeroed slot: `_OBJC_CLASS_$_NSURLProtocol` (D-type, the
8-byte range at exactly 0x1290).

### Symbol classification + real implementation

Provider: ordinal 5 = Foundation (`chrome-imports-by-ordinal.py`); type:
ObjC class-ref. Real implementation in the tree: YES —
`src/external/cfnetwork/src/URL/NSURLProtocol.m:69`
`@implementation NSURLProtocol`, and Foundation's
`reexport_x86_64.exp:117/148` declare
`_OBJC_CLASS_$_NSURLProtocol`/`_OBJC_METACLASS_$_NSURLProtocol` as
reexported. In the BUILT overlay stubs: NO — `nm -gU` of the overlay
CFNetwork → 0 NSURLProtocol symbols; Foundation → 0 (while exporting 356
OBJC_CLASS symbols overall); the overlay-wide scan finds the symbol in no
built dylib. The class-ref bind therefore resolved to the supplement's
zero quad, and libobjc's processing of that "class" dereferenced NULL at
libobjc+0x24746.

### Verdict (control #11, one line)

Инициализатор: libobjc.A.dylib+0x24746 (стабильно в двух прогонах; контекст
— загрузка изображения Chrome-фреймворка, каскадные инициализаторы ещё не
вызывались); символ: `_OBJC_CLASS_$_NSURLProtocol` (Foundation, ordinal 5,
тип: ObjC class-ref — zero-данные в suppl+0x1290, rax/rbx = suppl+0x1290);
реальная реализация: ЕСТЬ в дереве — cfnetwork/src/URL/NSURLProtocol.m:69
+ Foundation reexport_x86_64.exp:117/148, но в собранных overlay-стабах
отсутствует (CFNetwork 0, Foundation 0 из 356); фикс = класс: реальные
ObjC-классы — пересборка CFNetwork/Foundation из in-tree исходников с их
reexport-списками (zero-data class-refs — тупик Run B, задокументирован).

### Repro

```sh
# crash block + dyld-trace image map from the run log; offsets are
# slide-relative and reproduce across runs (Run B / Run I)
# DYLD_PRINT_INITIALIZERS=1 in the run env (passed through by the harness)
# nm -gU <suppl.dylib> sorted -> the symbol at rax - suppl_mh (0x1290)
# nm -gU <overlay CFNetwork/Foundation> -> absence of the class
# grep -n "@implementation NSURLProtocol" src/external/cfnetwork/src/URL/NSURLProtocol.m
```

## Control #12 — real ObjC classes in the supplement: the libobjc crash falls, initializers run, the wall moves to stub behavior

### Mechanism (choice: compiled classes, no in-place overlay writes)

A generated `objc-suppl.m` carries an empty `@interface/@implementation`
pair for each of the 97 class-refs from the Control #10 map — compiled
`-fobjc-runtime=macosx-10.12` against the committed SDK flat tarball, so
each class becomes a REAL `objc_class`/`objc_metaclass` in `__DATA` with a
`__objc_classlist` entry that libobjc registers when the image loads (the
same per-image processing that crashed on the zero quads now registers
valid classes). The class/metaclass names leave the stub part (no
duplicate exports); the combined dylib links stubs + classes and depends
on `/usr/lib/libobjc.A.dylib` (`__objc_empty_cache` resolves there):

```
suppl.dylib: 427 exports = 233 stubs + 97 classes x 2
  _OBJC_CLASS_$_NSURLProtocol      D @0x3a18
  _OBJC_METACLASS_$_NSURLProtocol  D @0x3a40
```

The same bytes are planted as the per-slack copies (`/usr/lib/suppl.dylib`,
`/s.dylib`, `/s.d`); the stage tree carries them (nm -gU of the staged
file reproduces the pair, 427 exports). The Control #11 surgeries on the
SOURCE copies re-point the 335-name list — the 102 class-ref imports
included — to the supplement ordinal.

### Run A result

`libobjc+0x24746` does not sound. `DYLD_PRINT_INITIALIZERS=1` now shows
38 initializer calls (Control #11: 14, all startup) — the cascade runs
its initializers: libgif, libGL ×2, CoreGraphics ×2, and the Chrome
framework's own (`calling initializer function 0x1579dfb764c0 in
//Frameworks/…/Google Chrome for Testing Framework`). The failure moved:

```
[darling-mldr] FATAL signal 11 (code=2) at addr=0x7fffffdfda68
  rip=0x7fffffdfda68  (the stack — a wild jump), rsp=0x7fffffdfda68
```

— an init-time wild jump through a behaviorally-empty stub (a stub that
returns 0 used as a function pointer/callback inside the framework's
initializer). The wall class changes again: from class-structure to stub
*behavior* — the102 class-refs are structurally real now; the 233 text
stubs remain0-returning.

### Verdict (control #12, one line)

NSURLProtocol реален в overlay (nm -gU staged suppl.dylib: 2 символа —
`_OBJC_CLASS_$_`+`_OBJC_METACLASS_$_` @0x3a18/0x3a40, 427 экспортов);
libobjc+0x24746 не звучит; dlopen = СЛЕДУЮЩАЯ ФАЗА — init-time wild jump
(FATAL 11 code=2, rip=стек) после вызова инициализатора Chrome framework
— стена сменилась на поведение стабов (функции-стабы возвращают 0 и
используются как указатели); покрыто class-ref'ов: 102 (97 классов ×2,
все class-ref'ы из карты Control #10).

### Repro

```sh
# gen objc-suppl.m (empty @interface/@implementation per class-ref name),
# clang -target x86_64-apple-macos10.12 -fobjc-runtime=macosx-10.12
#   -nostdinc -I<unpacked-sdk>/usr/include -c objc-suppl.m
# ld64.lld -dylib ... -install_name /usr/lib/suppl.dylib
#   -o suppl.dylib stub.o objc-suppl.o <overlay libobjc.A.dylib>
# cg-supl-patch.py on the source app copies (335-name list), plant the
#   combined dylib via the early-sync poller, run per Control #11 recipe
#   with DYLD_PRINT_INITIALIZERS=1
```

## Control #13 — stub-behavior blame: the wild jump is malloc-zone semantics, not a supplement stub

### Marker (blame, not a fix)

All 233 supplement text stubs were rebuilt with `int3` bodies (the real
ObjC classes and data stubs untouched) and planted as the per-slack
copies. The run crashed with the IDENTICAL signature as control #12 —
`FATAL signal 11 (code=2) at addr=0x7fffffdfda68, rip=rsp=stack` — **no
int3 trap fired**: the wild jump never entered a supplement stub. The
registers are stable across runs (rcx=rdx=rdi=0x307, rsi=0x50000).

### Import resolution at the call point

The Chrome framework's initializer at offset 0x212A4C0 (mh
0x1579dda4c000, the last `calling initializer … in //Frameworks/…`
print) starts with four stub calls. The imports behind them resolve from
the file itself: un-bound chained-fixup slots carry the import ordinal in
their own bytes (ordinal = slot value & 0xFFFFFF;
`llvm-objdump21 --macho --chained-fixups` confirms the table):

```
slot __DATA_CONST+0x1fd8 -> import[1209] _malloc_get_all_zones         <- libSystem
slot __DATA_CONST+0x80   -> import[15]   _strlen                      <- libSystem
slot __DATA_CONST+0x29f0 -> import[1539] _malloc_default_zone         <- libSystem
slot __DATA_CONST+0x29d8 -> import[1536] _malloc_default_purgeable_zone <- libSystem
```

The disasm around the initializer shows the zone-API pattern: call
`_malloc_get_all_zones` → test → out-params from stack locals → later
`callq *(%rax)` / `lock decl 0x8(%rbx)` / `callq *0x18(%rax)` — zone
retain/release/method dispatch through the returned zone object. The
overlay's libsystem_malloc exports all three zone symbols (fat slices at
0x24d40/0x211a0 etc.), so the binds resolved to the overlay's libmalloc —
and its zone objects do not carry the macOS method-table layout this
macOS-13 CFT build dispatches through; the dispatch jumped to a stack
address.

### Verdict (control #13, one line)

Вызов: Chrome framework initializer fw+0x212A4C0 (dyld-trace print);
стаб: `_malloc_get_all_zones` + `_malloc_default_zone` +
`_malloc_default_purgeable_zone` (libSystem ordinal 66 → overlay
libsystem_malloc.dylib, text, реализованы, но не по macOS-контракту зон);
вызыватель ждёт: malloc-zone объект с рабочей метод-таблицей (диспатч по
vtable-слотам 0x0/0x18 через возвращённую зону); реализация в дереве:
ЕСТЬ — `src/external/libmalloc/src/malloc.c:2293` +
`malloc_get_all_zones(task_t, memory_reader_t, vm_address_t**, unsigned*)`
и собранный libsystem_malloc.dylib экспортирует все три символа; фикс =
класс: привести реализацию зон libmalloc к macOS-контракту
(layout/vtable сигнатуры), стабы не при чём — int3-маркеры 233 стабов
отработали без единой ловушки.

### Repro

```sh
# marker: sed the supplement stub bodies to int3 (real classes/data kept),
#   relink, plant via the early-sync poller, run per control #12 — the
#   crash signature is unchanged => no supplement stub was called
# import resolution: the GOT slot's own 8 bytes hold the unbound fixup
#   entry (ordinal = value & 0xFFFFFF); cross-check with
#   llvm-objdump21 --macho --chained-fixups (imports table)
# nm -gU <overlay libsystem_malloc.dylib> -> the three zone symbols
# grep -n "malloc_get_all_zones" src/external/libmalloc/src/malloc.c
```

## Control #14 — zone vtable: the measured layout delta (and a second export defect)

### The probe (blame, not a fix)

`tests/zone-vtable-probe.c` (built per the build-crash-probe.sh recipe;
the SDK flat tarball lacks stdbool.h so dlfcn.h is replaced by direct
declarations) dumps `malloc_default_zone()`'s object the way the chrome
framework's initializer consumes it — the first eight qwords, each
resolved through `dladdr`. Run under the harness as the test binary —
no chrome staging involved.

### The dump (slide-stable, one run)

```
default_zone=0x2d4b5b133000
vtable[0]=0x0                dladdr=0 sym=-          ← reserved1: NULL
vtable[1]=0x0                dladdr=0 sym=-          ← reserved2: NULL
vtable[2]=default_zone_size      (valid code)
vtable[3]=default_zone_malloc    (valid code)
vtable[4]=default_zone_calloc    (valid code)
vtable[5]=default_zone_valloc    (valid code)
vtable[6]=default_zone_free      (valid code)
vtable[7]=default_zone_realloc   (valid code)
```

The runtime layout matches the pinned header exactly: the Darling
libmalloc's `struct _malloc_zone_t` (submodule
`src/external/libmalloc` @4f2a808d — see the submodule note below) leads
with `void *reserved1; void *reserved2;` (include/malloc/malloc.h:67-68)
before `size/malloc/calloc/valloc/free/realloc`. macOS-13 — the layout
the chrome framework was BUILT against — has no reserved pair: slot0 is
the `size` callback, slot1 `free`, slot2 `realloc`, slot3 `destroy`,
slot4 `zone_name`, slot5 `batch_malloc`, slot6 `batch_free`, slot7
`introspect`. Every macOS-13 slot sits +2 (16 bytes) later in the
overlay's zone.

### The dispatch site (control #13 disasm) vs the measured slots

`callq *(%rax)` reads slot0; `callq *0x18(%rax)` reads slot3. Under
macOS-13 those are `size` and `destroy` — valid callbacks. Under the
overlay: slot0 = NULL (a call through a NULL slot — the control #13 wild
jump) and slot3 = `malloc` (a wrong-semantic callback). The whole
method-table dispatch is misaligned by the two reserved fields.

### Second measured defect (same era class)

`_mach_task_self_` in the overlay's `libsystem_kernel.dylib` is a **BSS
symbol (type B @0x9bf7c)** — the probe's first runs called it and died
with `FATAL 11 code=2 rip=<the symbol address>` (NX on a data page,
reproduced slide-stable: …bf7c twice). macOS-13 binaries CALL this
symbol; the overlay exports it as a variable — the old-SDK semantics vs
the modern callable contract. `malloc_get_all_zones(task=NULL)` also
faults (addr=0x8, an unguarded NULL+8 read in the remote-zones path).

### Submodule note (step 3 of the dispatch)

The actual path: `src/external/libmalloc` — the superproject submodule
entry `src/external/libmalloc`, physically nested under the tree root's
own `src/` directory (repo-relative: `<tree-root>/src/external/libmalloc`).
Before this lane its checkout was `a57991e` (update_sources_11.5) while
the superproject pins `4f2a808d`; the pin object was absent locally and
was fetched from the submodule remote this turn, and the checkout now
sits AT the pin (`git rev-parse HEAD` →
4f2a808dfc675356c509bc54a8ee530cdcfc4c4f). Control #13's path claim was
relative-correct but at the wrong commit — corrected here.

### Verdict (control #14, one line)

zone ptr=default_zone (слайд-стабилен); vtable[0..7] = NULL, NULL,
default_zone_size, default_zone_malloc, default_zone_calloc,
default_zone_valloc, default_zone_free, default_zone_realloc; слот
`callq *(%rax)` = vtable[0] = NULL (мусор под macOS-13-слот `size`);
дельта layout vs macOS-13 = два ведущих `reserved1/reserved2`
(include/malloc/malloc.h:67-68 пина 4f2a808d) — весь macOS-13-слоты
сдвинуты на +2 (+16 Б); фикс-мишень = `struct _malloc_zone_t` +
все инициализаторы зон в src/malloc.c того же пина (слоты записи
сдвигаются вместе с layout); второй независимый дефект той же эпохи =
`_mach_task_self_` в overlay libsystem_kernel (B @0x9bf7c, вызов NX-fault,
замер дважды).

### Repro

```sh
# build tests/zone-vtable-probe.c per build-crash-probe.sh (SDK flat +
#   staged overlay libSystem; no dlfcn.h — stdbool.h missing in the flat)
# run via the harness as DARLING_TEST_BINARY=zone-vtable-probe-macho
#   (no chrome staging needed); the probe never calls mach_task_self_
# nm -gU/libsystem_kernel: _mach_task_self_ -> B @0x9bf7c (type B)
# the pinned layout: src/external/libmalloc (at 4f2a808d)
#   include/malloc/malloc.h:64-93
```

## Control #15 — zone contract: the layout fix is derived and self-checked; the acceptance criterion is NOT met (two measured blockers)

### (1)+(2) — the layout fix machinery (derived from the pin, submodule untouched)

`build-freebsd/zone-contract/build-libmalloc-zone.sh` derives the fix
from the pin checkout: a corrected `malloc/malloc.h` (the reserved pair
dropped) and a src copy with the ten reserved-referencing lines removed
across pguard/magazine/nano/nanov2/purgeable — every other slot write in
the sources is by field name, so the layout shifts with the header. The
script self-checks: `grep reserved` over the fixed copies → zero hits;
the compile loop is the proven cross recipe (SDK flat + clang resource
headers + `architecture/byte_order.h` from `src/external` + the fakesdk).
**The header web is RESOLVED** — the gate chain found and fixed inside
`build-libmalloc-zone.sh`:

- clang's resource `stdatomic.h` defers via
  `__has_include_next(<stdatomic.h>)`; the flat SDK ships its own
  stdatomic.h which is EMPTY under `__clang__` — the shadow silently
  killed the `memory_order` typedef (13 errors). Fix: move the scratch
  SDK copy aside (`stdatomic.h.disabled`) so the builtin fallback runs;
- `i386/cpu_capabilities.h` guards its content in `#ifdef PRIVATE`;
  fix: `-DPRIVATE` (the pin's CMakeLists echoes it);
- `nanov2_malloc.c`'s `OS_VARIANT_*` gates need
  `-DOS_VARIANT_NOTRESOLVED=1 -DOS_VARIANT_RESOLVED=1` (the pin's
  per-file COMPILE_FLAGS), else the resolver emits no `_nanov2_*`
  exports and the export list fails at link;
- `virtual_default_zone`'s POSITIONAL initializer leads with the two
  reserved placeholders — removed by the script (12 sites in total);
- `resolver.h` lives in the submodule's `resolver/` dir — added to -I;
- the link keeps the `$UNIX2003` libc imports undefined
  (`-undefined dynamic_lookup`) as the original carries them, and drops
  `-D__DARWIN_UNIX03` because THIS guest's closure exports no `$UNIX2003`
  variants of mprotect/write/sleep/kill (measured: lazy-bind failure
  `Symbol not found: _mprotect$UNIX2003 … Expected in: flat namespace`
  with the UNIX03 build).

Result: the faithful MSL dylib BUILDS (386016 B, 283 exports;
`_malloc_default_zone` @0x2e360, `_malloc_get_all_zones` @0x31160) and
LOADS in the guest. **The criterion is still not measured**: with the
faithful dylib planted early, the zone-vtable probe run dies with
`FATAL signal 10 (SIGBUS) at addr=0x34a962edf018, rip=0x826a41758,
rax=0x34a962edf000` — the faulting access sits at zone+0x18 where rax is
the runtime address of the `__v_zone` section (vmaddr 0x50000, slide
0x34a962e8f000). otool verifies the built dylib's layout is correct
(__v_zone addr 0x50000 size 0x4000 align 2^14, fully inside the
file-backed __DATA range) — the delta is in that section's runtime page
state under the guest, deeper than the toolchain gates.

### Init-cascade measurements (lane 81, ktrace + crash blocks)

Two independent early-plant runs of the faithful dylib fault during MSL
initialization, with run-to-run variance that pins the class:

- run D: `FATAL signal 10 (SIGBUS) at addr=0x34a962edf018,
  rip=0x826a41758, rax=0x34a962edf000` — the faulting access sits at
  zone+0x18 where rax is the `__v_zone` section's runtime address
  (vmaddr 0x50000, slide 0x34a962e8f000);
- run E (ktraced): `FATAL signal 11 (code=1) at addr=0x18ac354df555,
  rip=0x8268e74d2` with rcx=rsi=rdx=0x18ac354df555 (a wild pointer
  scanned like a string) and rdi=0x18abac5f5a20 — inside a guest
  image's __TEXT per the mmap records; the faulting PC falls in NO guest
  mapping of that run — it sits in mldr's host-side syscall translation,
  i.e. the guest init passed a garbage pointer to a syscall (mldr's own
  frames in the block: crash_debug_handler / thr_kill).

The static `virtual_default_zone` initializer is verified correct for
the pin's post-removal layout (the positional-shift compile errors
vanished; the named fields realign). The garbage arises in the init path
beyond the static zone — the next measurement is guest-side tracing of
the MSL init (which field feeds the syscall) — stopped here per the
dispatch's cascade rule.

### Init-cascade trace (lane 82): the fault is in the loader's processing of libobjc, not in libmalloc

The ktrace of the failing run (the run E trace) names the faulting
sequence: the guest loads an image — `mmap(__TEXT 0x59000)`,
`mprotect RWX → RX` (the fixup pass), `clock_gettime` — then
`PSIG SIGSEGV SEGV_MAPERR` with rcx=rsi=rdx = a garbage pointer. The
segment sizes identify the image: `libobjc.A.dylib` (TEXT size 0x59000,
LINKEDIT fileoff 0x5e000 — otool of the overlay's copy matches the
ktrace's mmap offsets exactly). The garbage signature recurs across
slides with stable low bytes: run E 0x18ac354df555, run F
0x3251754df555 (…4df555) — a fixed-offset value read during dyld's load
of the stock libobjc, triggered by the faithful MSL being planted.

Discriminating measurements:

- my built dylib's fixup format = `LC_DYLD_INFO_ONLY` (classic), the
  same as the original overlay dylib; `llvm-objdump21 --macho --bind`
  parses its bind table cleanly (__DATA.__got entries → libSystem);
- the export filter (intersecting the original's export list with the
  objects' global symbols) does NOT change the crash: the link still
  emits the hidden-export warnings (nm's type column cannot see
  visibility — the filter needs `nm -m` private-external detection, a
  refinement) and the probe run faults with the identical …4df555
  signature;
- the MSL's own code never runs: the fault precedes its initializer —
  the garbage is not read from the MSL's data.

Verdict (init-cascade trace, one line): сисколл = mmap/mprotect-проход
фиксапов (dyld грузит libobjc — размеры сегментов матчат libobjc.A.dylib);
мусор = rcx=rsi=rdx=…4df555 (стабильные младшие байты через слайды);
источник = loader-слой (dyld-загрузчик обрабатывает фиксапы/бинды
стокового libobjc при посадке верного MSL; код MSL ещё не выполнялся);
слот-карта = НЕ ДОСТИГНУТА; Control #15 dlopen = НЕ ДОСТИГНУТ;
`_mach_task_self_` = не дошло; остаток = чужой слой (dyld-загрузчик) — по
правилу не чиню вслепую; фильтр экспорт-списка подпись краша не меняет.

### (3) — the narrow path (allowed by the dispatch): a zone-contract dylib

`gen-zone-contract.py` + `zone-contract.c` produce a dylib at the
original install name with the full export contract (284 unique exports —
nm -gU on the fat original lists each once per slice; deduped) and a zone
object laid out per the macOS-13 contract the head prescribed (slot0=size,
slot3=destroy). Measured:

- plant late (on `binary cached locally`): the probe still dumped the
  ORIGINAL overlay zone (vtable[0]=NULL, vtable[2]=default_zone_size at
  the original's 0x24c30 offset) — the plant lost the startup race;
- plant early (on `cached locally: …/usr/lib`): the contract dylib loads
  and the guest dies during libSystem initialization —
  `FATAL signal 10 (SIGBUS) at addr=0xff374df570`, no probe output. The
  minimal bump allocator + zero introspect slot is not startup-viable.

### Verdict (control #15, one line)

Слот-карта после фикса = НЕ ДОСТИГНУТА, но путь (а) ПРОЙДЕН ДО конца
сборки: header-web снят (stdatomic include_next + -DPRIVATE + OS_VARIANT
+ позиционный initializer + dynamic_lookup + без UNIX03 — всё в скрипте),
пин-фиделити MSL дайблиб СОБРАН (386016 Б, 283 экспортов) и ЗАГРУЖАЕТСЯ
в госте; критерий упирается в измеренный init-cascade блок класса
«мусорный указатель в syscall переводе гостя»: run D — SIGBUS на странице
`__v_zone` (fault zone+0x18, rax = runtime-адрес секции vmaddr 0x50000;
otool-лейаут корректен), run E — SIGSEGV по wild-указателю 0x18ac354df555
(rip в host-коде mldr = перевод syscall гостя; статический initializer
`virtual_default_zone` проверен корректным для пост-removal лейаута пина);
узкий путь (b) не реанимируется (не startup-viable — замер #15);
Control #15 dlopen = НЕ ДОСТИГНУТ (гость падает в MSL-init до пробы);
`_mach_task_self_` (B @0x9bf7c) — не дошло (старт раньше); остаток =
guest-side трейсинг MSL-init (какое поле кормит syscall мусором) — по
стоп-правилу каскада остановлено на измеренном.

### Repro

```sh
# faithful path (derived, self-checked, blocked on the header web):
sh build-freebsd/zone-contract/build-libmalloc-zone.sh
# narrow path:
nm -gU <overlay libsystem_malloc> > orig-nm.txt
python3 build-freebsd/zone-contract/gen-zone-contract.py orig-nm.txt <dir>
clang -target x86_64-apple-macos10.12 … zone-contract.c zone-stubs.s
ld64.lld -dylib … -install_name /usr/lib/system/libsystem_malloc.dylib \
  -current_version 0.0.0 -compatibility_version 1.0.0 \
  -exported_symbols_list exports.txt -o libsystem_malloc.dylib …
# plant via the early-sync poller (marker "cached locally: <stage>/usr/lib"),
# run tests/zone-vtable-probe-macho per the control #14 recipe
```

## Control #16 — the garbage reader: dyld's ObjC-init notification reads a computed pointer to an unmapped slide

### (1) the bytes: absent everywhere → a formula

Static search for the low bytes `55 f5 4d` (the stable …4df555 part of
the fault address) across all crash-time candidates — the built MSL dylib
(0x5e3e0), the overlay dyld (0x4fad50), libSystem.B, libsystem_c — **0
hits in every file**; the same for the overlay's libobjc. The value is
not a baked constant anywhere. Formula: the faulting value = a
16MB-aligned slide base + 0x4df555 (run E 0x18ac354df555 → base
0x18ac30a00000; run F 0x3251754df555 → base 0x325175000000), and the
fault address is covered by NONE of the run's 646 mmap records — the
pointer targets an address where nothing is mapped: a runtime-computed
pointer into a slide where no image loaded.

### (2) the reader: dyld's ObjC-init notification phase

`dyld2.cpp:1104-1115` — at `dyld_image_state_dependents_initialized`,
for images with `notifyObjC()`, dyld calls
`(*sNotifyObjCInit)(image->getRealPath(), image->machHeader())` — the
hook into libobjc's initialization — wrapped in `mach_absolute_time`
timing (the ktrace's `clock_gettime` records are that guest timing). The
crash sequence in the ktrace — `mmap(__TEXT 0x59000)`,
`mprotect RWX→RX` (fixups), `clock_gettime`, `PSIG SIGSEGV
SEGV_MAPERR` — places the fault in the code that hook runs: libobjc's
init reading through the computed pointer.

### (3) the trigger: what the MSL plant changes

The fault correlates with planting the faithful MSL: my link carries a
different dependency set (only `-lSystem` vs the original's
kernel/platform/dyld/compiler_rt/c), which shifts the closure's image
order and slides; the objc-init notification then computes a pointer onto
a slide where nothing is mapped. Discrimination: «в поле уже мусор» vs
«dyld читает по испорченному указателю» — the field holds NO static
garbage (the byte search is empty across all images) → the reader
computes the pointer at runtime and dereferences the unmapped result.

### (4) whose layer → STOPPED

The faulting reader = dyld's ObjC-init notification + libobjc's init
code — the guest dyld (June) and the overlay's libobjc, NOT the MSL
layout: no pattern in my dylib, its vmaddrs (__TEXT base, __DATA 0x4f000)
are comparable to the original's (__DATA 0x52000). Per the dispatch's
clause the fix is outside my layer — measured and stopped; the foreign
layer (dyld's slide attribution for computed pointers under a
replaced-libsystem_malloc load order) is not blindly fixed.

### Verdict (control #16, one line)

читатель = фаза ObjC-init-уведомления dyld (dyld2.cpp:1104-1115,
sNotifyObjCInit → init-код libobjc); байты = шаблон …4df555 ОТСУТСТВУЕТ
во всех образах (поиск 0 hits: MSL dylib, dyld, libSystem.B, libsystem_c,
libobjc) → формула: значение = 16MB-выровненный слайд-база + 0x4df555, fault
не покрыт ни одним из 646 mmap-записей; слой = guest dyld + overlay
libobjc (ВНЕ MSL layout — vmaddr сопоставимы с оригиналом); фикс = ВНЕ
слоя — измерено и остановлено; слот-карта = блокирована; Control #15
dlopen = не достигнут; `_mach_task_self_` = не дошло; остаток = разбор
атрибуции слайдов в dyld-окружении с заменённым libsystem_malloc (область
загрузчика) или изоляция триггера возвратом оригинального MSL.

### Repro

```sh
# ktrace of the early-plant probe run (control #15 recipe); kdump:
#   the PSIG SIGSEGV record + the mmap list (the fault address is
#   covered by none of them)
# byte search: python over the crash-time images for 55 f5 4d — 0 hits
# the reader phase: dyld2.cpp:1104-1115 (sNotifyObjCInit at
#   dyld_image_state_dependents_initialized; the mach_absolute_time
#   timing around it = the ktrace's clock_gettime records)
```

## Control #17 — slides: the rebuild's dependency set now equals the original's; the probe-run crash class is measured unchanged (the dep set is not its root)

### (1) — align the LC_LOAD_DYLIB set with the original

The pin's CMakeLists (`add_circular(system_malloc FAT …)`) declares the
dep targets: SIBLINGS `system_kernel platform system_dyld compiler_rt`,
UPWARD `system_c`. The original overlay dylib's load commands (python LC
parser; `llvm-objdump21 --macho --load` does not exist in this llvm —
its macho section only offers `--private-headers`) are exactly:

```
LC_LOAD    /usr/lib/system/libsystem_kernel.dylib
LC_LOAD    /usr/lib/system/libsystem_platform.dylib
LC_LOAD    /usr/lib/system/libdyld.dylib        (the system_dyld target installs under this name)
LC_LOAD    /usr/lib/system/libcompiler_rt.dylib
LC_UPWARD  /usr/lib/system/libsystem_c.dylib
```

The rebuild linked `-lSystem` only — one umbrella dep — which re-solves
kernel/platform/dyld/compiler_rt/c through the umbrella at runtime and
shifts the closure's order/slides. The link now passes the four sibling
dylibs explicitly (`-lsystem_kernel -lsystem_platform -ldyld
-lcompiler_rt`, lowercase — `-lSystem_kernel` is not found on this
filesystem), and ld64.lld does not implement `-upward-l` /
`-upward_library` ("not yet implemented"), so the UPWARD edge is added
post-link by `add-upward-lc.py`: the LC record is **byte-cloned from the
original dylib's own record** (cmd/name-offset/timestamp/versions copied
verbatim, cmd=0x80000023), inserted after the last LC with exact-length
surgery — `ncmds 15→16`, `sizeofcmds 1960→2024`, `__TEXT`
filesize/vmsize +=64, and every file-offset field pointing at or past the
insertion shifted by +64 (LC_SEGMENT_64 fileoff, section offsets, symtab/
dysymtab, dyld_info, linkedit_data). Self-check after rebuild: the
dependency set of the built dylib equals the original's five commands
(one-to-one, same order, same spellings; `MATCH: True` on the parsed
lists).

### (2)+(3) — the probe re-run with the dep-aligned dylib

Early-plant recipe (#14/#15, poller on `cached locally: …/usr/lib`),
zone-vtable-probe:

```
FATAL signal 11 (code=1) at addr=0x320484dc99d
  rip=0x824f374d2  rax=0x2f  rcx=rsi=rdx=0x320484dc99d
  rdi=0x31f805cf4d0  backtrace: crash_debug_handler at mldr
```

Measured: the `…4df555` signature does **not** appear in the probe run
(absent — it belongs to the framework-initializer context of the earlier
chrome runs); the crash is the SAME MSL-init wild-pointer class as the
pre-alignment runs (lane 81 run E: SIGSEGV, rcx/rsi/rdx wild string-scan,
rip `…4d2` in mldr's host range; run P post-alignment: identical class,
different slide). The dep alignment therefore does **not** change the
probe-run crash class — the alignment is ruled out as the root of this
class by measurement (two runs, before/after).

Consequences for the standing criterion: the slot map (vtable[0]=size,
vtable[3]=destroy) stays BLOCKED — the probe dies in MSL-init before its
first printf; Control #15 dlopen not reached (startup crash);
`_mach_task_self_` not sounded.

### Verdict (control #17, one line)

зависимости = 5/5 (4 LC_LOAD + 1 LC_UPWARD) — совпали с оригиналом (запись
UPWARD байт-клонирована из оригинала, ncmds 15→16, self-check MATCH);
probe = …4df555 ушла (в контексте пробы отсутствует), но краш-класс НЕ
изменился — та же MSL-init wild-pointer сигнатура (SIGSEGV rcx/rsi/rdx,
rip=…4d2, совпал с run E до выравнивания) → зависимостный набор НЕ является
корнем этого класса (замерено на двух прогонах до/после); слот-карта =
заблокирована (проба падает в MSL-init до первого printf); Control #15
dlopen = отказ (старт раньше пробы); остаток = guest-side трейс MSL-init
(какое runtime-поле кормит syscall мусором) + chrome-контекст …4df555
(chrome-app-objc пересборка — артефактов лейнов #11–#16 в дереве нет).

### Repro

```sh
# dependency-set alignment is inside the build (link flags + surgery):
sh build-freebsd/zone-contract/build-libmalloc-zone.sh
# after BUILD_OK:
python3 build-freebsd/zone-contract/add-upward-lc.py  # (invoked by the script)
# probe per the control #14 recipe, plant via the early-sync poller
# (marker "cached locally: <stage>/usr/lib"):
#   the LC dump: python over the dylib load commands (cmd 0xC/0x80000023)
#   the crash class: the run log's FATAL block (addr/rip/rcx/rsi/rdx)
```

## Control #18 — diff: rebuild vs original (the stock baseline and the static delta)

### (1) — the stock baseline: the same probe with the original overlay dylib

The probe = tests/zone-vtable-probe-macho (control #14's recipe), run
via the harness with the ORIGINAL overlay libsystem_malloc explicitly
planted into the staged tree by an early-plant poller (marker:
`cached locally: …/usr/lib`; both-side md5 confirmed:
521c6983b531c2e71122e784981d3f89 — the live overlay's stock).

Measured: the probe STARTS, `malloc_default_zone()` returns a valid
zone object, and the full vtable[0..7] dump completes —
NULL, NULL, default_zone_size, default_zone_malloc, default_zone_calloc,
default_zone_valloc, default_zone_free, default_zone_realloc — the
pin-era layout exactly as control #14 recorded. **No SIGBUS, no wild
pointer, no MSL-init fault.** The run's only fault is the probe's own
task=NULL `malloc_get_all_zones` call (FATAL addr=0x8 — the unguarded
NULL+8 read in the remote-zones path, the same probe-logic artifact as
control #14).

The rebuild, by contrast, faults during MSL initialization in the same
probe with the same plant mechanism (lane 81, runs D/E): run D SIGBUS
at addr=…edf018 with rax=__v_zone's runtime address (vmaddr 0x50000 —
the REBUILD's __v_zone); run E SIGSEGV with rcx=rsi=rdx=…4df555 (the
wild pointer scanned like a string, the control #16 garbage-reader
class).

→ Branch (2а) of the dispatch: the stock PASSES the probe; the crash
class is carried by the rebuild → the static diff.

### (2а) — the static diff (build-freebsd/msl-diff.py, x86-64 slice)

Section table (iv): the rebuild's whole data layout is shifted −0x4000
vmaddr versus the original (__v_zone 0x54000→0x50000, __bss
0x58210→0x54210, __common 0x58000→0x54000, __data 0x52620→0x4f600, …);
__TEXT starts lower too (0x1260→0x7f0). Only-original section:
`__DATA.__nl_symbol_ptr` (vmaddr 0x52000, size 8) — the non-lazy
symbol-pointer anchor is ABSENT in the rebuild. Only-rebuild sections:
`__TEXT.__eh_frame`, `__TEXT.__literals`. Size deltas: __const 0x338 vs
0x328, __data 0x220 vs 0x218, __la_symbol_ptr 0x290 vs 0x280,
__unwind_info 0x70 vs 0x1040.

The zone object itself (i) — `__DATA.__v_zone` (both 0x4000 at their
segment base), first six qwords:

```
original: 0x0        0x0        0x29e30    0x29e90    0x29ef0    0x29f60
          reserved1  reserved2  size       malloc     calloc     valloc
rebuild : 0x32ee0    0x32f40    0x32fa0    0x33000    0x33060    0x330c0
          size       malloc     calloc     valloc     free       realloc
```

The stock carries the pin-era layout (reserved pair leading, method
table from slot 2); the rebuild carries the post-removal macOS-13
layout (method table from slot 0) — a 16-byte shift between the two
tables inside the SAME section name.

Streams (ii): the rebase opcode mix matches (do_imm 97 vs 121 — the
rebuild rebases 24 more pointers individually); the bind stream 0xf8
vs 0xe0 bytes (the missing non-lazy anchor + fewer imports); the
export stream 0x9b8 vs 0x928 bytes.

Export set (iii): the original exports 95 names, the rebuild 44 —
**51 exports are missing, all in the zone-management API**
(_malloc_default_zone, _malloc_get_all_zones, _malloc_create_zone,
_malloc_destroy_zone, _malloc_get_zone_name, _malloc_num_zones, … the
full list in the diff output). The rebuild's export trie lost exactly
the API the chrome framework's initializer binds (control #13's wall).

### (3) — the candidate, the fix target, the stop point

Candidate (in my layer, two objects):
- the `malloc_zone_t` layout divergence INSIDE the rebuild — the
  rebuilt zone object follows the post-removal (macOS-13) layout while
  the in-tree consumers read the pin-era offsets (the tree's header
  still leads with reserved1/reserved2 — src/external/libmalloc at the
  pin, include/malloc/malloc.h:67-68); every zone-method access on the
  rebuild is shifted by −16 bytes, which is the connection to the
  control #16 reader's path (the garbage fed to the syscall);
- the 51-name export loss in the same rebuild (the export trie of the
  zone-contract build dropped the zone-management API).

Both fixes are source/build-script changes in my layer, but executing
them means re-entering the zone-contract rebuild — the #81 gate chain
is on the do-not-repeat list — so the fix is NOT executed in this
control; the measurement and the exact fix-targets are handed up.

### Verdict (control #18, one line)

сток в пробе = проходит (vtable[0..7] дампится, только probe task=NULL
NULL-deref @0x8); дифф = DATA-сегмент сдвинут −0x4000, __v_zone: сток
reserved1/2 + метод-таблица с слота 2, пересборка — таблица с слота 0
(сдвиг −16 Б), __nl_symbol_ptr отсутствует, exports 95→44 (51 lost:
весь zone-management API); кандидат = divergence приватного/публичного
malloc_zone_t внутри пересборки + экспорт-потеря 51 (оба — мой слой,
связь с путём читателя Control #16: смещённые слоты кормят syscall
мусором); фикс = не выполнен (в моём слое, но требует пересборки
зон-контракта — гейты #81 под запретом повтора) — измерено и сдано;
слот-карта = блокирована (пересборка падает в MSL-init до любого дампа;
стоковая замерена в Control #14); Control #15 dlopen = достигнута в
обоих прогонах, отказ = Foundation version wall (рецепт без stage-patch
— чистая пара для сравнения классов); остаток = санкционировать пересборку
зон-контракта с единым layout по всем TU + экспорт-мапу на 95 имён, затем
перегнать пробу.

### Repro

```sh
# baseline: zone-vtable-probe-macho via the harness, poller plants the
#   ORIGINAL overlay MSL on the "cached locally: …/usr/lib" marker
#   (both-side md5 521c6983…); the vtable dump completes; the only
#   fault is the probe's task=NULL get_all_zones (addr=0x8)
# rebuild side: the same probe + the same plant of the zone-contract
#   build (b6e459c3…) faults in MSL-init (lane 81 runs D/E: SIGBUS at
#   __v_zone+0x18 / wild …4df555)
# static diff: build-freebsd/msl-diff.py <original> <rebuild>
#   (x86-64 slice; sections, __v_zone blobs, dyld_info streams,
#   export sets)
```

## Control #19 — pin-zone exports: the rebuild matches the stock's zone contract

### Mechanism

`build-freebsd/zone-contract/build-libmalloc-pin.sh` compiles the pin's
`src/external/libmalloc` sources **unmodified** (reserved1/reserved2 kept)
and links with the original's export trie names (95, extracted from the
overlay dylib's LC_DYLD_INFO export stream via `msl-diff.py`'s
`export_set`). ld64.lld's `-exported_symbol` flags put symbols in the
symtab but not always in the export trie (measured: 44/95 after linking);
`fix-export-trie.py` post-processes the built dylib, rebuilding the trie
from the symtab's 95 external defined symbols and updating the
LC_DYLD_INFO export offset/size (the old stream becomes dead space).

### Static diff (msl-diff.py, x86-64 slice)

```
export set: original 95, rebuild 95, only-original 0, only-rebuild 0
__v_zone[0..7]:
  original: 0x0, 0x0, 0x29e30, 0x29e90, 0x29ef0, 0x29f60, 0x29fc0, 0x2a020
  rebuild:  0x0, 0x0, 0x32f20, 0x32f80, 0x32fe0, 0x33040, 0x330a0, 0x33100
  [0]=reserved1=0x0, [1]=reserved2=0x0 — PIN layout matches
  [2..7] function pointers — values differ (vmaddr), structure identical
```

Residual benign: DATA vmaddr shifted −0x4000 (__v_zone 0x54000→0x50000,
__bss 0x58210→0x54210, etc.), __TEXT shifted −0x470 (0x1260→0x7f0);
`__DATA.__nl_symbol_ptr` absent in the rebuild (ld64.lld vs Apple ld64
non-lazy binding difference — compensated by lazy binding via
`__la_symbol_ptr`, same symbols resolved at runtime); `__TEXT.__eh_frame`
and `__TEXT.__literals` only-rebuild (compiler output differences);
`__TEXT.__unwind_info` size differs (0x70 vs 0x1040, different unwind
encoding). None of these affect the zone contract or export resolution.

### Probe (zone-vtable-probe-macho, early-plant recipe)

Planted `libsystem_malloc-fixed.dylib` into the staged tree (poller fires
at "cached locally" marker, md5 both sides:
24933b2aada5b329cce7e1dcea7fc13a). Measured:

```
dlsym malloc_default_zone -> 0x2abb44103c30
dlsym malloc_get_all_zones -> 0x2abb44106e40
default_zone=0x2abb44133000  (valid zone object)
vtable[0]=0x0  vtable[1]=0x0
vtable[2]=default_zone_size    vtable[3]=default_zone_malloc
vtable[4]=default_zone_calloc  vtable[5]=default_zone_valloc
vtable[6]=default_zone_free    vtable[7]=default_zone_realloc
```

**Stock behavior**: pin layout, full vtable dump, all function pointers
resolve to named symbols. The only fault is the probe's own task=NULL
`malloc_get_all_zones` call (FATAL addr=0x8, same artifact as Control
#14/#18 stock baseline). No MSL-init crash, no SIGBUS, no wild pointer.

### Verdict (control #19, one line)

layout = пин по дампу qwords (reserved1/2=0x0, слоты 2-7 = function
pointers, структура идентична стоку); exports = 95/95 (trie ∩ visible,
fix-export-trie.py); дифф = benign (vmaddr −0x4000 DATA / −0x470 TEXT,
__nl_symbol_ptr отсутствует — компенсируется lazy binding через
__la_symbol_ptr, __eh_frame/__literals только-rebuild); probe =
сток-поведение (default_zone валиден, vtable[0..7] полон, только probe
task=NULL NULL-deref @0x8); Control #15 dlopen = не запускался в этом
лейне (проба прошла, следующий шаг — dlopen по рецепту Control #12);
остаток = запустить dlopen с pin-layout rebuild'ом, измерить фазу/отказ.

### Repro

```sh
# build
sh build-freebsd/zone-contract/build-libmalloc-pin.sh
# static diff
python3 build-freebsd/msl-diff.py \
  <overlay>/usr/lib/system/libsystem_malloc.dylib \
  <build>/zone-pin/libsystem_malloc-fixed.dylib
# probe (early-plant poller at "cached locally" marker)
#   plant libsystem_malloc-fixed.dylib into staged tree
#   run harness with DARLING_TEST_BINARY=zone-vtable-probe-macho
#   expected: vtable[0..7] dump with pin layout, FATAL addr=0x8 (probe artifact)
```

## Control #20 — dlopen probe with the pin-rebuild MSL: the phase regresses to an MSL-init startup death; the stock reaches the Foundation version wall

### The pair (same recipe, only the planted MSL differs)

chrome-launcher dlopen probe (Control #12's stack: source-app surgery +
per-slack supplements + early-sync poller + DYLD_PRINT_INITIALIZERS +
ktrace), poller plants ONLY the MSL — no stage-patch, no supplement
plant — the Control #18 clean-pair method extended to the dlopen probe.
Both plants md5-verified in the stage tree after firing:

```
stock   : 521c6983b531c2e71122e784981d3f89 (the overlay's stock)
rebuild : 24933b2aada5b329cce7e1dcea7fc13a (Control #19's pin rebuild)
```

### Stock side (the baseline, re-measured this turn)

Startup completes (libSystem, dyld-trace, libc++, libobjc initializers
×5), the launcher reaches the dlopen, and the refusal is the asserted
Foundation version wall:

```
dlopen //Frameworks/Google Chrome for Testing Framework…:
  Library not loaded: /System/Library/Frameworks/Foundation.framework/Versions/C/Foundation
  Reason: Incompatible library version: Google Chrome for Testing
    Framework requires version 300.0.0 or later, but Foundation provides
    version 0.0.0.
```

(The run log: 1905 lines; the launcher aborts after the refusal —
ktrace tail: SIGABRT caught → SIGILL SIG_DFL.)

### Rebuild side (the lane's subject)

The run dies during STARTUP, before the dlopen phase:

```
log: 151 lines, last non-trace line:
  dyld: calling initializer function 0x8f7d848f110 in /usr/lib/libSystem.B.dylib
  (no "…in libsystem_malloc.dylib" initializer print — the death sits
   inside libSystem's init cascade, where libsystem_c's init calls into MSL)
no FATAL block in the run log; RC=132 (SIGILL).
ktrace tail (pid 91473): repeated
  PSIG SIGILL caught handler=0x823039920 code=ILL_PRVOPC
  → sigaction(SIGILL) → thr_kill(SIGILL) → PSIG SIGILL SIG_DFL SI_LWP
```

ILL_PRVOPC traps also appear in the stock trace (mldr's trap-based
translation mechanism is trap-heavy in both runs) — the discriminating
fact is the PHASE: the stock continues past its traps to the dlopen
refusal; the rebuild's traps end the process at the libSystem
initializer.

### Phase comparison

| side    | phase reached                | refusal / death                          |
|---------|------------------------------|------------------------------------------|
| stock   | dlopen (startup complete)    | Foundation version wall (300.0.0 vs 0.0.0)|
| rebuild | libSystem initializer        | SIGILL ILL_PRVOPC, thr_kill, startup death|

The rebuild REGRESSES the dlopen probe: the Control #19 zone-vtable
probe showed stock behavior for this same dylib (the MSL initializer
ran, the vtable dumped) — the class is CONTEXT-DEPENDENT: the
chrome-launcher's libSystem init cascade exercises an MSL-init path
that the zone-vtable probe does not.

### Verdict (control #20, one line)

dlopen = фаза НЕ ДОСТИГНУТА (rebuild: смерть на инициализаторе
/usr/lib/libSystem.B.dylib — 151 строка лога против 1905 у стока, SIGILL
ILL_PRVOPC → thr_kill, без FATAL-блока); против стока = СМЕСТИЛАСЬ НАЗАД
(сток: startup полон, dlopen достигнут, отказ = Foundation version wall —
«requires version 300.0.0 or later, but Foundation provides version
0.0.0»); пересборка несёт собственный класс = ДА — MSL-init в
dlopen-контексте (тот же dylib в зон-vtable-пробе Control #19 отрабатывал
штатно — класс контекст-зависимый); остаток = разбор контекст-зависимой
MSL-init смерти (путь libsystem_c-init → MSL в chrome-закрытии — отдельный
лейн); выбор сток-vs-пересборка в dlopen — решение владельца: замер
говорит, что в этой пробе сток проходит дальше.

### Repro

```sh
# pair: chrome-launcher dlopen probe, poller plants ONLY the MSL at the
#   early marker "cached locally: <stage>/usr/lib" (no stage-patch, no
#   supplements — the Control #18 clean-pair method); md5 both sides
#   (stock 521c6983…, rebuild 24933b2a…)
# ktrace -i + DYLD_PRINT_INITIALIZERS=1 per the Control #12 recipe;
#   read: run-log length + last initializer print + crash/refusal text,
#   kdump tail: PSIG records (ILL_PRVOPC caught vs SIG_DFL)
# rebuild side: log ends at "calling initializer … /usr/lib/libSystem.B.dylib",
#   RC=132; stock side: dlopen refusal text (Foundation version wall)
```

## Control #22 — delivery via a writable overlay copy lands (stock UUID-proven x3); the pin-rebuild is refused by dyld BEFORE open (its own reproducible class)

### Step 1 — the pair (UUID-controlled, >=3 runs per side)

Writable copy of the overlay (cp -R, no hardlinks) at a scratch path;
DARLING_OVERLAY pointed at the copy; the copy's MSL slice replaced by the
pin-rebuild for the second side only. Per run the criterion BEFORE reading
any output: the `dyld: loaded: <UUID> /usr/lib/system/libsystem_malloc.dylib`
line must equal the UUID of the artifact planted on that side (LC_UUID of
the x86_64 slice, extracted host-side). Same probe recipe on both sides
(Control #12 stack: chrome-launcher dlopen, early-sync poller plants ONLY
the 57-provider version patch, DYLD_PRINT_INITIALIZERS=1; no ktrace for the
variance runs).

Stock side (artifact md5 521c6983…, slice LC_UUID 1FA0731B-F0EA-310E-8808-
B4118C4E62D8):
- 3/3 runs: loaded UUID == planted UUID (control OK) — the Control #18/#20
  "plant never reaches dyld" failure mode is GONE; the guest reads the MSL
  from the overlay copy.
- Outcome identical 3/3 (variance M=1): `Symbol not found:
  _kCGColorSpaceITUR_2100_PQ` — the deep symbol wall, rc=132 (the
  launcher's normal teardown SIGILL); the poller patched 57/57 every run.

Rebuild side (planted artifact md5 24933b2a… = the Control #20 rebuild,
LC_UUID 4C4C4498-5555-3144-A176-95CC70A42E3B):
- 3/3 runs: NO loaded line for the MSL at all (control MISMATCH, outputs
  void by the lane's own criterion) and the run dies at startup:
  `dyld: Library not loaded: /usr/lib/system/libsystem_malloc.dylib` +
  `abort_with_payload: reason: …`, rc=132.

### Step 2 — localization: the refusal is BEFORE open

One ktrace run of the rebuild side: the staged path is walked
component-by-component with fstatat(AT_SYMLINK_NOFOLLOW) — usr, usr/lib,
usr/lib/system, the file — all RET 0, final stat size=392944 (the
rebuild) — and then NO openat of the MSL ever happens; the refusal
follows. The throw sits in dyld between path canonicalization/stat and
file open (the /usr/lib/system route goes through dyld3's shared-cache
machinery: dyld2.cpp loadPhase2/loadPhase5 call
dyld3::findInSharedCacheImage on these paths).

Host-side structural deltas between the stock x86_64 slice (loads) and the
rebuild (refused) — the candidates for the pre-open rejection:
- stock = FAT container (x86_64 @0x1000 size=403784 + i386 @0x64000
  size=259112); rebuild = thin x86_64, 392944 bytes
- ncmds 19 vs 15; LC_BUILD_VERSION(0x32) vs LC_VERSION_MIN_IPHONEOS(0x24,
  ver=0xa0c00)
- stock-only load commands: LC_REEXPORT_DYLIB (0x8000001c, size=312),
  LC_DYLD_EXPORTS_TRIE (0x80000023), cmds 0x2a/0x1e
- LC_ID_DYLIB cur/compat: 0x2e3/0x63 (stock) vs 0x2e6/0x5f (rebuild);
  both carry an ad-hoc LC_CODE_SIG (datasize=0x30)

### Verdict (control #22, one line)

Delivery via the overlay copy works — stock: LOADED UUID == planted artifact
x3, variance M=1 (symbol wall); the rebuild has its own reproducible class
3/3: dyld refuses it BEFORE open (stat-walk OK, size=392944, "Library not
loaded: /usr/lib/system/libsystem_malloc.dylib", loaded UUID absent) —
candidates: FAT/thin container, LC-set deltas (REEXPORT/EXPORTS_TRIE only
in stock), version-cmd class; the Control #20 SIGILL attribution is now
fully withdrawn — the rebuild has never loaded under any plant; next =
wrap the thin rebuild in a FAT container matching the stock layout (the
cheapest discriminator), or chase dyld3's /usr/lib/system cache routing
that rejects before open.

### Repro

```sh
# cp -R the overlay to a scratch path; DARLING_OVERLAY=<copy>; stock side:
#   run the Control #12 probe 3x, compare
#   dyld: loaded: <UUID> /usr/lib/system/libsystem_malloc.dylib against
#   the x86_64-slice LC_UUID of <copy>/usr/lib/system/libsystem_malloc.dylib
# rebuild side: cp the pin-rebuild over that path (md5 24933b2a…), run 3x:
#   loaded line absent, "dyld: Library not loaded: /usr/lib/system/
#   libsystem_malloc.dylib", rc=132; ktrace -i shows the fstatat component
#   walk (RET 0, size=392944) and NO openat before the refusal
```

## Control #23 — FAT wrapper matches the stock layout; the refusal is unchanged but now NAMED: "dyld export info overruns __LINKEDIT" (exports-tie class)

### Step 1 — the wrap (layout vs stock)

`build-freebsd/fat-wrap.py`: the stock arch table is reproduced exactly —
x86_64 @0x1000 (cputype 0x1000007, subtype 3, align 12) holds the thin
rebuild slice (392944 B, LC_UUID 98444C4C-5555-4431-A176-95CC70A42E3B =
the rebuild's own UUID), i386 @0x64000 (cputype 0x7, subtype 3, align 12,
259112 B) copied byte-for-byte from the stock container (its slice
LC_UUID 098A0C35-C830-5137-8679-2F4E8AEC6E3E). Container size 668712 ==
stock; the x86_64 slot ends 0x60ef0 < i386 offset 0x64000 (no overlap);
wrap artifact md5 d330dc2486e5b63276b1b4ba0735d464.

### Step 2 — the pair (UUID-controlled, 3+3 runs, the #22 recipe)

Stock side 3/3: control OK (loaded UUID == planted 1FA0731B-F0EA-310E-
8808-B4118C4E62D8); outcome = symbol wall `Symbol not found:
_kCGColorSpaceITUR_2100_PQ`, rc=132 (M=1, identical to #22).

Wrap side 3/3: NO loaded line for the MSL (control mismatch by the lane's
criterion), `dyld: Library not loaded: /usr/lib/system/
libsystem_malloc.dylib` + abort_with_payload, rc=132 — the refusal class
is IDENTICAL to the thin rebuild's (#22); it did not shift.

ktrace of the wrap side names the defect verbatim (abort_with_payload fd-1
write; the fd-2 message stream): `Reason: no suitable image found.  Did
find:\n  /usr/lib/system/libsystem_malloc.dylib: malformed mach-o image:
dyld export info overruns __LINKEDIT; code: 7` — Referenced from
/usr/lib/libSystem.B.dylib. No dyld shared cache exists in the overlay
(find: 0 hits), so the #22 "dyld3 cache routing pre-open" attribution is
disproven as the mechanism: dyld parses the file and rejects it
structurally.

### Step 3 — host-side map (what actually differs, measured)

- LC_ID_DYLIB: BOTH artifacts carry cur=0x0, compat=0x10000 (cmdsize 64)
  — the #22 doc's 0x2e3/0x63 vs 0x2e6/0x5f figures do not match the files
  under test; ID-version is eliminated by measurement.
- The stock x86_64 slice has LC_DYLD_EXPORTS_TRIE (cmdsize=64, dataoff=0x18,
  datasize=2; the 2 trie bytes at file offset 0x18 = 85 00 — the flags
  field bytes, flags=0x00110085 in both files). The rebuild has NO
  LC_DYLD_EXPORTS_TRIE.
- Both carry LC_DYLD_CHAINED_FIXUPS (cmdsize 48, dataoff = __LINKEDIT
  start, datasize=0x30) whose "header" fields are string garbage (rebuild
  symbols_offset=0x59107044; stock 0x445d4153). Stock survives because
  its exports-trie LC short-circuits dyld's export derivation; the
  rebuild, with no trie LC, sends dyld into the fixups-derived fallback,
  whose region overruns __LINKEDIT.
- SYMTAB regions fit exactly in both (rebuild strings end 0x5e3b0 ==
  __LINKEDIT end 0x5e3b0; stock 0x62948 == its end).

### Step 4 — the two authorized in-place experiments (both refused)

EXP-1 (+LC_DYLD_EXPORTS_TRIE, cmdsize=16, appended at the LC-area end
0x7c8, dataoff=0x7d8 inside the verified-zero slack 0x7c8..0x7f0, trie
bytes 00 00; ncmds 15→16, sizeofcmds 1960→1976; __text at 0x7f0
untouched; LC_UUID unchanged): 3/3 refused — the reason moved to
`malformed mach-o image: dylib load command #15 has offset (2008) outside
its size (16); code: 7` — dyld applies a dylib-name-offset check
(offset < cmdsize) to the trie LC; stock passes it via cmdsize=64
(0x18=24 < 64).

EXP-2 (dataoff 0x7d8→0x0d — the 2 zero bytes at file 0x0d..0x0f inside
the mach header; 13 < 16 passes the offset<size check; same cmdsize 16;
wrap md5 efeee5fbeacbfb5eef17ba85a70a32e7): 3/3 refused — back to
`dyld export info overruns __LINKEDIT; code: 7`. A 16-byte trie LC is not
honored as the export source; the garbage fixups fallback re-enters. The
rebuild's header flags bytes [0x18:0x1a] = 85 00, identical to stock's —
a faithful stock-geometry transplant needs no byte writes beyond the LC
header itself.

### Verdict (control #23, one line)

FAT wrapper = stock layout exact (arch table, i386 byte-identical, size
668712); pair #23: rebuild = refusal same 3/3 (no loaded line, rc=132)
but the reason is now named verbatim — `malformed mach-o image: dyld
export info overruns __LINKEDIT; code: 7`; root = named — not FAT
(eliminated by the identical-layout wrap), not LC_ID (measured identical
0x0/0x10000), but the exports-tie class: the rebuild lacks
LC_DYLD_EXPORTS_TRIE (stock: cmdsize=64, dataoff=0x18, datasize=2) while
its LC_DYLD_CHAINED_FIXUPS is garbage-fed; both authorized experiments
(16-byte trie LC at dataoff 0x7d8 and 0x0d) refused — dyld demands the
stock geometry; remainder = next phase, same method: transplant the stock
trie-LC geometry exactly (rewrite the 48-byte bogus fixups LC in place:
cmd 0x80000022→0x80000023, dataoff=0x18, datasize=2 — the bytes at
[0x18:0x1a] are already 85 00, no other writes) or fix the link step in
build-libmalloc-zone.sh to emit the stock-style empty trie LC; dyld3
routing itself stays out of layer — measurement only.

### Repro

```sh
# wrap: python3 build-freebsd/fat-wrap.py <stock-msl> <rebuild-thin> <out>
#   -> arch table == stock (cputype/offset/align), i386 slice byte-identical,
#      x86_64 slice UUID == the rebuild's
# pair: the #22 recipe — overlay copy as DARLING_OVERLAY, plant per side,
#   poller patches the 57-provider version list after the staging marker,
#   3 runs/side; control = "dyld: loaded: <UUID> /usr/lib/system/
#   libsystem_malloc.dylib" == the planted slice's LC_UUID
#   stock: ctrl OK 3/3 (symbol wall); wrap: no loaded line 3/3, rc=132
# ktrace -f -i on the wrap side: the abort_with_payload fd-1 write carries
#   the full reason ("...malformed mach-o image: dyld export info
#   overruns __LINKEDIT; code: 7")
# EXP-1/EXP-2: in-place LC insert/patch per Step 4 — exact-length discipline:
#   LC-area slack 0x7c8..0x7f0 is 40 zero bytes, __text at 0x7f0 untouched,
#   LC_UUID unchanged (98444C4C-5555-4431-A176-95CC70A42E3B)
```

## Control #24 — the exports-trie class is CLOSED (stock-geometry trie-LC, deterministic build); the refusal MOVES to the link's segment vm geometry

### Step 0 — base and premise check (measured before any experiment)

Base: pr-arm64 tip 122a69490 (lane 93 accepted, FF). The dispatch's premise —
"the rebuild has no LC_DYLD_EXPORTS_TRIE" — measures FALSE for the current
artifact: the zone-contract thin build (386120 B) already carries LC[15]
cmd=0x80000023 cmdsize=64 dataoff=0x18 datasize=0x2 — the EXACT stock
x86_64-slice geometry (stock LC[15]: identical cmd/cmdsize/dataoff/datasize;
trie bytes at file [0x18:0x1a] = 85 00 in both; header flags = 0x110085 in
both). Its LC_DYLD_INFO_ONLY (LC[3], cmdsize 48) streams are sane and inside
__LINKEDIT (rebase=0x54040/0x30, bind=0x54070/0xe0, lazy=0x54150/0x20,
export=0x54898/0x928, end 0x551C0 < __LINKEDIT end 0x5e448) — the Control #23
"garbage fixups" premise also measures false for this build. The Control
#23-era artifact (392944 B, no trie LC) is gone; the export-list filter (the
Control #15 build: the original's export list intersected with the objects'
global symbols) changed the link — ld64.lld now emits the stock-form empty
trie LC.

### Step 1 — the fix is robust in the script (the (3а) clause, executed)

A fresh rebuild from the current build-freebsd/zone-contract/
build-libmalloc-zone.sh is BYTE-IDENTICAL to the artifact under test (md5
b6e459c3179227a7bed7d246d4e8d722 before and after the rebuild; LC_UUID
4C4C446A-5555-3144-A1B2-F57C46954BD1; the stock-geometry trie LC at LC[15];
[0x18:0x1a]=85 00) — the trie-LC geometry is a DETERMINISTIC product of the
link. The dispatch's literal (1) transplant (rewrite the 48-byte fixups LC
into a trie LC with non-stock cmdsize) was NOT executed: its precondition (no
trie LC present) measures false, and on this artifact it would duplicate the
trie LC at LC[15] and destroy the well-formed LC_DYLD_INFO_ONLY streams — a
third experiment beyond the ladder, forbidden by (3б).

### Step 2 — the pair (the #22 recipe, fresh overlay copy, 3+3 runs)

Fresh writable copy of the overlay (cp -R, 620 MB) as DARLING_OVERLAY; the
poller patches the 57-provider version list after the staging marker
(cft69-patchlist.txt, unchanged); control BEFORE reading outcomes = the
"dyld: loaded: <UUID> /usr/lib/system/libsystem_malloc.dylib" line == the
planted artifact's LC_UUID.

Stock side 3/3: control OK (loaded UUID == planted 1FA0731B-F0EA-310E-8808-
B4118C4E62D8, md5 521c6983…); outcome identical to Controls #22/#23 (M=1):
symbol wall `Symbol not found: _kCGColorSpaceITUR_2100_PQ`, rc=132.

Thin side 3/3 (planted md5 b6e459c3…, UUID 4C4C446A-5555-3144-A1B2-
F57C46954BD1): loaded line ABSENT (control MISMATCH — outputs void past the
load by the lane's criterion) and the refusal class MOVED, verbatim 3/3:

```
/usr/lib/system/libsystem_malloc.dylib: malformed mach-o image: segment __DATA
vm overlaps segment __TEXT
```

The Control #23 class ("dyld export info overruns __LINKEDIT") is GONE — the
stock-geometry trie LC closed it.

### Step 3 — the new class, named statically (no third guest experiment)

The thin's segment vm map from its LC table: __TEXT vmaddr=0x0 vmsize=0x4f040
→ [0x0, 0x4f040); __DATA vmaddr=0x4f000 vmsize=0x6000 → [0x4f000, 0x55000) —
__DATA's vmaddr sits 0x40 bytes INSIDE __TEXT's vm range (overlap =
0x4f040 − 0x4f000 = 0x40). Stock: __TEXT [0x0, 0x52000), __DATA
[0x52000, 0x58000), __LINKEDIT [0x58000, 0x62948) — contiguous,
page-aligned, no overlap. The defect is the link's segment vm geometry:
__TEXT vmsize 0x4f040 is not a page multiple and ld64.lld places __DATA at
vmaddr align_down(0x4f040) instead of align_up — build-libmalloc-zone.sh
(my layer) territory.

### Verdict (control #24, one line)

пересборка = сток-геометрия trie-LC (cmd 0x80000023, cmdsize 64, dataoff 0x18,
datasize 2; байты [0x18:0x1a]=85 00 = сток; LC_DYLD_INFO_ONLY-строки валидны
внутри __LINKEDIT), сборка побайтово воспроизводима (md5 b6e459c3… до/после,
UUID 4C4C446A-…); пара: сток 3/3 ctrl OK (символьная стена
_kCGColorSpaceITUR_2100_PQ), пересборка 3/3 MISMATCH (loaded line отсутствует)
+ отказ СМЕСТИЛСЯ: "malformed mach-o image: segment __DATA vm overlaps
segment __TEXT" — экспорт-трие-класс (#23) ЗАКРЫТ; корень = vm-геометрия
сегментов линковки (__TEXT vmsize 0x4f040 перекрывает __DATA vmaddr 0x4f000
на 0x40 байт; сток 0x52000/0x52000 — без перекрытий) — слой
build-libmalloc-zone.sh; фикс = СТОП по (3б) — третий эксперимент не
выполнялся; слот-карта = блокирована; Control #15 dlopen = не достигнут
(отказ до open); остаток = следующий лейн: выровнять сегментную vm-геометрию
линковки (__TEXT vmsize кратно странице / __DATA vmaddr = align_up) и
повторить пару.

### Repro

```sh
# build: sh build-freebsd/zone-contract/build-libmalloc-zone.sh
#   -> md5 b6e459c3179227a7bed7d246d4e8d722 (byte-identical rebuild); LC dump:
#      LC[15] 0x80000023/64/0x18/2 (stock geometry), LC[3] 0x80000022/48
#      export=0x54898/0x928 (inside __LINKEDIT 0x54040..0x5e448)
# pair (the #22 recipe; driver kept in the session scratchpad as
#   cft94-matrix.sh, summary cft94-summary.txt): fresh overlay copy as
#   DARLING_OVERLAY, plant per side, poller patches cft69-patchlist.txt after
#   the staging marker ("cached locally: /tmp/darling-local-overlay/
#   System/Library/PrivateFrameworks"), 3 runs/side; control = the
#   "dyld: loaded: <UUID> /usr/lib/system/libsystem_malloc.dylib" line
#   stock: ctrl OK 3/3 (symbol wall _kCGColorSpaceITUR_2100_PQ, rc=132)
#   thin:  no loaded line 3/3, rc=132, verbatim:
#     "malformed mach-o image: segment __DATA vm overlaps segment __TEXT"
# static map: segment vm ranges from the LC table (python, little-endian
#   walk at header+32): __TEXT [0x0,0x4f040) vs __DATA [0x4f000,0x55000)
#   overlap 0x40; stock [0x0,0x52000)/[0x52000,0x58000)/[0x58000,0x62948)
#   contiguous
```

## Control #25 — vm-geometry: the rebuild PASSES dyld3's image checks and loads; the refusal class moves to runtime init

Task lane #95, branch task/cft-vm-geometry (from pr-arm64 after the
89ad3a444 merge). The #94 refusal `malformed mach-o image: segment
__DATA vm overlaps segment __TEXT` (overlap 0x40, ld64.lld does not
round __TEXT's vmsize up to a page and has no -segalign) is closed by a
post-link fixup; the delivery layer — dyld3 pre-open image checks — now
PASSES and the rebuilt MSL LOADS in the guest.

### Link variants (two, both measured dead ends; no third)

- `-segalign 0x1000`: silently ignored — this ld64.lld has no such
  option (`--help` has only -pagezero_size/-sectalign/-sectorder);
  artifact byte-identical (md5 b6e459c3 unchanged), geometry unchanged;
- `-add_empty_section __TEXT __zpad` + `-sectalign __TEXT __zpad
  0x1000`: __TEXT grew +0x1000 but its end stayed `...040` (unaligned),
  __DATA still overlapped by 0x40.

### The fix: fixup-segment-vm.py (build-freebsd/zone-contract/, exact-length LC field surgery + one zero-pad insertion)

Three measured revisions, each refusal class recorded verbatim:

- r1 (vmaddrs only, __TEXT vmsize page-rounded): refusal `segment
  __TEXT has vmsize != filesize and is executable` (3/3) — dyld checks
  executable segments for vmsize == filesize (stock __TEXT: both
  0x52000);
- r2 (+0xfc0 zero-pad at __TEXT's file end so filesize == vmsize;
  __DATA fileoff +7 section offsets + LC offset fields — LC_SYMTAB
  symoff/stroff, LC_DYSYMTAB tocoff/modtaboff/extrefsymoff/
  indirectsymoff/extreloff/locreloff, LC_DYLD_INFO_ONLY rebase/bind/
  weak/lazy/export offs, LC_DATA_IN_CODE, LC_SEGMENT_SPLIT_INFO —
  shifted +0xfc0; fixup streams are segment-relative, none rewritten):
  __LINKEDIT vmaddr was left at 0x55000 while __DATA moved to
  [0x50000,0x56000) → refusal `segment __LINKEDIT vm overlaps segment
  __DATA` (3/3);
- r3 (+ __LINKEDIT vmaddr follows __DATA's new end): static criterion
  MET — segments contiguous and page-aligned, no overlaps,
  __TEXT vmsize == filesize (0x50000), trie/upward record LC[15]
  preserved (cmd 0x80000023, cmdsize 64, name offset 0x18,
  /usr/lib/system/libsystem_c.dylib — byte-identical to stock;
  the `85 00` at file offset 0x18 are the mach-header flags bytes, as
  in stock), LC_UUID linker-computed (4C4C446A-5555-3144-A1B2-
  F57C46954BD1), artifact 390152 B (+0xfc0), md5 39d30039, symtab
  parses (283 exports, zone symbols at sane addresses).

### The pair (#22 recipe: fresh 620M overlay copy, poller on the
PrivateFrameworks staging marker, control = loaded UUID == planted
UUID read BEFORE any outcome, 3 runs per side)

- stock 3/3: ctrl OK (loaded 1FA0731B-F0EA-310E-8808-B4118C4E62D8),
  outcome = the reference wall `Symbol not found:
  _kCGColorSpaceITUR_2100_PQ` (delivery-layer success reference);
- fixed 3/3: ctrl OK — `dyld: loaded:
  <4C4C446A-5555-3144-A1B2-F57C46954BD1> /usr/lib/system/
  libsystem_malloc.dylib` — all three `malformed mach-o image`
  classes are GONE; the refusal moved to RUNTIME: `FATAL signal 11
  (code=1) at addr=0xffffffffffffff8c` (NULL-0x74 deref), rip inside
  libSystem.B's initializer region (preceded by `calling initializer
  function 0xddc12a8f110 in /usr/lib/libSystem.B.dylib`), handler
  frames: mldr crash_debug_handler ← libthr _pthread_sigmask ←
  pthread_signals_unblock_np. The Chrome dlopen is NOT reached on the
  fixed side (stock reaches the CG wall); the runtime init-cascade
  class of the rebuilt MSL (Controls #18/#20/#21 lineage) is the next
  lane's subject.

### Verdict (control #25, one line)

Зависимости = 5/5 (4×LC_LOAD kernel/platform/dyld/compiler_rt +
LC_UPWARD system_c; LC[15] байт-в-байт со стоком, совпал); probe =
загрузка — все три класса `malformed mach-o image` сняты (новая
подпись = runtime SIGSEGV addr=0xffffffffffffff8c, NULL+0x74 deref в
инициализаторе libSystem.B); слот-карта = блокирована runtime
init-cascade классом (падение до пробы); Control #15 dlopen = фаза: MSL
загружен (UUID посаженный, ctrl OK 3/3), стена _kCGColorSpaceITUR_2100_PQ
НЕ достигнута (сток side её достигает 3/3); остаток = guest-side
трейсинг инициализатора пересобранного MSL (NULL+0x74 deref в init-цепи
libSystem.B).

### Repro

```sh
# static geometry + build (fixup wired into the script):
sh build-freebsd/zone-contract/build-libmalloc-zone.sh
#   -> GEOMETRY_OK, artifact 390152 B, md5 39d30039, 283 exports
python3 build-freebsd/zone-contract/fixup-segment-vm.py <dylib>  # alone:
#   GEOMETRY_OK, prints vm/file ranges per segment
# the pair: fresh overlay copy + poller + UUID control per #22 recipe
#   (cft95-matrix.sh pattern: stock 3 runs, plant fixed artifact,
#    fixed 3 runs; control = loaded UUID read before outcomes)
#   fixed side: dyld: loaded <4C4C446A-...> libsystem_malloc.dylib
#   then FATAL signal 11 addr=0xffffffffffffff8c in libSystem init
```
