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
# with CHROME_APP pointing at the patched app copy
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
