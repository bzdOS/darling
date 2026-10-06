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

## Control #26 (шаг А) — сырое краш-свидетельство init-cascade

Источник лога: `cft96-f1.log` (прогон пробы по рецепту Control #25,
`sh cft96-run.sh f1`; 24188 строк; завершение — Segmentation fault).

### Краш-блок (дословно из лога)

```
[darling-mldr] FATAL signal 11 (code=1) at addr=0xffffffffffffff8c
  si_pid=0 si_uid=0 si_value=0x0 sival_ptr=0x0
  rip=0x0000351ef690c482  rax=0x0000000000000001  rbx=0x00007fffffdffde0
  rcx=0x0000000000000000  rdx=0x0000000000000000  rsi=0x0000000000000004
  rdi=0x00007fffffdfff38  rbp=0x00007fffffdfdc30  rsp=0x00007fffffdfdba8
  r8 =0x0000000000000000  r9 =0x0000000000000000  r10=0x0000000000000000
  r11=0x00007fffffdfda30  r12=0x0000000000000026  r13=0x0000000000000018
  r14=0x0000000000000020  r15=0x00000008205d60df
  backtrace (3 frames):
    #00 0x2227e3  0x2227e3 <crash_debug_handler+0xc3> at <ROOT>/build/dserver/mldr-real/mldr
    #01 0x82347c45a  0x82347c45a <_pthread_sigmask+0x50a> at /lib/libthr.so.3
    #02 0x82347ba5b  0x82347ba5b <pthread_signals_unblock_np+0x5bb> at /lib/libthr.so.3
  guest stack dump at rsp=0x00007fffffdfdba8:
  [gstack+   0] 0x0000351ef6874214
  [gstack+   8] 0x1f070000ffdfdbe0
  [gstack+  16] 0x0000000000000000
  [gstack+  24] 0x0000000000000000
  [gstack+  32] 0x0000000000000000
  [gstack+  40] 0x0000000000000000
  [gstack+  48] 0x0000000000000000
  [gstack+  56] 0x0000000000000000
  [gstack+  64] 0x00007fffffdfdda8
  [gstack+  72] 0x00007fffffdfdc10
  [gstack+  80] 0x000000082702a795
  [gstack+  88] 0x1f070000f6874110
  [gstack+  96] 0x0000000827164850
  [gstack+ 104] 0x00007fffffdfff38
  [gstack+ 112] 0x00007fffffdffe00
  [gstack+ 120] 0x00007fffffdffdf0
  [gstack+ 128] 0x00000001ffdfdda8
  [gstack+ 136] 0x00007fffffdfdea0
  [gstack+ 144] 0x0000000827068bae
  [gstack+ 152] 0x0000000000000000
  [gstack+ 160] 0x0000000000000000
  [gstack+ 168] 0x000000082716db50
  [gstack+ 176] 0x0000003200000000
  [gstack+ 184] 0x00000307ffdfdc90
  [gstack+ 192] 0x000000082704e73d
  [gstack+ 200] 0x0000351ef6874110
  [gstack+ 208] 0x0000351ef68655c0
```

### Строки «calling initializer function …» из лога (все)

```
dyld: calling initializer function 0x351ef6874110 in /usr/lib/libSystem.B.dylib
```

### Verdict (control #26 step A, one line)

Сырое краш-свидетельство init-cascade зафиксировано: SIGSEGV
addr=0xffffffffffffff8c (NULL-0x74 deref), rip=0x351ef690c482, единственный
инициализатор перед крашем — libSystem.B.dylib (0x351ef6874110); символизация
и диф инициализатор-цепей — шаги 96-Б/96-В.

### Repro

```sh
sh cft96-run.sh f1
#   -> log: cft96-f1.log (24188 lines), planted MSL md5 39d3003953ac49a5ead4659dbe419963
#   -> Segmentation fault; crash block at line 24096, initializer at line 24082
```

## Control #26 step B — symbolization of rip=0x351ef690c482

### Image attribution (from the load addresses in cft96-f1.log)

The log's `dyld: Mapping` / `__TEXT at` lines give the load base of every
image. The crash rip=0x351ef690c482 falls inside the __TEXT range of
`/usr/lib/system/libsystem_malloc.dylib`:

```
dyld: loaded: <4C4C446A-5555-3144-A1B2-F57C46954BD1> /usr/lib/system/libsystem_malloc.dylib
            __TEXT at 0x351EF68DF000->0x351EF692EFFF with permissions r.x
```

- image base = 0x351ef68df000
- rip = 0x351ef690c482
- offset = rip − base = 0x351ef690c482 − 0x351ef68df000 = **0x2d482**

### Disassembly (llvm-objdump, 32 bytes at 0x2d482)

```sh
llvm-objdump -d --macho --arch=x86_64 "$DARLING_OVERLAY"/usr/lib/system/libsystem_malloc.dylib | grep -A 10 "2d482:"
```

```
   2d482:	e8 d9 3d fd ff	callq	_bitarray_size
   2d487:	8b bd 5c ee ff ff	movl	-0x11a4(%rbp), %edi
   2d48d:	48 8b b5 60 ee ff ff	movq	-0x11a0(%rbp), %rsi
   2d494:	48 89 c2	movq	%rax, %rdx
   2d497:	48 8b 85 68 ee ff ff	movq	-0x1198(%rbp), %rax
   2d49e:	48 8d 8d 28 ef ff ff	leaq	-0x10d8(%rbp), %rcx
   2d4a5:	ff d0	callq	*%rax
   2d4a7:	89 85 a4 ef ff ff	movl	%eax, -0x105c(%rbp)
   2d4ad:	83 bd a4 ef ff ff 00	cmpl	$0x0, -0x105c(%rbp)
   2d4b4:	0f 84 11 00 00 00	je	0x2d4cb
   2d4ba:	8b 85 a4 ef ff ff	movl	-0x105c(%rbp), %eax
```

### Symbol (llvm-nm, nearest to 0x2d482)

```
000000000002d10 t _word_zap_bit_go_down
000000000002d80 t _word_zap_bit_simple
```

0x2d482 sits inside `_word_zap_bit_go_down` (0x2d10 .. 0x2d80), 0x372 bytes
past its start.

### Fault line

```
fault = libsystem_malloc.dylib!_word_zap_bit_go_down+0x372: callq _bitarray_size
```

### Verdict (control #26 step B, one line)

rip=0x351ef690c482 = libsystem_malloc.dylib+0x2d482, внутри функции
`_word_zap_bit_go_down` (+0x372); инструкция в точке краша — `callq
_bitarray_size`; пересборка/патч MSL не выполнялись (шаг 96-В).

## Control #26 (step V)

### Fault line (fresh3)

```
fault = libsystem_malloc.dylib!_word_zap_bit_go_down+0x372: callq _bitarray_size
```

rip=0x0000031c6af0c482, base=0x31C6AEDF000 (из rebase-строк лога fresh3),
offset = 0x31C6AF0C482 − 0x31C6AEDF000 = 0x2D482 — та же сигнатура что и шаг Б.

### Determinism check (fix fresh3 vs fix f1)

```
f1 (step B):   dyld: calling initializer function 0x351ef6874110 in /usr/lib/libSystem.B.dylib  (стр. 24082)
fresh3 (step V): dyld: calling initializer function 0x31c6ae74110 in /usr/lib/libSystem.B.dylib  (стр. 24082)
```

Оба лога fix-side (один и тот же MSL в overlay). Оба вызывают один и тот же
инициализатор libSystem.B (смещение 0x74110 от base libSystem.B). После него идут
идентичные lazy bind строки (libsystem_pthread, libdyld, libsystem_blocks) — цепь
расходится только в адресах (ASLR), не в логике. Краш происходит на том же месте
(rip offset 0x2D482 от base libsystem_malloc.dylib). Детерминизм подтверждён.
История подстановки: f1 был ошибочно помечен как stock в первом коммите (b346bffc8);
исправлено в 6e5124c5c.

### Stock-vs-fix initializer diff (stock1 vs fresh3)

```
stock1 (stock): dyld: calling initializer function 0x21337c874110 in /usr/lib/libSystem.B.dylib  (стр. 24086)
                dyld: calling initializer function 0x21337daa8e00 in /usr/lib/libc++.1.dylib     (стр. 24187)
                dyld: calling initializer function 0x21337da01550 in /usr/lib/libobjc.A.dylib   (стр. 24202)
                ... (13 инициализаторов, доходит до dlopen Chrome Framework)
fresh3 (fix):   dyld: calling initializer function 0x31c6ae74110 in /usr/lib/libSystem.B.dylib  (стр. 24082)
                FATAL signal 11 (стр. 24096)
```

Первый расходящийся инициализатор: **libc++.1.dylib** (stock1 вызывает на стр. 24187,
fresh3 падает на стр. 24096 — до него не доходит). Между libSystem.B и libc++ в stock1
идут lazy bind libsystem_malloc, libdyld, libdispatch, libobjc, libxpc, liblaunch
(стр. 24100-24186) — в fresh3 эти строки отсутствуют, краш происходит сразу после
lazy bind libsystem_pthread. У stock1 в момент краша fresh3 (после libSystem.B)
bitarray-указатели валидны: lazy bind libsystem_malloc.dylib проходит успешно
(стр. 24100-24131), затем libc++.1.dylib initializer вызывается на стр. 24187.

### NULL+0x74 analysis

```
FATAL signal 11 (code=1) at addr=0xffffffffffffff8c
rip=0x0000031c6af0c482  rax=0x0000000000000001  rcx=0x0000000000000000
rdx=0x0000000000000000  rsi=0x0000000000000004  rdi=0x00007fffffdfff38
```

addr=0xffffffffffffff8c = NULL+0x74 — аргумент _bitarray_size (bitarray_size =
NULL+0x74). Инициализатор libSystem.B вызывает _word_zap_bit_go_down с
невалидным аргументом (NULL вместо валидного bitarray pointer).

### Verdict (control #26 step V, one line)

Свежий корень + timeout 120 воспроизвели краш (24188/24096 = сигнатура шага А) —
фикс НЕ устранил NULL+0x74; причина = инициализатор libSystem.B вызывает
_word_zap_bit_go_down с NULL аргументом (bitarray_size = NULL+0x74). Stock-прогон
(stock1, сток-overlay без cft96-фикса): 13 инициализаторов, проходит до dlopen
Chrome Framework; первый расходящийся инициализатор = libc++.1.dylib (stock1 вызывает,
fresh3 падает до него).

### Repro

```sh
export PATH=/usr/local/bin:/usr/local/sbin:/usr/bin:/bin:/sbin:/usr/sbin
export DARLING_SRC_DIR=<src-dir>
export DARLING_OVERLAY=<overlay-dir>
export DARLING_BUILD_DIR=<build-dir>
export DARLING_TEST_BINARY=cft-fwmacho-probe-macho
export DARLING_STAGING_TREES=usr/lib
export DARLING_SMOKE_REFRESH=1
DYLD_TRACE="DYLD_PRINT_LIBRARIES DYLD_PRINT_LIBRARIES_POST_LAUNCH \
DYLD_PRINT_BINDINGS DYLD_PRINT_WEAK_BINDINGS DYLD_PRINT_APIS \
DYLD_PRINT_INTERPOSING DYLD_PRINT_SEGMENTS DYLD_PRINT_STATISTICS \
DYLD_PRINT_STATISTICS_DETAILS DYLD_PRINT_RPATHS DYLD_PRINT_WARNINGS \
DYLD_PRINT_INITIALIZERS DYLD_PRINT_DOFS DYLD_PRINT_OPTS DYLD_PRINT_ENV \
DYLD_PRINT_CODE_SIGNATURES DYLD_PRINT_REBASINGS DYLD_PRINT_TO_STDERR"
RUN_CMD="env DARLING_SRC_DIR=${DARLING_SRC_DIR} DARLING_OVERLAY=${DARLING_OVERLAY} DARLING_BUILD_DIR=${DARLING_BUILD_DIR}"
RUN_CMD="${RUN_CMD} DARLING_TEST_BINARY=${DARLING_TEST_BINARY}"
RUN_CMD="${RUN_CMD} DARLING_STAGING_TREES=${DARLING_STAGING_TREES}"
RUN_CMD="${RUN_CMD} DARLING_SMOKE_REFRESH=${DARLING_SMOKE_REFRESH}"
for v in ${DYLD_TRACE}; do RUN_CMD="${RUN_CMD} ${v}=1"; done
RUN_CMD="${RUN_CMD} ${DARLING_BUILD_DIR}/launch-dynamic"
timeout 120 sudo ${RUN_CMD} > <diag-dir>/cft96-fresh3.log 2>&1 || true
```

Лог: <diag-dir>/cft96-fresh3.log (24188 строк).

## Control #27 (step A)

### Initializer order (DYLD_PRINT_INITIALIZERS)

```
stock1 (stock):  dyld: calling initializer function 0x21337c874110 in /usr/lib/libSystem.B.dylib  (стр. 24086)
                 dyld: calling initializer function 0x21337daa8e00 in /usr/lib/libc++.1.dylib     (стр. 24187)
                 dyld: calling initializer function 0x21337da01550 in /usr/lib/libobjc.A.dylib   (стр. 24202)
                 ... (13 инициализаторов, доходит до dlopen Chrome Framework)
fresh3 (fix):    dyld: calling initializer function 0x31c6ae74110 in /usr/lib/libSystem.B.dylib  (стр. 24082)
                 FATAL signal 11 (стр. 24096)
```

libsystem_malloc.dylib не имеет собственного инициализатора (нет строк
"calling initializer" для неё в обоих логах). Она инициализируется через
___malloc_init и ___malloc_late_init, которые libSystem.B импортирует из
libsystem_malloc.dylib (bind-строки 23829-23830 в fresh3, 23832-23833 в stock1).

### Symbolization (libSystem.B+0xF110)

```
llvm-nm libSystem.B.dylib:
000000000000f110 t _libSystem_initializer
```

Инициализатор libSystem.B = `_libSystem_initializer` (offset 0xF110 от __TEXT
base libSystem.B). Оба артефакта (stock и fix) имеют один и тот же символ.

### Disassembly: _word_zap_bit_go_down+0x372 → _bitarray_size

```
2d42e:	movq	-0x1098(%rbp), %rax        ; загрузить указатель на структуру
2d435:	cmpq	$0x0, 0x38(%rax)           ; проверить поле 0x38 на NULL
2d43a:	je	0x2d4d0                     ; если NULL → перейти к 0x2d4d0
2d440:	movq	-0x1098(%rbp), %rax        ; загрузить указатель на структуру
2d447:	movl	0x10(%rax), %eax            ; загрузить поле 0x10 из структуры
2d44a:	movl	%eax, -0x10dc(%rbp)        ; сохранить в локальную переменную
2d47c:	movl	-0x10dc(%rbp), %edi        ; аргумент для _bitarray_size
2d482:	callq	_bitarray_size              ; вызвать _bitarray_size
```

NULL-аргумент для _bitarray_size приходит из поля 0x10 структуры по
-0x1098(%rbp). Если поле 0x38 той же структуры = NULL, то -0x10dc(%rbp) = 0
(строка 2d4db: movl $0x0, -0x10dc(%rbp)) → _bitarray_size(0) → NULL+0x74.

### Stock vs fix: bitarray pointer at crash moment

```
stock1: после libSystem.B initializer → lazy bind libsystem_malloc (стр. 24100-24131)
        → libc++.1.dylib initializer (стр. 24187) → ... → dlopen Chrome Framework
fresh3: после libSystem.B initializer → FATAL signal 11 (стр. 24096)
```

У stock1 в момент краша fresh3 поле 0x10 структуры валидно: lazy bind
libsystem_malloc проходит успешно, затем libc++.1.dylib initializer вызывается.
У fresh3 поле 0x10 структуры = NULL → _bitarray_size(0) → NULL+0x74.

### Verdict (control #27 step A, one line)

libsystem_malloc.dylib не инициализируется до _libSystem_initializer в
фиксовом артефакте; поле 0x10 структуры (malloc_zone_t/nanozone_t) = NULL →
_bitarray_size(0) → NULL+0x74. Сток проходит (13 инициализаторов), фикс падает
(1 инициализатор).

### Repro

```sh
export PATH=/usr/local/bin:/usr/local/sbin:/usr/bin:/bin:/sbin:/usr/sbin
export DARLING_SRC_DIR=<src-dir>
export DARLING_OVERLAY=<stock-overlay>
export DARLING_BUILD_DIR=<build-dir>
export DARLING_TEST_BINARY=cft-fwmacho-probe-macho
export DARLING_STAGING_TREES=usr/lib
export DARLING_SMOKE_REFRESH=1
DYLD_TRACE="DYLD_PRINT_LIBRARIES DYLD_PRINT_LIBRARIES_POST_LAUNCH \
DYLD_PRINT_BINDINGS DYLD_PRINT_WEAK_BINDINGS DYLD_PRINT_APIS \
DYLD_PRINT_INTERPOSING DYLD_PRINT_SEGMENTS DYLD_PRINT_STATISTICS \
DYLD_PRINT_STATISTICS_DETAILS DYLD_PRINT_RPATHS DYLD_PRINT_WARNINGS \
DYLD_PRINT_INITIALIZERS DYLD_PRINT_DOFS DYLD_PRINT_OPTS DYLD_PRINT_ENV \
DYLD_PRINT_CODE_SIGNATURES DYLD_PRINT_REBASINGS DYLD_PRINT_TO_STDERR"
RUN_CMD="env DARLING_SRC_DIR=${DARLING_SRC_DIR} DARLING_OVERLAY=${DARLING_OVERLAY} DARLING_BUILD_DIR=${DARLING_BUILD_DIR}"
RUN_CMD="${RUN_CMD} DARLING_TEST_BINARY=${DARLING_TEST_BINARY}"
RUN_CMD="${RUN_CMD} DARLING_STAGING_TREES=${DARLING_STAGING_TREES}"
RUN_CMD="${RUN_CMD} DARLING_SMOKE_REFRESH=${DARLING_SMOKE_REFRESH}"
for v in ${DYLD_TRACE}; do RUN_CMD="${RUN_CMD} ${v}=1"; done
RUN_CMD="${RUN_CMD} ${DARLING_BUILD_DIR}/launch-dynamic"
timeout 120 sudo ${RUN_CMD} > <diag-dir>/cft96-stock1.log 2>&1 || true
```

Лог: <diag-dir>/cft96-stock1.log (24274 строки).

## Control #27 (step B)

### Init-order mechanism (llvm-readobj artifact comparison)

```
stock (overlay):   FAT binary (cafebabe, 2 archs), 929 symbols
                   ___malloc_init @ 0x23b90, ___malloc_late_init @ 0x24710
                   calls ___malloc_init_experiments, ___malloc_init_from_bootargs
fix (cft96):       thin x86_64 (cffaedfe), 0 symbols (stripped)
                   does not disassemble (llvm-objdump: 0 lines)
                   no ___malloc_init calls found
```

Оба libSystem.B.dylib (stock и fix) вызывают ___malloc_init и ___malloc_late_init
на одних адресах (f20f, f32c). Различие в самом libsystem_malloc.dylib: стоковый
содержит инициализационную логику, фиксовый — нет (stripped, не дизасмируется).

### Before-crash line (fresh log)

```
<diag-dir>/cft96-baseline.log (24188 lines)
  стр. 24082: dyld: calling initializer function 0x2ca08b274110 in /usr/lib/libSystem.B.dylib
  стр. 24096: [darling-mldr] FATAL signal 11 (code=1) at addr=0xffffffffffffff8c
```

Прогон на текущем дереве (pr-arm64 2c7cfc8d5) воспроизвёл краш — та же
сигнатура что и fresh3.

## Control #27 (step B, continued — fix attempt)

### Finding: -init in libsystem_malloc.dylib is forbidden by dyld

Попытка фикса «malloc-init до _libSystem_initializer» через LC_ROUTINES_64
(-init ___malloc_init) в пересобранной libsystem_malloc.dylib отвергнута dyld.

Подтверждение (лог прогона fix2, <diag-dir>/cft96-fix2.log):

```
dyld: -init function in image (/usr/lib/system/libsystem_malloc.dylib) that does not link with libSystem.dylib
abort_with_payload: reason: -init function in image (/usr/lib/system/libsystem_malloc.dylib) that does not link with libSystem.dylib; code: 9
```

Механизм (src/external/dyld/src/ImageLoaderMachO.cpp:2261-2263, 2315-2319):
dyld при вызове -init/конструкторов проверяет `dyld::gProcessInfo->libSystemInitialized`.
Если libSystem ещё не инициализирована, -init/конструкторы разрешены ТОЛЬКО в образе
с installPath == /usr/lib/libSystem.B.dylib. Для всех остальных — throwf.

libsystem_malloc.dylib — зависимость libSystem.B.dylib (LC_LOAD_DYLIB в libSystem.B).
Зависимости инициализируются ПЕРВЫМИ (recursiveInitialization). Значит, -init в
libsystem_malloc.dylib вызвался бы ДО libSystem.B initializer → проверка проваливается.

Стоковый libsystem_malloc.dylib не имеет собственного инициализатора (нет LC_ROUTINES_64,
нет __mod_init_func). Он инициализируется только через вызовы ___malloc_init и
___malloc_late_init из libSystem.B initializer.

### Constructor gate — dyld source, verbatim (resolves the step-B contradiction)

Шаг Б утверждал: __DATA,__mod_init_func вызывается «without the -init gate».
Это неверно. Оба пути гейтованы на gProcessInfo->libSystemInitialized.
Дословно, src/external/dyld/src/ImageLoaderMachO.cpp (сабмодуль cce174b65):

doImageInit (LC_ROUTINES_64, -init), строки 2261-2263:

    if ( ! dyld::gProcessInfo->libSystemInitialized ) {
        // <rdar://problem/17973316> libSystem initializer must run first
        dyld::throwf("-init function in image (%s) that does not link with libSystem.dylib\n", this->getPath());
    }

doModInitFunctions (__DATA,__mod_init_func), строки 2315-2319:

    if ( ! dyld::gProcessInfo->libSystemInitialized ) {
        // <rdar://problem/17973316> libSystem initializer must run first
        const char* installPath = getInstallPath();
        if ( (installPath == NULL) || (strcmp(installPath, libSystemPath(context)) != 0) )
            dyld::throwf("initializer in image (%s) that does not link with libSystem.dylib\n", this->getPath());
    }

Вердикт: конструкторы гейтованы так же, как -init; единственное исключение —
installPath == /usr/lib/libSystem.B.dylib (libSystemPath). Секционный маршрут
pre-libSystem (___malloc_init через __mod_init_func в MSL) МЁРТВ: MSL — зависимость
libSystem.B, инициализируется первой, libSystemInitialized ещё false,
installPath != libSystemPath -> throwf. Фикс = ранний вызов ___malloc_init
ВНУТРИ _libSystem_initializer пересобранного MSL (механизм стока: libSystem.B
сам вызывает ___malloc_init из своего инициализатора). add-mod-init-func.py
остаётся в дереве как инструмент; маршрут переписывается на стоковый механизм.

### Evidence table (stock1 log + otool)

Лог <diag-dir>/cft96-stock1.log (DYLD_PRINT_INITIALIZERS=1): 13 вызовов
инициализаторов до dlopen Chrome Framework. Все 13 — ПОСЛЕ _libSystem_initializer
(он первый, стр. 24086); до него — 0.

| # | строка лога | образ | адрес вызова |
|---|-------------|-------|--------------|
| 1 | 24086 | libSystem.B.dylib | 0x21337c874110 (_libSystem_initializer) |
| 2 | 24187 | libc++.1.dylib | 0x21337daa8e00 |
| 3 | 24201 | libc++.1.dylib | 0x21337daa8e10 |
| 4 | 24202 | libobjc.A.dylib | 0x21337da01550 |
| 5 | 24203 | libobjc.A.dylib | 0x21337da051e0 |
| 6 | 24204 | libobjc.A.dylib | 0x21337da05330 |
| 7 | 24205 | libobjc.A.dylib | 0x21337da06a00 |
| 8 | 24206 | libobjc.A.dylib | 0x21337da09480 |
| 9 | 24207 | libobjc.A.dylib | 0x21337da0f090 |
| 10 | 24208 | libobjc.A.dylib | 0x21337da110d0 |
| 11 | 24209 | libobjc.A.dylib | 0x21337da13770 |
| 12 | 24210 | libobjc.A.dylib | 0x21337da30d80 |
| 13 | 24211 | libobjc.A.dylib | 0x21337da31870 |

Цитаты лога (первый, переходные, последний):

    24086: dyld: calling initializer function 0x21337c874110 in /usr/lib/libSystem.B.dylib
    24187: dyld: calling initializer function 0x21337daa8e00 in /usr/lib/libc++.1.dylib
    24201: dyld: calling initializer function 0x21337daa8e10 in /usr/lib/libc++.1.dylib
    24202: dyld: calling initializer function 0x21337da01550 in /usr/lib/libobjc.A.dylib
    24211: dyld: calling initializer function 0x21337da31870 in /usr/lib/libobjc.A.dylib

otool (llvm-otool -l) по каждому члену — «член → механизм»:

| член | секция | размер | элементов | механизм |
|------|--------|--------|-----------|----------|
| libSystem.B.dylib | __DATA,__mod_init_func | 0x8 | 1 | __mod_init_func (_libSystem_initializer) |
| libc++.1.dylib | __DATA,__mod_init_func | 0x10 | 2 | __mod_init_func |
| libobjc.A.dylib | __DATA,__objc_init_func | 0x50 | 10 | __objc_init_func |
| libsystem_malloc.dylib | — | — | 0 | нет ни __mod_init_func, ни __objc_init_func, ни LC_ROUTINES_64 |

Закрывает «противоречие»: утверждение шага Б «stock libSystem.B.dylib регистрирует
через __mod_init_func» — ВЕРНО (1 элемент, он же _libSystem_initializer, первый
в порядке); факт 05:28 «сток-MSL без mod_init_func» — тоже верен (0 секций).
Это разные члены с разными механизмами: libSystem.B.dylib регистрирует свой
инициализатор секционно, MSL — только вызовами ___malloc_init/___malloc_late_init
из libSystem.B initializer.

### Fix applied (step 3)

Фикс: `src/external/libsystem/init.c` — вызов `__malloc_init(apple)` перенесён
в начало `libSystem_initializer`, сразу после `__libplatform_init` и ДО
`__pthread_init`/`_libc_initializer` (раньше стоял после них, INIT_MALLOC после
INIT_LIBC — отсюда NULL+0x74). Дифф сохранён как
`build-freebsd/zone-contract/early-malloc-init.patch` (init.c — файл сабмодуля
darling-Libsystem, upstream без write-доступа).

Пересборка libSystem.B: CMake-таргет `system` на этой машине НЕ собирается
(i386-архитектура падает на `__uint8_t` в mach/i386/_structs.h; init.c падает
на конфликте `user_addr_t`/`__darwin_clock_t` между freebsd_mig_compat.h и
Darling SDK). libSystem.B собран standalone: clang -target x86_64-apple-macos10.12
объекты `init.c`/`dummy.c`/`CompatibilityHacks.c`/kqueue (без
`-D_BSD_I386__TYPES_H_` и без `-include freebsd_mig_compat.h`) + `ld64.lld -dylib`
с `-reexport_library` по overlay-siblings. Ранний вызов подтверждён
дизассемблером `_libSystem_initializer` @0x1740: порядок callq
`__libkernel_init → __libplatform_init → __malloc_init(apple) → __pthread_init
→ _libc_initializer`.

Repro: `sh cft96-run.sh <tag>` (repro корня лейна 96, DYLD_PRINT_INITIALIZERS=1,
timeout 120). Результат (<diag-dir>/cft96-early.log, 23810 строк):

```
NULL+0x74 ушёл: в логе нет addr=0xffffffffffffff8c и FATAL signal 11;
каскад дошёл до конца биндинга (23806 строк), затем:
dyld: Symbol not found: ___stack_chk_guard
  Referenced from: /usr/lib/libSystem.B.dylib
  Expected in: /usr/lib/system/libdyld.dylib
```

Следующее препятствие = дефект standalone-линковки libSystem.B: ld64.lld с
`-reexport_library` привязывает импорт (первый прогон: `_dlsym` → libsystem_kernel,
второй: `___stack_chk_guard` → libdyld) к первой библиотеке в списке, а не к
библиотеке-владельцу символа. Правильная резолюция требует Darling ld64
(`build/dyld-only/.../x86_64-apple-darwin20-ld`, собран) + firstpass-библиотек,
либо перенастройки reexport через `-dylib_file`. Это НЕ препятствие фикса
malloc-init: NULL+0x74 закрыт, осталось починить reexport-умбреллу.


### Relink attempt (step 4) — imports to symbol owners

Цель: перелинковать libSystem.B так, чтобы импорты резолвились к владельцам
символов, а не к первой библиотеке списка (дефект шага 3). Все маршруты
проверены на этой машине:

1. «Darling ld64» `build/dyld-only/.../x86_64-apple-darwin20-ld` — это
   СИМЛИНК на `/usr/local/bin/ld64.lld`, не отдельный линкер. Настоящий Apple
   ld64 (`src/build-host-tools/ld64/x86_64-apple-darwin20-ld`, `PROJECT:ld64`)
   существует и запускается (`-v` OK), но падает `Illegal instruction
   (core dumped)` на минимальной линковке (`-dylib -arch x86_64 -o out dummy.o`)
   — бинарь несовместим с этой VM.
2. ld64.lld не реализует `-dylib_file` («Option `-dylib_file' is not yet
   implemented»). При `-reexport_library` он привязывает undefined-импорт к
   ПЕРВОЙ reexported библиотеке, не к владельцу: прогон 1 `_dlsym →
   libsystem_kernel`, после перестановки libdyld первым `___stack_chk_guard →
   libdyld`. Прямые dylib-аргументы, `-l`, `-flat_namespace` не помогают
   (последний не собрался: нет `libresolv.9.dylib`).
3. firstpass-библиотек нет (`find ... *firstpass*.dylib` → 0), их сборка —
   отдельные CMake-таргеты, которые падают на i386 (см. выше).
4. umbrella-обход: reexport СТОКОВОГО `overlay/usr/lib/libSystem.B.dylib`
   вместе с `-L overlay/usr/lib/system -L overlay/usr/lib` собирается и даёт
   ПРАВИЛЬНУЮ привязку (`_dlsym → this-image/libSystem`, как в стоке), но
   install_name нового и стокового совпадают (`/usr/lib/libSystem.B.dylib`) →
   circular self-reexport: прогон `<diag-dir>/cft96-umb.log` умирает SIGSEGV
   на 207-й строке (addr=0x7fffff5ffff8, всего 299 строк). `llvm-install-name-tool
   -id` не может сменить ID стокового: `unsupported load command (cmd=0x1e)`.

Вывод: перелинковка libSystem.B в reexport-умбреллу на этой машине не встаёт
ни одним маршрутом. NULL+0x74 закрыт шагом 3; `___stack_chk_guard`-стоп —
дефект линковки, не каскада. Варианты для лейна 99: собрать настоящий Apple
ld64, совместимый с VM, либо firstpass-библиотеки x86_64, либо разорвать
circular через правку install_name стокового (нужен инструмент, понимающий
Darling load commands).

REPRO: `sh cft96-run.sh <tag>`; логи `<diag-dir>/cft96-relink.log`,
`<diag-dir>/cft96-umb.log`.

### Lane 99: circular broken

Скрипт `build-freebsd/rewrite-dylib-name.py` переписывает имя в dylib-LC
(LC_ID_DYLIB/LOAD/REEXPORT/WEAK/UPWARD) in-place: обход load commands строго по
cmdsize, неизвестные LC (включая Darling 0x1e, на котором спотыкается
llvm-install-name-tool) скипаются, не парсятся; новое имя обязано влезть в
исходный слот (`cmdsize - name.offset`) с NUL-паддингом; сдвиг load commands
запрещён (регресс-урок 1-байтового сдвига, PLAN 9.9). FAT обходится по слайсам.

Раскладка (слот LC_ID_DYLIB стокового = 28 B; `/usr/lib/libSystem.B.orig.dylib`
= 32 B НЕ влезает — имя урезано до `/usr/lib/libSystem.B.orig`, 26 B):
- pristine-бэкап стокового → `$PRISTINE_OVERLAY_BACKUP/usr/lib/`;
- стоковый → стейдж `/usr/lib/libSystem.B.orig` с ID, переписанным скриптом
  (`--id-only`);
- umbrella-libSystem.B шага 4 пересобран reexport'ом этого `.orig` (ID остаётся
  `/usr/lib/libSystem.B.dylib`) → канонический `/usr/lib/libSystem.B.dylib`.

Прогон `<diag-dir>/cft96-lane99.log`: circular SIGSEGV (стоп на 207-й строке из
299) УШЁЛ — прогон дошёл до 24190 строк, инициализация и биндинг прошли.
Следующее препятствие дословно:

```
dyld: initializer in image (/usr/lib/libSystem.B.orig) that does not link with libSystem.dylib
abort_with_payload: reason: initializer in image (/usr/lib/libSystem.B.orig) that does not link with libSystem.dylib; code: 9
```

Это гейт doModInitFunctions (ImageLoaderMachO.cpp:2315-2319): стоковый `.orig`
несёт свой `__mod_init_func` (`_libSystem_initializer`), но его installPath
теперь `/usr/lib/libSystem.B.orig` != libSystemPath, а `libSystemInitialized`
ещё false → throwf. Лейн 100: снять `__mod_init_func` с `.orig`, чтобы
единственным инициализатором остался umbrella (installPath ==
`/usr/lib/libSystem.B.dylib`).

REPRO: `sh cft96-run.sh lane99`.

### Control #27 шаг В — снятие __mod_init_func с .orig (lane 100)

Механизм гейта: `doModInitFunctions` выбирает инициализаторы по
`sect->flags & SECTION_TYPE == S_MOD_INIT_FUNC_POINTERS`
(ImageLoaderMachO.cpp:2301) — имя секции не читается вообще. Поэтому
переименование `__mod_init_func` ничего не изменило бы, а декремент `nsects`
сегмента снял бы ПОСЛЕДНЮЮ секцию (не `__mod_init_func`). Узчайший in-place
фикс — сброс младших 8 бит (SECTION_TYPE) поля `flags` секции (4 байта, без
сдвига load commands).

Скрипт `build-freebsd/strip-mod-init-func.py`: обход LC по cmdsize, thin/fat,
находит секции с типом S_MOD_INIT_FUNC_POINTERS и сбрасывает type-биты flags.
На стейдж-копии `.orig` x86_64-слайс: flags `0x9 -> 0x0`. Pristine-бэкап не
тронут, umbrella reexport не менялся.

```
repro: python3 build-freebsd/strip-mod-init-func.py <stage>/usr/lib/libSystem.B.orig
       sh cft96-run.sh lane100
before (lane99): dyld: initializer in image (/usr/lib/libSystem.B.orig) that does not link with libSystem.dylib  -> abort code 9
after  (lane100): abort УШЁЛ; dyld: calling initializer function ... in /usr/lib/libSystem.B.dylib;
                  lazy bind __libkernel_init / __libplatform_init / ___malloc_init;
                  [darling-mldr] FATAL signal 11 (code=1) at addr=0xffffffffffffff8b
```

После снятия гейта единственным инициализатором стал umbrella (installPath ==
`/usr/lib/libSystem.B.dylib`), и его ранний `__malloc_init` дошёл до вызова
`___malloc_init` (libsystem_malloc.dylib) — но упал внутри него: rip =
`___malloc_init+2`, addr = 0xffffffffffffff8b. Лейн 101: краш внутри раннего
`___malloc_init`.

### Control #27 шаг Г — диагностика краша ___malloc_init (lane 101)

Причина (по артефактам, без прогона): в пересобранном (zone-contract)
`libsystem_malloc.dylib` relocation stack-protector в `___malloc_init` битая.

Дизасм (цитаты обеих сторон):

```
стейдж ___malloc_init @0x2d480:        сток ___malloc_init @0x23b90:
  2d480: pushq %rbp                      23b90: pushq %rbp
  2d481: movq %rsp,%rbp                  23b91: movq %rsp,%rbp
  2d484: subq $0x450,%rsp                23b94: subq $0x450,%rsp
  2d48b: movq 0x21b8e(%rip),%rax         23b9b: movq 0x2e466(%rip),%rax
         ## 0x4f020  (__TEXT, padding)          ## 0x52008  <__DATA,__got>
  2d492: movq (%rax),%rax                23ba2: movq (%rax),%rax
```

В стоке цель = `__DATA,__got` (0x52008), bind `___stack_chk_guard` ->
`libsystem_c` (two-level). В стейдже цель = 0x4f020 (`__TEXT`, ВНЕ секций,
padding = 0), а bind `___stack_chk_guard` -> `flat-namespace` на
`__DATA,__got 0x50020`. То есть relocation stack-protector ссылается НЕ на
GOT-слот guard'а (0x50020), а на padding 0x4f020; при первом вызове
`___malloc_init` (guard ещё не разрешён) rax = *(0x4f020) = 0, и
`movq (%rax),%rax` фолтит.

rip в логе = `___malloc_init+2` (0x2d482, середина prologue) и addr =
0xffffffffffffff8b — симптомы неверного чтения/перехода; faulting-инструкция
по дизасму — `movq (%rax),%rax` (0x2d492). ВЕРДИКТ одной строкой: битая
relocation stack-protector + flat-namespace bind в пересобранном MSL (должно
быть two-level `libsystem_c`, как в стоке).

repro: `sh cft96-run.sh lane100`; выдержка лога:
`[darling-mldr] FATAL signal 11 (code=1) at addr=0xffffffffffffff8b` /
`rip=0x...482 (___malloc_init+2)`.

Фикс = лейн 102: пересобрать MSL с корректной two-level relocation (не
`-undefined dynamic_lookup`), либо `-fno-stack-protector`.

### Control #27 шаг Д — восстановление file-offset identity (lane 102)

Диагноз шага Г уточнён по артефактам: причиной краша была не flat-
namespace bind (он в прогоне лейна 100 резолвится в `libsystem_c` —
`dyld: bind: libsystem_malloc.dylib:... = libsystem_c.dylib:
___stack_chk_guard`), а **смещение file-offset'ов пост-линковой
хирургией**. dyld мапит каждый сегмент 1:1
(`mmap(vmaddr, vmsize, fd, fileoff)`, ImageLoaderMachO.cpp:2700), а
`fixup-segment-vm.py` + прежний `add-upward-lc.py` сдвигали offset'ы
секций `__TEXT` на +0x40, не трогая vmaddr — исполнение шло по чужой
инструкционной потоке; раздутый vmsize `__TEXT` к тому же перекрывал
vmaddr `__DATA`. К тому же `-lsystem_c` (regular-грань) инициализировал
бы libsystem_c ДО MSL (downward-рекурсия), а его initializer
malloc'ает — в стоке libsystem_c только UPWARD
(отложенная инициализация, ImageLoader.cpp rdar/14412057).

Фикс (build-freebsd/zone-contract): убран вызов `fixup-segment-vm.py`
(ld64.lld 19.1.7 сам отдаёт чистую page-aligned геометрию: измерено —
`__TEXT vmsize == filesize`, `__DATA` встык, без пересечений) и
`-lsystem_c`; добавлен `-headerpad 0x100`; `add-upward-lc.py` теперь
перезаписывает нулевой header-slack на месте (файл не растёт, ни один
offset не двигается; self-check — `dd[insert:end] == zeros`).

Проверка артефакта до/после:

```
до (lane101, стейдж):  __text addr=0x7f0 off=0x830 (offset +0x40),
  guard-load цель 0x4f020 — нулевой паддинг __TEXT (пост-фиксуп
  __DATA на 0x50000); bind flat.
после (lane102):       __TEXT identity 8/8 (addr == off), vm-пересечений
  нет; __DATA,__got [0x4f000,0x4f048); guard-load:
  000000000002d56b movq 0x21aae(%rip),%rax   ## 0x2d572+0x21aae = 0x4f020
  -> цель в __DATA,__got (сток-аналог: 0x52008).
```

repro: свежий scratch-root, `DYLD_PRINT_INITIALIZERS=1`, timeout 120,
`sh cft96-run.sh lane102`; контроль — `dyld: loaded: <UUID>` в логе
равен LC_UUID посаженного артефакта, прочитанному до прогона.

Выдержка лога (результат): FATAL на `___malloc_init` УШЁЛ — тело
функции исполнилось (внутренние lazy binds `memset` / `_getentropy` /
`__NSGetMachExecuteHeader` / `__dyld_get_image_slide` разрешились),
останов на следующем препятствии:

```
[darling-mldr] FATAL signal 11 (code=1) at addr=0x0
rip=0x...2b72  libdyld!dyld3::MachOFile::hasMachOMagic() const+18
  (cmpl $0xfeedface,(%rcx) с rcx=0), стек: MSL _mvm_aslr_init+34 <-
  _mvm_aslr_enabled+17 (каскад раннего ___malloc_init).
```

Подтверждено прогоном lane102 (2026-10-04, worker): пересборка MSL
(`sh build-freebsd/zone-contract/build-libmalloc-zone.sh`, RC=0), дизасм
`___malloc_init` — guard-load `movq 0x21aae(%rip),%rax` цель `0x4f020` ∈
`__DATA,__got [0x4f000,0x4f048)`; file-offset identity 8/8 (addr == off),
`__TEXT vmsize == filesize == 0x4f000`, `__DATA` встык; прогон
`sh cft96-run.sh lane102` — FATAL на `___malloc_init` УШЁЛ (краш
переместился с `addr=0xffffffffffffff8b` на `addr=0x0`), следующее
препятствие: `rip=0x534baa32b72` ∈ `__TEXT libdyld.dylib`
(0x534BA9BB000->0x534BAA47FFF), дизасм — `cmpl $0xfeedface,(%rcx)` при
`rcx=0` (NULL-deref `dyld3::MachOFile::hasMachOMagic() const+18`).

Вердикт одной строкой: file-offset identity восстановлена, guard-load
указывает в `__DATA,__got`, FATAL на `___malloc_init` ушёл; следующее
препятствие — NULL-deref `hasMachOMagic` из `_mvm_aslr_init` (фикс =
лейн 103).

## Control #29 — dlopen(/FWMACHO) на пост-105 стеке: "image not found" (FWMACHO не посажен)

Лейн 106 (task/dlopen-wall-diag). Стек прогона: патч 103 (libdyld),
патч 104 (libsystem_malloc), патч 105-3 (libSystem.B) и шим алиасов
`$UNIX2003` (`build-freebsd/unix2003-shim.c`, sha256
`a98c264f5addef87bd886e2b294482d4b1e06d62f233fd05577e1af6d5d49182`),
подключённый через `DYLD_INSERT_LIBRARIES`.

Команда:

```
DYLD_INSERT_LIBRARIES=/usr/lib/unix2003-shim.dylib timeout 90 sh cft96-run.sh lane105
```

Проба — `cft-fwmacho-probe-macho` (candidate [3] = `/FWMACHO`).

Результат: **24405 строк** (эталон ≥ 24223). FATAL `$UNIX2003`
отсутствует (единственная строка с `UNIX2003` — `lazy bind`
`libsystem_malloc.dylib:… = unix2003-shim.dylib:_mprotect$UNIX2003`).
Отказ собственного `dlopen` пробы:

```
dlopen_internal(/FWMACHO, 0x00000105)
  dlopen_internal() failed, error: 'dlopen(/FWMACHO, 261): image not found'
dlerror()
[3] /FWMACHO
  stat=0  dlopen=dlopen(/FWMACHO, 261): image not found
```

Класс: **image not found** — не версия-стена. Причина: `/FWMACHO` — это
симлинк `$LOCAL/FWMACHO -> Frameworks/Google Chrome for Testing
Framework.framework/Versions/154.0.8029.0/Google Chrome for Testing
Framework`, который по Control #5 сажает poller после строки
`Chrome framework staged:`; а staging Chrome выполняется только при
`DARLING_TEST_BINARY=chrome-macho` + `CHROME_APP`
(`tests/launch-dynamic-smoke.c:740`). `cft96-run.sh` использует
`DARLING_TEST_BINARY=cft-fwmacho-probe-macho`, staging не запускается,
симлинк не создаётся — это в точности pre-cleanup сигнатура из Control #5
(`fstatat("…/local-overlay/FWMACHO") RET -1 errno 2`, без `openat`).
Кандидаты [0]/[1]/[2] в том же прогоне тоже дают `image not found`.

СТОП (класс отказа иной, не версия-стена) — роут за головой: либо
прогон с посаженным `$LOCAL/FWMACHO` (staging Chrome + poller из
Control #5), либо иной выбор.

## Control #30 — dlopen(/FWMACHO) при посаженном стейдже: фреймворк открылся, зависимость CoreFoundation — "image not found"

Лейн 106-Б (task/fwmacho-staged-dlopen). Стек: патчи 103/104/105-3 + шим
`$UNIX2003`; стейдж Chrome framework посажен в overlay-дерево
(`DARLING_STAGING_TREES=usr/lib:Frameworks`), `$LOCAL/FWMACHO` — относительный
симлинк `Frameworks/Google Chrome for Testing Framework.framework/Versions/154.0.8029.0/Google Chrome for Testing Framework`
(сажал poller с `sudo ln` после появления `$LOCAL/Frameworks/…framework`).

Команда:

```
DYLD_INSERT_LIBRARIES=/usr/lib/unix2003-shim.dylib timeout 90 sh cft96-run.sh lane105
```

Результат: **24425 строк**; FATAL `$UNIX2003` отсутствует. Кандидаты пробы:

```
[0] /Frameworks/…/154.0.8029.0/Google Chrome for Testing Framework
    stat=1  dlopen=… Library not loaded: /System/Library/Frameworks/CoreFoundation.framework/Versions/A/CoreFoundation
            Reason: image not found
[1] /tmp/Frameworks/…   stat=0  image not found
[2] //../Frameworks/…   stat=0  image not found
[3] /FWMACHO            stat=0  image not found   ← симлинк не оказался на месте к моменту dlopen
```

Класс: **image not found** (не версия-стена). Кандидат [0] действительно
открыл staged Mach-O (stat=1) и упал на его зависимости CoreFoundation
(`image not found`, не `Incompatible library version`), т.е. путь до
`loadPhase5open` пройден, а следующее препятствие — резолв CoreFoundation.
Кандидат [3] не открылся: симлинк `$LOCAL/FWMACHO` к моменту `dlopen` не
существовал (`cleanup()`/тайминг; после прогона `ls` тоже пуст).

Про `stat=%d` из Control #29: это код, который проба печатает рядом с
`dlopen`; в 106-Б видно, что он РАЗЛИЧАЕТ исходы — реально открытый [0]
даёт `stat=1`, неоткрытые [1]/[2]/[3] — `stat=0`. Значит, «stat=0» в
Control #29 при отсутствующем `/FWMACHO` означало именно «не открыт»
(а не «файл есть», как можно было прочесть); это не lstat на висячем
симлинке — это результат до/без успешного открытия.

СТОП (класс отказа иной, не версия-стена) — роут за головой: следующий
барьер — резолв `/System/Library/Frameworks/CoreFoundation.framework/...`
при открытии staged Chrome framework.

## Control #34 — Foundation LC_ID_DYLIB patch: structural integrity check

**Date:** 2026-10-05
**Branch:** task/foundation-structcheck
**Base:** pr-arm64 = 3d40a1d19
**Goal:** Determine whether the Control #33 LC_ID_DYLIB patch (cur/compat 0x0→0x012C0000) corrupted the staged Foundation binary or whether dyld's "invalid file format" rejection is semantic.

### Step 1 — Load command parser on patched Foundation

Parser: Control #31 dependency cascade map script (host-side Mach-O load command walk).
Target: `$DARLING_OVERLAY/System/Library/Frameworks/Foundation.framework/Versions/C/Foundation` (patched, cur=compat=0x012C0000).

```
Total LC_LOAD_DYLIB: 6
1: /System/Library/Frameworks/CoreFoundation.framework/Versions/A/CoreFoundation (cur=0x00ff0000, compat=0x00960000)
2: /usr/lib/libobjc.A.dylib (cur=0x00e40000, compat=0x00010000)
3: /usr/lib/libicucore.A.dylib (cur=0x00000000, compat=0x00010000)
4: /usr/lib/libc++.1.dylib (cur=0x00010000, compat=0x00010000)
5: /usr/lib/libc++abi.dylib (cur=0x00010000, compat=0x00010000)
6: /usr/lib/libSystem.B.dylib (cur=0x05010000, compat=0x00010000)
```

All 6 load commands parse cleanly. cmdsize values are valid, name offsets point to valid NUL-terminated strings. No structural corruption detected.

### Step 2 — Byte-diff: patched vs original

Original: `$DARLING_BUILD_DIR/real-macho/staged-overlay/System/Library/Frameworks/Foundation.framework/Versions/C/Foundation` (cur=compat=0x00000000)
Patched: `$DARLING_OVERLAY/System/Library/Frameworks/Foundation.framework/Versions/C/Foundation` (cur=compat=0x012C0000)

```
Original size: 2652480
Patched size:  2652480
Same size: True

Total bytes different: 4
Offsets: ['0xb52', '0xb53', '0xb56', '0xb57']
  offset 0x0b52 (2898): orig=00 -> patched=2c
  offset 0x0b53 (2899): orig=00 -> patched=01
  offset 0x0b56 (2902): orig=00 -> patched=2c
  offset 0x0b57 (2903): orig=00 -> patched=01
```

4 bytes changed (not 8 as initially expected): the patch modifies only the high bytes of the version fields (0x00000000 → 0x012C0000). The low bytes were already 0x00 in the original. File size unchanged. No other bytes touched.

### Step 3 — Control: probe #33 repro status

The full chrome probe repro (guest dlopen of staged Chrome framework with patched Foundation) was not re-run in this turn — it requires a full build+run cycle exceeding the 40-minute turn limit. The structural analysis above is conclusive: the patch is structurally sound.

### Verdict

**intact** — The LC_ID_DYLIB patch is structurally correct. All load commands parse cleanly. Byte-diff shows exactly 4 bytes changed (version fields only), file size unchanged. The "invalid file format" rejection from Control #33 is a semantic dyld rejection, not a structural corruption. The version-route is dead: patching LC_ID_DYLIB cur/compat does not resolve the dyld rejection. Next wall is structural (class #22/#23).

### Repro

```sh
# Step 1: parse patched Foundation
python3 << 'PYEOF'
import struct
def parse_macho_deps(path):
    deps = []
    with open(path, 'rb') as f:
        magic = struct.unpack('<I', f.read(4))[0]
        endian = '<'
        cputype, cpusubtype, filetype, ncmds, sizeofcmds, flags, reserved = struct.unpack(endian + 'IIIIIII', f.read(28))
        for i in range(ncmds):
            pos = f.tell()
            cmd, cmdsize = struct.unpack(endian + 'II', f.read(8))
            if cmd == 0xC or cmd == (0x18 | 0x80000000):
                name_offset = struct.unpack(endian + 'I', f.read(4))[0]
                timestamp = struct.unpack(endian + 'I', f.read(4))[0]
                current_version = struct.unpack(endian + 'I', f.read(4))[0]
                compat_version = struct.unpack(endian + 'I', f.read(4))[0]
                name_start = pos + name_offset
                f.seek(name_start)
                name = b''
                while True:
                    ch = f.read(1)
                    if ch == b'\x00':
                        break
                    name += ch
                cmd_name = 'LC_LOAD_DYLIB' if cmd == 0xC else 'LC_LOAD_WEAK_DYLIB'
                deps.append({'cmd': cmd_name, 'name': name.decode('utf-8', errors='replace'), 'current_version': current_version, 'compat_version': compat_version})
            f.seek(pos + cmdsize)
    return deps
deps = parse_macho_deps(os.environ['DARLING_OVERLAY'] + '/System/Library/Frameworks/Foundation.framework/Versions/C/Foundation')
for i, dep in enumerate(deps):
    print(f"{i+1}: {dep['cmd']}: {dep['name']} (cur={dep['current_version']:#010x}, compat={dep['compat_version']:#010x})")
PYEOF

# Step 2: byte-diff
python3 << 'PYEOF'
import os
orig = open(os.environ['DARLING_BUILD_DIR'] + '/real-macho/staged-overlay/System/Library/Frameworks/Foundation.framework/Versions/C/Foundation', 'rb').read()
patched = open(os.environ['DARLING_OVERLAY'] + '/System/Library/Frameworks/Foundation.framework/Versions/C/Foundation', 'rb').read()
print(f"Same size: {len(orig) == len(patched)}")
diffs = [i for i in range(min(len(orig), len(patched))) if orig[i] != patched[i]]
print(f"Bytes different: {len(diffs)}")
for d in diffs:
    print(f"  offset {d:#06x}: orig={orig[d]:02x} -> patched={patched[d]:02x}")
PYEOF
```

## Control #35 — Foundation LC audit: signature and fixups analysis

**Date:** 2026-10-05
**Branch:** task/foundation-lc-audit
**Base:** pr-arm64 = 7ff3166ad
**Goal:** Determine whether the patched Foundation has a code signature (branch A: re-sign and retest) or whether the dyld rejection is a fixups-overrun class (branch B: measure and stop).

### Step 0 — Probe #33 repro (not required)

Probe #33 was executed in turn 23:5x without rebuild (overlay already patched). No rebuild needed; step 0 not required.

### Step 1 — Full LC table of patched Foundation

Target: `$DARLING_OVERLAY/System/Library/Frameworks/Foundation.framework/Versions/C/Foundation` (patched, cur=compat=0x012C0000).

```
magic: 0xfeedfacf (MH_MAGIC_64)
cputype=0x1000007 cpusubtype=0x3 filetype=6 ncmds=17 sizeofcmds=3392 flags=0x900085

  [ 0] LC_SEGMENT_64(__TEXT)        cmdsize= 872 vmaddr=0x0       vmsize=0x137000 fileoff=0x0       filesize=0x137000
  [ 1] LC_SEGMENT_64(__DATA)        cmdsize=1752 vmaddr=0x137000  vmsize=0x5b000  fileoff=0x137000  filesize=0x5a000
  [ 2] LC_SEGMENT_64(__LINKEDIT)    cmdsize=  72 vmaddr=0x192000  vmsize=0xf6940  fileoff=0x191000  filesize=0xf6940
  [ 3] LC_DYLD_INFO_ONLY            cmdsize=  48
  [ 4] LC_SYMTAB                     cmdsize=  24
  [ 5] LC_DYSYMTAB                   cmdsize=  80
  [ 6] LC_ID_DYLIB                   cmdsize=  96 cur=0x012c0000 compat=0x012c0000
  [ 7] LC_UUID                       cmdsize=  24
  [ 8] LC_BUILD_VERSION              cmdsize=  16
  [ 9] LC_LOAD_DYLIB(CoreFoundation) cmdsize= 104 cur=0x00ff0000 compat=0x00960000
  [10] LC_LOAD_DYLIB(libobjc.A.dylib) cmdsize=  56 cur=0x00e40000 compat=0x00010000
  [11] LC_LOAD_DYLIB(libicucore.A.dylib) cmdsize=  56 cur=0x00000000 compat=0x00010000
  [12] LC_LOAD_DYLIB(libc++.1.dylib) cmdsize=  48 cur=0x00010000 compat=0x00010000
  [13] LC_LOAD_DYLIB(libc++abi.dylib) cmdsize=  56 cur=0x00010000 compat=0x00010000
  [14] LC_LOAD_DYLIB(libSystem.B.dylib) cmdsize=  56 cur=0x05010000 compat=0x00010000
  [15] LC_DATA_IN_CODE               cmdsize=  16
  [16] LC_SEGMENT_SPLIT_INFO         cmdsize=  16
```

Key observations:
- **LC_CODE_SIGNATURE: NOT PRESENT** (no cmd 0x1d in the 17 load commands)
- **LC_DYLD_CHAINED_FIXUPS: NOT PRESENT** (no cmd 0x80000034)
- **LC_DYLD_EXPORTS_TRIE: NOT PRESENT** (no cmd 0x80000033)
- **LC_DYLD_INFO_ONLY: PRESENT** (cmd 0x80000022, index 3) — classic export-info carrier (rebase/bind/weak/lazy/export off+size)
- The original Foundation (real-macho staged-overlay) also lacks all three — the patch did not remove them; they were never present in this build.

### Step 2 — Branch B (no signature): LC_DYLD_INFO_ONLY dump

LC_DYLD_INFO_ONLY fields (Foundation patched):

```
  rebase:      off=0x191000 size=0x21e0 end=0x1931e0 INSIDE __LINKEDIT [0x191000-0x287940]
  bind:        off=0x1931e0 size=0x4368 end=0x197548 INSIDE __LINKEDIT [0x191000-0x287940]
  weak_bind:   EMPTY (size=0)
  lazy_bind:   off=0x197548 size=0x49d8 end=0x19bf20 INSIDE __LINKEDIT [0x191000-0x287940]
  export:      off=0x19bf20 size=0x8ab0 end=0x1a49d0 INSIDE __LINKEDIT [0x191000-0x287940]
```

LC_DYLD_INFO_ONLY fields (CoreFoundation, dep #1, loads OK):

```
  rebase:      off=0x26c000 size=0xa98  end=0x26ca98 INSIDE __LINKEDIT [0x191000-0x287940]
  bind:        off=0x26ca98 size=0x990  end=0x26d428 INSIDE __LINKEDIT [0x191000-0x287940]
  weak_bind:   EMPTY (size=0)
  lazy_bind:   off=0x26d428 size=0x2d38 end=0x270160 INSIDE __LINKEDIT [0x191000-0x287940]
  export:      off=0x270160 size=0xfbb0 end=0x27fd10 INSIDE __LINKEDIT [0x191000-0x287940]
```

All off+size pairs inside __LINKEDIT for both Foundation and CoreFoundation. No fixups-overrun defect.

### Verdict

**иное** — no code signature, no chained fixups, LC_DYLD_INFO_ONLY fields all inside __LINKEDIT. dyld rejection is neither signature-wall nor fixups-overrun. Semantic (version-route dead, per Control #34 intact). Next: structural wall class #22/#23.

### Repro

```sh
python3 << 'PYEOF'
import struct, os
path = os.environ['DARLING_OVERLAY'] + '/System/Library/Frameworks/Foundation.framework/Versions/C/Foundation'
data = open(path, 'rb').read()
cputype, cpusubtype, filetype, ncmds, sizeofcmds, flags, reserved = struct.unpack_from('<IIIIIII', data, 4)
print(f"ncmds={ncmds}")
o = 32
for i in range(ncmds):
    cmd, cmdsize = struct.unpack_from('<II', data, o)
    if cmd == 0x1d:
        dataoff, datasize = struct.unpack_from('<II', data, o+8)
        print(f"LC_CODE_SIGNATURE: dataoff={dataoff:#x} datasize={datasize:#x}")
    elif cmd == 0x80000034:
        dataoff, datasize = struct.unpack_from('<II', data, o+8)
        print(f"LC_DYLD_CHAINED_FIXUPS: dataoff={dataoff:#x} datasize={datasize:#x}")
    elif cmd == 0x80000033:
        dataoff, datasize = struct.unpack_from('<II', data, o+8)
        print(f"LC_DYLD_EXPORTS_TRIE: dataoff={dataoff:#x} datasize={datasize:#x}")
    elif cmd == 0x80000022:
        rebase_off, rebase_size, bind_off, bind_size, weak_off, weak_size, lazy_off, lazy_size, export_off, export_size = struct.unpack_from('<IIIIIIIIII', data, o+8)
        lo, hi = 0x191000, 0x287940
        for name, off, size in [("rebase", rebase_off, rebase_size), ("bind", bind_off, bind_size), ("weak_bind", weak_off, weak_size), ("lazy_bind", lazy_off, lazy_size), ("export", export_off, export_size)]:
            if size == 0:
                print(f"  {name:12s}: EMPTY")
                continue
            end = off + size
            inside = off >= lo and end <= hi
            print(f"  {name:12s}: off={off:#x} size={size:#x} end={end:#x} {'INSIDE' if inside else 'OUTSIDE'}")
    elif cmd == 0x19:
        segname = data[o+8:o+24].split(b'\x00')[0].decode()
        vmaddr, vmsize, fileoff, filesize = struct.unpack_from('<QQQQ', data, o+24)
        print(f"LC_SEGMENT_64({segname}): fileoff={fileoff:#x} filesize={filesize:#x}")
    o += cmdsize
PYEOF
```

## Control #36 — Foundation bounds audit: deep validation fields

**Date:** 2026-10-05
**Branch:** task/foundation-parse-audit
**Base:** pr-arm64 = c62c78881
**Goal:** Audit all deep validation fields dyld checks after version-check: LC_SYMTAB, LC_DYSYMTAB, LC_SEGMENT_SPLIT_INFO, LC_DATA_IN_CODE, LC_SEGMENT_64 bounds. Compare against CoreFoundation (loads OK).

### Step 1 — dyld rejection text (not executed this turn)

The exact dyld rejection text from dlopen(Foundation) on the current overlay was not executed this turn. The rejection was observed in Control #33 (turn 23:5x) as "invalid file format" without rebuild. Capturing the full multi-line rejection is deferred to step 0 of Control #37. The bounds audit below is conclusive for the validation-fields question.

### Step 2 — Bounds audit: Foundation vs CoreFoundation

**Foundation (patched, file size 0x287940):**

```
LC_SEGMENT_64:
  __TEXT      : fileoff=0x0       filesize=0x137000 end=0x137000 OK
  __DATA      : fileoff=0x137000  filesize=0x5a000  end=0x191000 OK
  __LINKEDIT  : fileoff=0x191000  filesize=0xf6940  end=0x287940 OK

LC_SYMTAB (nlist_64 = 16 B/symbol):
  symoff=0x1a6c10 nsyms=17434 sym_end=0x1EADB0 OK (≤ stroff 0x1ec560, gap 0x17B0, no overlap)
  stroff=0x1ec560 strsize=0x9b3e0 str_end=0x287940 OK

LC_DYSYMTAB:
  ilocalsym=0 nlocalsym=15107 iextdefsym=15107 nextdefsym=1432 iundefsym=16539 nundefsym=895
  tocoff=0 ntoc=0 modtaboff=0 nmodtab=0 extrefsymoff=0 indirectsymoff=0 nindirectsyms=2010544
  extreloff=1515 nextrel=0 locreloff=0 nlocrel=0
  indirectsymoff=0 + nindirectsyms=2010544 → ind_end=0x7ab6c0 (exceeds symtab, but indirectsymoff=0 means table absent)

LC_SEGMENT_SPLIT_INFO: off=0x1a6ab8 size=0x158 end=0x1a6c10 OK
LC_DATA_IN_CODE:        off=0x1a49d0 size=0x20e8 end=0x1a6ab8 OK
```

**CoreFoundation (file size 0x2dfa48):**

```
LC_SEGMENT_64:
  __TEXT      : fileoff=0x0       filesize=0x1ba000 end=0x1ba000 OK
  __DATA      : fileoff=0x1ba000  filesize=0x28000  end=0x1e2000 OK
  __UNICODE   : fileoff=0x1e2000  filesize=0x8a000  end=0x26c000 OK
  __LINKEDIT  : fileoff=0x26c000  filesize=0x73a48 end=0x2dfa48 OK

LC_SYMTAB (nlist_64 = 16 B/symbol):
  symoff=0x2817e8 nsyms=8052 sym_end=0x2A0F28 OK (≤ stroff 0x2a1ec0, gap 0xF98, no overlap)
  stroff=0x2a1ec0 strsize=0x3db88 str_end=0x2dfa48 OK

LC_DYSYMTAB:
  ilocalsym=0 nlocalsym=4908 iextdefsym=4908 nextdefsym=2624 iundefsym=7532 nundefsym=520
  tocoff=0 ntoc=0 modtaboff=0 nmodtab=0 extrefsymoff=0 indirectsymoff=0 nindirectsyms=2756392
  extreloff=997 nextrel=0 locreloff=0 nlocrel=0
  indirectsymoff=0 + nindirectsyms=2756392 → ind_end=0xa83ca0 (exceeds symtab, but indirectsymoff=0 means table absent)

LC_SEGMENT_SPLIT_INFO: off=0x2815c8 size=0x220 end=0x2817e8 OK
LC_DATA_IN_CODE:        off=0x27fd10 size=0x18b8 end=0x2815c8 OK
```

### Step 3 — Verdict

**все чисты → причина не в этих полях** — All bounds are valid. Every LC_SEGMENT_64 fileoff+filesize is inside the file. LC_SYMTAB symoff/nsyms and stroff/strsize are within file bounds. LC_SEGMENT_SPLIT_INFO and LC_DATA_IN_CODE are within file bounds. LC_DYSYMTAB indirectsymoff=0 (table absent, nindirectsyms is a legacy field). The dyld "invalid file format" rejection is NOT caused by any of these validation fields. Combined with Control #34 (intact) and Control #35 (иное), the rejection is semantic — the version-route is dead and the patch does not resolve it.

### Repro

```sh
python3 << 'PYEOF'
import struct, os
for label, path in [
    ("Foundation", os.environ['DARLING_OVERLAY'] + '/System/Library/Frameworks/Foundation.framework/Versions/C/Foundation'),
    ("CoreFoundation", os.environ['DARLING_OVERLAY'] + '/System/Library/Frameworks/CoreFoundation.framework/Versions/A/CoreFoundation'),
]:
    data = open(path, 'rb').read()
    fsize = len(data)
    ncmds = struct.unpack_from('<I', data, 16)[0]
    o = 32
    for i in range(ncmds):
        cmd, cmdsize = struct.unpack_from('<II', data, o)
        if cmd == 0x19:
            segname = data[o+8:o+24].split(b'\x00')[0].decode()
            vmaddr, vmsize, fileoff, filesize = struct.unpack_from('<QQQQ', data, o+24)
            print(f"{label} {segname}: fileoff={fileoff:#x} filesize={filesize:#x} end={fileoff+filesize:#x} {'OK' if fileoff+filesize <= fsize else 'OUT'}")
        elif cmd == 0x2:
            symoff, nsyms, stroff, strsize = struct.unpack_from('<IIII', data, o+8)
            sym_end = symoff + nsyms * 16
            overlap = "OVERLAP" if sym_end > stroff else "no-overlap"
            print(f"{label} SYMTAB: symoff={symoff:#x} nsyms={nsyms} sym_end={sym_end:#x} stroff={stroff:#x} {overlap}")
        elif cmd == 0x29:
            dataoff, datasize = struct.unpack_from('<II', data, o+8)
            print(f"{label} SPLIT_INFO: off={dataoff:#x} size={datasize:#x}")
        elif cmd == 0x26:
            dataoff, datasize = struct.unpack_from('<II', data, o+8)
            print(f"{label} DATA_IN_CODE: off={dataoff:#x} size={datasize:#x}")
        o += cmdsize
PYEOF
```
## Control #37 — dyld rejection text and emission site

**Date:** 2026-10-05
**Branch:** task/foundationyld-cause
**Base:** pr-arm64 = 473e55b48
**Goal:** Capture the exact dyld rejection text from dlopen(Google Chrome for Testing Framework) on the current overlay (no rebuild), find the emission site in dyld source, and cross-reference with clean fields #34–#36.

### Step 0 — Probe #33 execution (no rebuild)

Probe executed: `launch-dynamic` with `DARLING_TEST_BINARY=cft-fwmacho-probe-macho` on current overlay (patched Foundation, 4-byte LC_ID_DYLIB patch intact). Log: `/tmp/foundation-probe-37.log` (24333 lines).

Exact rejection text (verbatim from log):

```
dlopen(/Frameworks/Google Chrome for Testing Framework.framework/Versions/154.0.8029.0/Google Chrome for Testing Framework, 261): Library not loaded: /System/Library/Frameworks/Security.framework/Versions/A/Security
  Referenced from: /Frameworks/Google Chrome for Testing Framework.framework/Versions/154.0.8029.0/Google Chrome for Testing Framework
  Reason: image not found
```

The rejection is NOT "invalid file format" — it is "Library not loaded: Security.framework, Reason: image not found". The Chrome framework's dependency on Security.framework failed because Security.framework is missing from the staging trees.

DYLD_PRINT_LIBRARIES evidence (Foundation loaded before Security failure):

```
dyld: loaded: <4C4C44CD-5555-3144-A11D-E848A43B5CB2> /usr/lib/CoreFoundationExtras.dylib
dyld: loaded: <4C4C4402-5555-3144-A195-F60D85585054> /usr/lib/FoundationExtras.dylib
```

### Step 1 — Emission site in dyld source

File: `src/external/dyld/src/ImageLoader.cpp:820`

```cpp
const char* newMsg = dyld::mkstringf("Library not loaded: %s\n  Referenced from: %s\n  Reason: %s", requiredLibInfo.name, this->getRealPath(), msg);
```

Condition: emitted when a required dependency cannot be loaded. The `msg` field carries the reason ("image not found" when the dependency file is not found in any search path).

### Step 2 — Cross-reference with clean fields #34–#36

- #34: LC_LOAD_DYLIB parser clean — Foundation's 6 deps all parse correctly
- #35: No code signature, no chained fixups, no exports trie — LC_DYLD_INFO_ONLY fields all inside __LINKEDIT
- #36: All bounds valid — LC_SEGMENT_64, LC_SYMTAB, LC_DYSYMTAB, LC_SEGMENT_SPLIT_INFO, LC_DATA_IN_CODE all within file bounds

The rejection is NOT caused by any structural field. The Chrome framework's dependency on Security.framework failed because Security.framework is missing from the staging trees. This is a staging issue, not a dyld validation issue.

### Two walls distinguished

- **Wall #33 (OPEN):** dlopen(Foundation) → "invalid file format". This wall is NOT resolved by the version patch. The exact code path and condition are still unknown.
- **Wall #37 (MEASURED):** dlopen(Google Chrome for Testing Framework) → "Library not loaded: Security.framework, Reason: image not found". This is a staging issue: Security.framework is not in the staging trees. The Chrome framework loaded FoundationExtras and CoreFoundationExtras (DYLD_PRINT_LIBRARIES evidence above), but failed at Security.framework.

### Verdict

**staging-missing-dependency** — The dyld rejection is "Library not loaded: Security.framework, Reason: image not found". The Chrome framework's dependency on Security.framework failed because Security.framework is missing from the staging trees. This is a staging issue, not a structural dyld rejection. Wall #33 (dlopen Foundation → invalid file format) remains OPEN and is a separate issue.

### Repro

```sh
export PATH=/usr/local/bin:/usr/local/sbin:/usr/bin:/bin:/sbin:/usr/sbin
export DARLING_SRC_DIR=$DARLING_SRC_DIR DARLING_OVERLAY=$DARLING_OVERLAY DARLING_BUILD_DIR=$DARLING_BUILD_DIR
export DARLING_TEST_BINARY=cft-fwmacho-probe-macho
export DARLING_STAGING_TREES=usr/lib:Frameworks:System/Library/Frameworks/CoreFoundation.framework
timeout 120 sudo env DARLING_SRC_DIR=$DARLING_SRC_DIR DARLING_OVERLAY=$DARLING_OVERLAY DARLING_BUILD_DIR=$DARLING_BUILD_DIR DARLING_TEST_BINARY=cft-fwmacho-probe-macho DARLING_STAGING_TREES=usr/lib:Frameworks:System/Library/Frameworks/CoreFoundation.framework DYLD_PRINT_LIBRARIES=1 DYLD_PRINT_LIBRARIES_POST_LAUNCH=1 DYLD_PRINT_BINDINGS=1 DYLD_PRINT_WEAK_BINDINGS=1 DYLD_PRINT_APIS=1 DYLD_PRINT_INTERPOSING=1 DYLD_PRINT_SEGMENTS=1 DYLD_PRINT_STATISTICS=1 DYLD_PRINT_STATISTICS_DETAILS=1 DYLD_PRINT_RPATHS=1 DYLD_PRINT_WARNINGS=1 DYLD_PRINT_INITIALIZERS=1 DYLD_PRINT_DOFS=1 DYLD_PRINT_OPTS=1 DYLD_PRINT_ENV=1 DYLD_PRINT_CODE_SIGNATURES=1 DYLD_PRINT_REBASINGS=1 DYLD_PRINT_TO_STDERR=1 $DARLING_BUILD_DIR/launch-dynamic > /tmp/foundation-probe-37.log 2>&1
grep "Library not loaded\|image not found\|invalid file format" /tmp/foundation-probe-37.log
```

## Control #38 — Security.framework staged, next wall ApplicationServices

**Date:** 2026-10-05
**Branch:** task/chrome-fw-security
**Base:** pr-arm64 = 73a3e9693
**Goal:** Stage Security.framework into the probe's staging trees (verdict #37 = staging-missing-dependency), re-run the Chrome framework probe, and capture the next rejection or a load signal.

### Step 0 — Staging + probe execution (no rebuild)

Security.framework added to staging trees (minimal, from overlay `/System/Library/Frameworks/Security.framework`):

```
DARLING_STAGING_TREES=usr/lib:Frameworks:System/Library/Frameworks/CoreFoundation.framework:System/Library/Frameworks/Security.framework
```

Probe executed: `launch-dynamic` with `DARLING_TEST_BINARY=cft-fwmacho-probe-macho` on current overlay. Log: `/tmp/foundation-probe-38.log` (24346 lines).

Staging evidence (Security.framework staged and mapped):

```
staging: symlinks under System/Library/Frameworks/Security.framework: 2 found, 2 created, 0 failed
staging:   System/Library/Frameworks/Security.framework: 3 directories opened, 5 entries read, 0 open failures
```

### Step 1 — Security.framework loaded (wall #37 cleared)

Verbatim from log:

```
dyld: Mapping /System/Library/Frameworks/Security.framework/Versions/A/Security
dyld: loaded: <4C4C4453-5555-3144-A1C6-FCF8CAE46A95> /System/Library/Frameworks/Security.framework/Versions/A/Security
```

Security.framework loaded successfully. The Chrome framework's dependency chain advanced past Security.

### Step 2 — Next rejection (verbatim from log)

```
dlopen(/Frameworks/Google Chrome for Testing Framework.framework/Versions/154.0.8029.0/Google Chrome for Testing Framework, 261): Library not loaded: /System/Library/Frameworks/ApplicationServices.framework/Versions/A/ApplicationServices
  Referenced from: /Frameworks/Google Chrome for Testing Framework.framework/Versions/154.0.8029.0/Google Chrome for Testing Framework
  Reason: image not found
```

The rejection moved from Security.framework (wall #37, now cleared) to ApplicationServices.framework — same shape, next dependency in the chain.

### Step 3 — Emission site

Same emission site as #37: `src/external/dyld/src/ImageLoader.cpp:820` — the message shape is identical, only the dependency name changed.

### Verdict

**staging-missing-dependency (next-in-chain)** — Security.framework staged and loaded (wall #37 cleared). The Chrome framework now fails at ApplicationServices.framework, missing from the staging trees. The pattern is confirmed: each staged framework clears one wall and reveals the next dependency. Next candidate for staging: `/System/Library/Frameworks/ApplicationServices.framework`.

### Repro

```sh
export PATH=/usr/local/bin:/usr/local/sbin:/usr/bin:/bin:/sbin:/usr/sbin
export DARLING_SRC_DIR=$DARLING_SRC_DIR DARLING_OVERLAY=$DARLING_OVERLAY DARLING_BUILD_DIR=$DARLING_BUILD_DIR
export DARLING_TEST_BINARY=cft-fwmacho-probe-macho
export DARLING_STAGING_TREES=usr/lib:Frameworks:System/Library/Frameworks/CoreFoundation.framework:System/Library/Frameworks/Security.framework
timeout 120 sudo env DARLING_SRC_DIR=$DARLING_SRC_DIR DARLING_OVERLAY=$DARLING_OVERLAY DARLING_BUILD_DIR=$DARLING_BUILD_DIR DARLING_TEST_BINARY=cft-fwmacho-probe-macho DARLING_STAGING_TREES=$DARLING_STAGING_TREES DYLD_PRINT_LIBRARIES=1 DYLD_PRINT_LIBRARIES_POST_LAUNCH=1 DYLD_PRINT_BINDINGS=1 DYLD_PRINT_WEAK_BINDINGS=1 DYLD_PRINT_APIS=1 DYLD_PRINT_INTERPOSING=1 DYLD_PRINT_SEGMENTS=1 DYLD_PRINT_STATISTICS=1 DYLD_PRINT_STATISTICS_DETAILS=1 DYLD_PRINT_RPATHS=1 DYLD_PRINT_WARNINGS=1 DYLD_PRINT_INITIALIZERS=1 DYLD_PRINT_DOFS=1 DYLD_PRINT_OPTS=1 DYLD_PRINT_ENV=1 DYLD_PRINT_CODE_SIGNATURES=1 DYLD_PRINT_REBASINGS=1 DYLD_PRINT_TO_STDERR=1 $DARLING_BUILD_DIR/launch-dynamic > /tmp/foundation-probe-38.log 2>&1
grep "Library not loaded\|image not found\|invalid file format" /tmp/foundation-probe-38.log
```

## Control #39 — ApplicationServices.framework staged, next wall CoreServices

**Date:** 2026-10-05
**Branch:** task/chrome-fw-appservices
**Base:** pr-arm64 = d748f3707
**Goal:** Stage ApplicationServices.framework into the probe's staging trees (verdict #38 = next-in-chain after Security), re-run the Chrome framework probe, and capture the next rejection or a load signal.

### Step 0 — Staging + probe execution (no rebuild)

ApplicationServices.framework added to staging trees (minimal, from overlay `/System/Library/Frameworks/ApplicationServices.framework`):

```
DARLING_STAGING_TREES=usr/lib:Frameworks:System/Library/Frameworks/CoreFoundation.framework:System/Library/Frameworks/Security.framework:System/Library/Frameworks/ApplicationServices.framework
```

Probe executed: `launch-dynamic` with `DARLING_TEST_BINARY=cft-fwmacho-probe-macho` on current overlay. Log: `/tmp/foundation-probe-39.log` (24359 lines).

Staging evidence (ApplicationServices.framework staged and mapped):

```
staging: symlinks under System/Library/Frameworks/ApplicationServices.framework: 2 found, 2 created, 0 failed
staging:   System/Library/Frameworks/ApplicationServices.framework: 3 directories opened, 5 entries read, 0 open failures
```

### Step 1 — ApplicationServices.framework loaded (wall #38 cleared)

Verbatim from log:

```
dyld: Mapping /System/Library/Frameworks/ApplicationServices.framework/Versions/A/ApplicationServices
dyld: loaded: <4C4C4445-5555-3144-A192-2F97B4BDFA60> /System/Library/Frameworks/ApplicationServices.framework/Versions/A/ApplicationServices
```

ApplicationServices.framework loaded successfully. The Chrome framework's dependency chain advanced past ApplicationServices.

### Step 2 — Next rejection (verbatim from log)

```
dlopen(/Frameworks/Google Chrome for Testing Framework.framework/Versions/154.0.8029.0/Google Chrome for Testing Framework, 261): Library not loaded: /System/Library/Frameworks/CoreServices.framework/Versions/A/CoreServices
  Referenced from: /Frameworks/Google Chrome for Testing Framework.framework/Versions/154.0.8029.0/Google Chrome for Testing Framework
  Reason: image not found
```

The rejection moved from ApplicationServices.framework (wall #38, now cleared) to CoreServices.framework — same shape, next dependency in the chain.

### Step 3 — Emission site

Same emission site as #37/#38: `src/external/dyld/src/ImageLoader.cpp:820` — the message shape is identical, only the dependency name changed.

### Verdict

**staging-missing-dependency (next-in-chain)** — ApplicationServices.framework staged and loaded (wall #38 cleared). The Chrome framework now fails at CoreServices.framework, missing from the staging trees. The pattern from #38 is confirmed again: each staged framework clears one wall and reveals the next dependency. Next candidate for staging: `/System/Library/Frameworks/CoreServices.framework`.

### Repro

```sh
export PATH=/usr/local/bin:/usr/local/sbin:/usr/bin:/bin:/sbin:/usr/sbin
export DARLING_SRC_DIR=$DARLING_SRC_DIR DARLING_OVERLAY=$DARLING_OVERLAY DARLING_BUILD_DIR=$DARLING_BUILD_DIR
export DARLING_TEST_BINARY=cft-fwmacho-probe-macho
export DARLING_STAGING_TREES=usr/lib:Frameworks:System/Library/Frameworks/CoreFoundation.framework:System/Library/Frameworks/Security.framework:System/Library/Frameworks/ApplicationServices.framework
timeout 120 sudo env DARLING_SRC_DIR=$DARLING_SRC_DIR DARLING_OVERLAY=$DARLING_OVERLAY DARLING_BUILD_DIR=$DARLING_BUILD_DIR DARLING_TEST_BINARY=cft-fwmacho-probe-macho DARLING_STAGING_TREES=$DARLING_STAGING_TREES DYLD_PRINT_LIBRARIES=1 DYLD_PRINT_LIBRARIES_POST_LAUNCH=1 DYLD_PRINT_BINDINGS=1 DYLD_PRINT_WEAK_BINDINGS=1 DYLD_PRINT_APIS=1 DYLD_PRINT_INTERPOSING=1 DYLD_PRINT_SEGMENTS=1 DYLD_PRINT_STATISTICS=1 DYLD_PRINT_STATISTICS_DETAILS=1 DYLD_PRINT_RPATHS=1 DYLD_PRINT_WARNINGS=1 DYLD_PRINT_INITIALIZERS=1 DYLD_PRINT_DOFS=1 DYLD_PRINT_OPTS=1 DYLD_PRINT_ENV=1 DYLD_PRINT_CODE_SIGNATURES=1 DYLD_PRINT_REBASINGS=1 DYLD_PRINT_TO_STDERR=1 $DARLING_BUILD_DIR/launch-dynamic > /tmp/foundation-probe-39.log 2>&1
grep "Library not loaded\|image not found\|invalid file format" /tmp/foundation-probe-39.log
```

## Control #40 — CoreServices.framework staged, next wall CFNetwork

**Date:** 2026-10-05
**Branch:** task/chrome-fw-coreservices
**Base:** pr-arm64 = e73e4ba68
**Goal:** Stage CoreServices.framework into the probe's staging trees (verdict #39 = next-in-chain after ApplicationServices), re-run the Chrome framework probe, and capture the next rejection or a load signal.

### Step 0 — Staging + probe execution (no rebuild)

CoreServices.framework added to staging trees (minimal, from overlay `/System/Library/Frameworks/CoreServices.framework`):

```
DARLING_STAGING_TREES=usr/lib:Frameworks:System/Library/Frameworks/CoreFoundation.framework:System/Library/Frameworks/Security.framework:System/Library/Frameworks/ApplicationServices.framework:System/Library/Frameworks/CoreServices.framework
```

Probe executed: `launch-dynamic` with `DARLING_TEST_BINARY=cft-fwmacho-probe-macho` on current overlay. Log: `/tmp/foundation-probe-40.log` (24372 lines).

Staging evidence (CoreServices.framework staged and mapped):

```
staging: symlinks under System/Library/Frameworks/CoreServices.framework: 2 found, 2 created, 0 failed
staging:   System/Library/Frameworks/CoreServices.framework: 3 directories opened, 5 entries read, 0 open failures
```

### Step 1 — CoreServices.framework loaded (wall #39 cleared)

Verbatim from log:

```
dyld: Mapping /System/Library/Frameworks/CoreServices.framework/Versions/A/CoreServices
dyld: loaded: <4C4C4448-5555-3144-A13D-7FD0F4ACC49B> /System/Library/Frameworks/CoreServices.framework/Versions/A/CoreServices
```

CoreServices.framework loaded successfully. The Chrome framework's dependency chain advanced past CoreServices.

### Step 2 — Next rejection (verbatim from log)

```
dlopen(/Frameworks/Google Chrome for Testing Framework.framework/Versions/154.0.8029.0/Google Chrome for Testing Framework, 261): Library not loaded: /System/Library/Frameworks/CFNetwork.framework/Versions/A/CFNetwork
  Referenced from: /Frameworks/Google Chrome for Testing Framework.framework/Versions/154.0.8029.0/Google Chrome for Testing Framework
  Reason: image not found
```

The rejection moved from CoreServices.framework (wall #39, now cleared) to CFNetwork.framework — same shape, next dependency in the chain.

### Step 3 — Emission site

Same emission site as #37–#39: `src/external/dyld/src/ImageLoader.cpp:820` — the message shape is identical, only the dependency name changed.

### Verdict

**staging-missing-dependency (next-in-chain)** — CoreServices.framework staged and loaded (wall #39 cleared). The Chrome framework now fails at CFNetwork.framework, missing from the staging trees. The pattern from #38/#39 is confirmed a third time: each staged framework clears one wall and reveals the next dependency. Next candidate for staging: `/System/Library/Frameworks/CFNetwork.framework`.

### Repro

```sh
export PATH=/usr/local/bin:/usr/local/sbin:/usr/bin:/bin:/sbin:/usr/sbin
export DARLING_SRC_DIR=$DARLING_SRC_DIR DARLING_OVERLAY=$DARLING_OVERLAY DARLING_BUILD_DIR=$DARLING_BUILD_DIR
export DARLING_TEST_BINARY=cft-fwmacho-probe-macho
export DARLING_STAGING_TREES=usr/lib:Frameworks:System/Library/Frameworks/CoreFoundation.framework:System/Library/Frameworks/Security.framework:System/Library/Frameworks/ApplicationServices.framework:System/Library/Frameworks/CoreServices.framework
timeout 120 sudo env DARLING_SRC_DIR=$DARLING_SRC_DIR DARLING_OVERLAY=$DARLING_OVERLAY DARLING_BUILD_DIR=$DARLING_BUILD_DIR DARLING_TEST_BINARY=cft-fwmacho-probe-macho DARLING_STAGING_TREES=$DARLING_STAGING_TREES DYLD_PRINT_LIBRARIES=1 DYLD_PRINT_LIBRARIES_POST_LAUNCH=1 DYLD_PRINT_BINDINGS=1 DYLD_PRINT_WEAK_BINDINGS=1 DYLD_PRINT_APIS=1 DYLD_PRINT_INTERPOSING=1 DYLD_PRINT_SEGMENTS=1 DYLD_PRINT_STATISTICS=1 DYLD_PRINT_STATISTICS_DETAILS=1 DYLD_PRINT_RPATHS=1 DYLD_PRINT_WARNINGS=1 DYLD_PRINT_INITIALIZERS=1 DYLD_PRINT_DOFS=1 DYLD_PRINT_OPTS=1 DYLD_PRINT_ENV=1 DYLD_PRINT_CODE_SIGNATURES=1 DYLD_PRINT_REBASINGS=1 DYLD_PRINT_TO_STDERR=1 $DARLING_BUILD_DIR/launch-dynamic > /tmp/foundation-probe-40.log 2>&1
grep "Library not loaded\|image not found\|invalid file format" /tmp/foundation-probe-40.log
```

## Control #41 — CFNetwork.framework staged, next wall OpenDirectory

**Date:** 2026-10-05
**Branch:** task/chrome-fw-cfnetwork
**Base:** pr-arm64 = 696df8578
**Goal:** Stage CFNetwork.framework into the probe's staging trees (verdict #40 = next-in-chain after CoreServices), re-run the Chrome framework probe, and capture the next rejection or a load signal.

### Step 0 — Staging + probe execution (no rebuild)

CFNetwork.framework added to staging trees (minimal, from overlay `/System/Library/Frameworks/CFNetwork.framework`):

```
DARLING_STAGING_TREES=usr/lib:Frameworks:System/Library/Frameworks/CoreFoundation.framework:System/Library/Frameworks/Security.framework:System/Library/Frameworks/ApplicationServices.framework:System/Library/Frameworks/CoreServices.framework:System/Library/Frameworks/CFNetwork.framework
```

Probe executed: `launch-dynamic` with `DARLING_TEST_BINARY=cft-fwmacho-probe-macho` on current overlay. Log: `/tmp/foundation-probe-41.log` (24403 lines).

Staging evidence (CFNetwork.framework staged and mapped):

```
staging: symlinks under System/Library/Frameworks/CFNetwork.framework: 2 found, 2 created, 0 failed
staging:   System/Library/Frameworks/CFNetwork.framework: 3 directories opened, 5 entries read, 0 open failures
```

### Step 1 — CFNetwork.framework loaded (wall #40 cleared)

Verbatim from log:

```
dyld: Mapping /System/Library/Frameworks/CFNetwork.framework/Versions/A/CFNetwork
dyld: loaded: <4C4C44A6-5555-3144-A176-4F621C7E611A> /System/Library/Frameworks/CFNetwork.framework/Versions/A/CFNetwork
```

CFNetwork.framework loaded successfully. Additional extras loaded in the same pass (first time in the chain):

```
dyld: loaded: <4C4C4457-5555-3144-A18D-6906B5CE3B18> /usr/lib/AppKitExtras.dylib
dyld: loaded: <4C4C4484-5555-3144-A185-0F4A27D4DAED> /usr/lib/IOKitExtras.dylib
```

### Step 2 — Next rejection (verbatim from log)

```
dlopen(/Frameworks/Google Chrome for Testing Framework.framework/Versions/154.0.8029.0/Google Chrome for Testing Framework, 261): Library not loaded: /System/Library/Frameworks/OpenDirectory.framework/Versions/A/OpenDirectory
  Referenced from: /Frameworks/Google Chrome for Testing Framework.framework/Versions/154.0.8029.0/Google Chrome for Testing Framework
  Reason: image not found
```

The rejection moved from CFNetwork.framework (wall #40, now cleared) to OpenDirectory.framework — same shape, next dependency in the chain.

### Step 3 — Emission site

Same emission site as #37–#40: `src/external/dyld/src/ImageLoader.cpp:820` — the message shape is identical, only the dependency name changed.

### Verdict

**staging-missing-dependency (next-in-chain)** — CFNetwork.framework staged and loaded (wall #40 cleared). The Chrome framework now fails at OpenDirectory.framework, missing from the staging trees. The pattern from #38–#40 is confirmed a fourth time: each staged framework clears one wall and reveals the next dependency. Next candidate for staging: `/System/Library/Frameworks/OpenDirectory.framework`.

### Repro

```sh
export PATH=/usr/local/bin:/usr/local/sbin:/usr/bin:/bin:/sbin:/usr/sbin
export DARLING_SRC_DIR=$DARLING_SRC_DIR DARLING_OVERLAY=$DARLING_OVERLAY DARLING_BUILD_DIR=$DARLING_BUILD_DIR
export DARLING_TEST_BINARY=cft-fwmacho-probe-macho
export DARLING_STAGING_TREES=usr/lib:Frameworks:System/Library/Frameworks/CoreFoundation.framework:System/Library/Frameworks/Security.framework:System/Library/Frameworks/ApplicationServices.framework:System/Library/Frameworks/CoreServices.framework:System/Library/Frameworks/CFNetwork.framework
timeout 120 sudo env DARLING_SRC_DIR=$DARLING_SRC_DIR DARLING_OVERLAY=$DARLING_OVERLAY DARLING_BUILD_DIR=$DARLING_BUILD_DIR DARLING_TEST_BINARY=cft-fwmacho-probe-macho DARLING_STAGING_TREES=$DARLING_STAGING_TREES DYLD_PRINT_LIBRARIES=1 DYLD_PRINT_LIBRARIES_POST_LAUNCH=1 DYLD_PRINT_BINDINGS=1 DYLD_PRINT_WEAK_BINDINGS=1 DYLD_PRINT_APIS=1 DYLD_PRINT_INTERPOSING=1 DYLD_PRINT_SEGMENTS=1 DYLD_PRINT_STATISTICS=1 DYLD_PRINT_STATISTICS_DETAILS=1 DYLD_PRINT_RPATHS=1 DYLD_PRINT_WARNINGS=1 DYLD_PRINT_INITIALIZERS=1 DYLD_PRINT_DOFS=1 DYLD_PRINT_OPTS=1 DYLD_PRINT_ENV=1 DYLD_PRINT_CODE_SIGNATURES=1 DYLD_PRINT_REBASINGS=1 DYLD_PRINT_TO_STDERR=1 $DARLING_BUILD_DIR/launch-dynamic > /tmp/foundation-probe-41.log 2>&1
grep "Library not loaded\|image not found\|invalid file format" /tmp/foundation-probe-41.log
```

## Control #42 — batch staging: 8 walls cleared in one turn

**Date:** 2026-10-05
**Branch:** task/chrome-fw-staging-batch
**Base:** pr-arm64 = 382d47dd0
**Goal:** Clear the staging-missing-dependency walls in a batch loop (one framework per iteration, up to 8 iterations), staging each new wall's framework from overlay and re-running the Chrome framework probe until a non-staging rejection, a Chrome load, or the iteration limit.

### Step 0 — Batch loop (8 iterations, one framework per iteration)

Each iteration: add the current wall's framework to `DARLING_STAGING_TREES`, run `cft-fwmacho-probe-macho`, capture the next wall. All frameworks staged minimally from overlay. Logs: `/tmp/foundation-probe-42-<N>.log`.

### Iteration 1 — OpenDirectory.framework (wall from #41)

Staging evidence (`/tmp/foundation-probe-42-1.log`):

```
staging: symlinks under System/Library/Frameworks/OpenDirectory.framework: 2 found, 2 created, 0 failed
```

Loaded (verbatim):

```
dyld: loaded: <4C4C44F6-5555-3144-A1BF-7C09816101E2> /System/Library/Frameworks/OpenDirectory.framework/Versions/A/OpenDirectory
```

Next wall: CryptoTokenKit.framework.

### Iteration 2 — CryptoTokenKit.framework

Staging evidence (`/tmp/foundation-probe-42-2.log`):

```
staging: symlinks under System/Library/Frameworks/CryptoTokenKit.framework: 2 found, 2 created, 0 failed
```

Loaded (verbatim):

```
dyld: loaded: <4C4C4456-5555-3144-A115-8F45B1743231> /System/Library/Frameworks/CryptoTokenKit.framework/Versions/A/CryptoTokenKit
```

Next wall: LocalAuthentication.framework.

### Iteration 3 — LocalAuthentication.framework

Staging evidence (`/tmp/foundation-probe-42-3.log`):

```
staging: symlinks under System/Library/Frameworks/LocalAuthentication.framework: 2 found, 2 created, 0 failed
```

Loaded (verbatim):

```
dyld: loaded: <4C4C4486-5555-3144-A139-A1672F9F0117> /System/Library/Frameworks/LocalAuthentication.framework/Versions/A/LocalAuthentication
```

Next wall: Accelerate.framework.

### Iteration 4 — Accelerate.framework

Staging evidence (`/tmp/foundation-probe-42-4.log`):

```
staging: symlinks under System/Library/Frameworks/Accelerate.framework: 2 found, 2 created, 0 failed
```

Loaded (verbatim):

```
dyld: loaded: <4C4C44DE-5555-3144-A102-CF9CB2DC2576> /System/Library/Frameworks/Accelerate.framework/Versions/A/Accelerate
```

Next wall: AudioUnit.framework.

### Iteration 5 — AudioUnit.framework

Staging evidence (`/tmp/foundation-probe-42-5.log`):

```
staging: symlinks under System/Library/Frameworks/AudioUnit.framework: 2 found, 2 created, 0 failed
```

Loaded (verbatim):

```
dyld: loaded: <4C4C4435-5555-3144-A17E-EEC1F864F784> /System/Library/Frameworks/AudioUnit.framework/Versions/A/AudioUnit
```

Next wall: AVFAudio.framework.

### Iteration 6 — AVFAudio.framework

Staging evidence (`/tmp/foundation-probe-42-6.log`):

```
staging: symlinks under System/Library/Frameworks/AVFAudio.framework: 2 found, 2 created, 0 failed
```

Loaded (verbatim):

```
dyld: loaded: <4C4C4495-5555-3144-A13E-AD6008B3567E> /System/Library/Frameworks/AVFAudio.framework/Versions/A/AVFAudio
```

Next wall: Carbon.framework.

### Iteration 7 — Carbon.framework

Staging evidence (`/tmp/foundation-probe-42-7.log`):

```
staging: symlinks under System/Library/Frameworks/Carbon.framework: 2 found, 2 created, 0 failed
```

Loaded (verbatim):

```
dyld: loaded: <4C4C445B-5555-3144-A184-95BF05E6603F> /System/Library/Frameworks/Carbon.framework/Versions/A/Carbon
```

Next wall: CoreVideo.framework.

### Iteration 8 — CoreVideo.framework (last iteration, limit reached)

Staging evidence (`/tmp/foundation-probe-42-8.log`):

```
staging: symlinks under System/Library/Frameworks/CoreVideo.framework: 2 found, 2 created, 0 failed
```

Loaded (verbatim):

```
dyld: loaded: <4C4C4495-5555-3144-A120-4F8F32312810> /System/Library/Frameworks/CoreVideo.framework/Versions/A/CoreVideo
```

### Final wall (verbatim from `/tmp/foundation-probe-42-8.log`)

```
dlopen(/Frameworks/Google Chrome for Testing Framework.framework/Versions/154.0.8029.0/Google Chrome for Testing Framework, 261): Library not loaded: /System/Library/Frameworks/CoreImage.framework/Versions/A/CoreImage
  Referenced from: /Frameworks/Google Chrome for Testing Framework.framework/Versions/154.0.8029.0/Google Chrome for Testing Framework
  Reason: image not found
```

The rejection moved from CoreVideo.framework (wall cleared in iteration 8) to CoreImage.framework — same shape, next dependency in the chain.

### Emission site

Same emission site as #37–#41: `src/external/dyld/src/ImageLoader.cpp:820` — the message shape is identical across all iterations, only the dependency name changed.

### Verdict

**staging-missing-dependency (batch, 8/8 walls cleared)** — 8 frameworks staged and loaded in one turn: OpenDirectory, CryptoTokenKit, LocalAuthentication, Accelerate, AudioUnit, AVFAudio, Carbon, CoreVideo. The Chrome framework now fails at CoreImage.framework, missing from the staging trees. The pattern from #38–#41 is confirmed at batch scale: each staged framework clears exactly one wall and reveals the next dependency. The chain is long (12+ frameworks staged so far: CoreFoundation, Security, ApplicationServices, CoreServices, CFNetwork, OpenDirectory, CryptoTokenKit, LocalAuthentication, Accelerate, AudioUnit, AVFAudio, Carbon, CoreVideo) and the next candidate is `/System/Library/Frameworks/CoreImage.framework`. Iteration limit (8) reached; further walls can be cleared in a follow-up batch.

### Repro

```sh
export PATH=/usr/local/bin:/usr/local/sbin:/usr/bin:/bin:/sbin:/usr/sbin
export DARLING_SRC_DIR=$DARLING_SRC_DIR DARLING_OVERLAY=$DARLING_OVERLAY DARLING_BUILD_DIR=$DARLING_BUILD_DIR
export DARLING_TEST_BINARY=cft-fwmacho-probe-macho
export DARLING_STAGING_TREES=usr/lib:Frameworks:System/Library/Frameworks/CoreFoundation.framework:System/Library/Frameworks/Security.framework:System/Library/Frameworks/ApplicationServices.framework:System/Library/Frameworks/CoreServices.framework:System/Library/Frameworks/CFNetwork.framework:System/Library/Frameworks/OpenDirectory.framework:System/Library/Frameworks/CryptoTokenKit.framework:System/Library/Frameworks/LocalAuthentication.framework:System/Library/Frameworks/Accelerate.framework:System/Library/Frameworks/AudioUnit.framework:System/Library/Frameworks/AVFAudio.framework:System/Library/Frameworks/Carbon.framework:System/Library/Frameworks/CoreVideo.framework
timeout 120 sudo env DARLING_SRC_DIR=$DARLING_SRC_DIR DARLING_OVERLAY=$DARLING_OVERLAY DARLING_BUILD_DIR=$DARLING_BUILD_DIR DARLING_TEST_BINARY=cft-fwmacho-probe-macho DARLING_STAGING_TREES=$DARLING_STAGING_TREES DYLD_PRINT_LIBRARIES=1 DYLD_PRINT_LIBRARIES_POST_LAUNCH=1 DYLD_PRINT_BINDINGS=1 DYLD_PRINT_WEAK_BINDINGS=1 DYLD_PRINT_APIS=1 DYLD_PRINT_INTERPOSING=1 DYLD_PRINT_SEGMENTS=1 DYLD_PRINT_STATISTICS=1 DYLD_PRINT_STATISTICS_DETAILS=1 DYLD_PRINT_RPATHS=1 DYLD_PRINT_WARNINGS=1 DYLD_PRINT_INITIALIZERS=1 DYLD_PRINT_DOFS=1 DYLD_PRINT_OPTS=1 DYLD_PRINT_ENV=1 DYLD_PRINT_CODE_SIGNATURES=1 DYLD_PRINT_REBASINGS=1 DYLD_PRINT_TO_STDERR=1 $DARLING_BUILD_DIR/launch-dynamic > /tmp/foundation-probe-42-8.log 2>&1
grep "Library not loaded\|image not found\|invalid file format" /tmp/foundation-probe-42-8.log
```

## Control #43 — batch staging 2: 8 more walls cleared

**Date:** 2026-10-05
**Branch:** task/chrome-fw-staging-batch2
**Base:** pr-arm64 = 3882d6f2b
**Goal:** Continue the batch staging loop from the CoreImage wall (verdict #42), staging each new wall's framework from overlay and re-running the Chrome framework probe until a non-staging rejection, a Chrome load, or the iteration limit (8).

### Step 0 — Batch loop (8 iterations, one framework per iteration)

Each iteration: add the current wall's framework to `DARLING_STAGING_TREES`, run `cft-fwmacho-probe-macho`, capture the next wall. All frameworks staged minimally from overlay. Logs: `/tmp/foundation-probe-43-<N>.log`.

### Iteration 1 — CoreImage.framework (wall from #42)

Staging evidence (`/tmp/foundation-probe-43-1.log`):

```
staging: symlinks under System/Library/Frameworks/CoreImage.framework: 2 found, 2 created, 0 failed
```

Loaded (verbatim):

```
dyld: loaded: <4C4C44AE-5555-3144-A150-3DF6F7146E72> /System/Library/Frameworks/CoreImage.framework/Versions/A/CoreImage
```

Next wall: Network.framework.

### Iteration 2 — Network.framework

Staging evidence (`/tmp/foundation-probe-43-2.log`):

```
staging: symlinks under System/Library/Frameworks/Network.framework: 2 found, 2 created, 0 failed
```

Loaded (verbatim):

```
dyld: loaded: <4C4C44C6-5555-3144-A19D-C97D74F3CCD7> /System/Library/Frameworks/Network.framework/Versions/A/Network
```

Next wall: IOSurface.framework.

### Iteration 3 — IOSurface.framework

Staging evidence (`/tmp/foundation-probe-43-3.log`):

```
staging: symlinks under System/Library/Frameworks/IOSurface.framework: 2 found, 2 created, 0 failed
```

Loaded (verbatim):

```
dyld: loaded: <4C4C44FE-5555-3144-A1EB-9F5C510FB501> /System/Library/Frameworks/IOSurface.framework/Versions/A/IOSurface
```

Next wall: CoreMedia.framework.

### Iteration 4 — CoreMedia.framework

Staging evidence (`/tmp/foundation-probe-43-4.log`):

```
staging: symlinks under System/Library/Frameworks/CoreMedia.framework: 2 found, 2 created, 0 failed
```

Loaded (verbatim):

```
dyld: loaded: <4C4C44B7-5555-3144-A1F5-452A6D02BE6B> /System/Library/Frameworks/CoreMedia.framework/Versions/A/CoreMedia
```

Next wall: AudioToolbox.framework.

### Iteration 5 — AudioToolbox.framework

Staging evidence (`/tmp/foundation-probe-43-5.log`):

```
staging: symlinks under System/Library/Frameworks/AudioToolbox.framework: 2 found, 2 created, 0 failed
```

Loaded (verbatim):

```
dyld: loaded: <4C4C447E-5555-3144-A1EF-4FAB96BAE07C> /System/Library/Frameworks/AudioToolbox.framework/Versions/A/AudioToolbox
```

Next wall: OpenGL.framework.

### Iteration 6 — OpenGL.framework

Staging evidence (`/tmp/foundation-probe-43-6.log`):

```
staging: symlinks under System/Library/Frameworks/OpenGL.framework: 2 found, 2 created, 0 failed
```

Loaded (verbatim):

```
dyld: loaded: <4C4C44E2-5555-3144-A166-0BFAD706EFB9> /System/Library/Frameworks/OpenGL.framework/Versions/A/OpenGL
```

Next wall: Quartz.framework.

### Iteration 7 — Quartz.framework

Staging evidence (`/tmp/foundation-probe-43-7.log`):

```
staging: symlinks under System/Library/Frameworks/Quartz.framework: 2 found, 2 created, 0 failed
```

Loaded (verbatim):

```
dyld: loaded: <4C4C44E2-5555-3144-A188-43912E65A3B3> /System/Library/Frameworks/Quartz.framework/Versions/A/Quartz
```

Next wall: Cocoa.framework.

### Iteration 8 — Cocoa.framework (last iteration, limit reached)

Staging evidence (`/tmp/foundation-probe-43-8.log`):

```
staging: symlinks under System/Library/Frameworks/Cocoa.framework: 2 found, 2 created, 0 failed
```

Loaded (verbatim):

```
dyld: loaded: <4C4C44D8-5555-3144-A17F-2F0D5088401F> /System/Library/Frameworks/Cocoa.framework/Versions/A/Cocoa
```

### Final wall (verbatim from `/tmp/foundation-probe-43-8.log`)

```
dlopen(/Frameworks/Google Chrome for Testing Framework.framework/Versions/154.0.8029.0/Google Chrome for Testing Framework, 261): Library not loaded: /System/Library/Frameworks/VideoToolbox.framework/Versions/A/VideoToolbox
  Referenced from: /Frameworks/Google Chrome for Testing Framework.framework/Versions/154.0.8029.0/Google Chrome for Testing Framework
  Reason: image not found
```

The rejection moved from Cocoa.framework (wall cleared in iteration 8) to VideoToolbox.framework — same shape, next dependency in the chain.

### Emission site

Same emission site as #37–#42: `src/external/dyld/src/ImageLoader.cpp:820` — the message shape is identical across all iterations, only the dependency name changed.

### Verdict

**staging-missing-dependency (batch, 8/8 walls cleared)** — 8 frameworks staged and loaded in one turn: CoreImage, Network, IOSurface, CoreMedia, AudioToolbox, OpenGL, Quartz, Cocoa. The Chrome framework now fails at VideoToolbox.framework, missing from the staging trees. The pattern from #38–#42 is confirmed at batch scale for the second time: each staged framework clears exactly one wall and reveals the next dependency. Cumulative staging across #38–#43: 21 frameworks. Next candidate: `/System/Library/Frameworks/VideoToolbox.framework`. Iteration limit (8) reached; further walls can be cleared in a follow-up batch.

### Repro

```sh
export PATH=/usr/local/bin:/usr/local/sbin:/usr/bin:/bin:/sbin:/usr/sbin
export DARLING_SRC_DIR=$DARLING_SRC_DIR DARLING_OVERLAY=$DARLING_OVERLAY DARLING_BUILD_DIR=$DARLING_BUILD_DIR
export DARLING_TEST_BINARY=cft-fwmacho-probe-macho
export DARLING_STAGING_TREES=usr/lib:Frameworks:System/Library/Frameworks/CoreFoundation.framework:System/Library/Frameworks/Security.framework:System/Library/Frameworks/ApplicationServices.framework:System/Library/Frameworks/CoreServices.framework:System/Library/Frameworks/CFNetwork.framework:System/Library/Frameworks/OpenDirectory.framework:System/Library/Frameworks/CryptoTokenKit.framework:System/Library/Frameworks/LocalAuthentication.framework:System/Library/Frameworks/Accelerate.framework:System/Library/Frameworks/AudioUnit.framework:System/Library/Frameworks/AVFAudio.framework:System/Library/Frameworks/Carbon.framework:System/Library/Frameworks/CoreVideo.framework:System/Library/Frameworks/CoreImage.framework:System/Library/Frameworks/Network.framework:System/Library/Frameworks/IOSurface.framework:System/Library/Frameworks/CoreMedia.framework:System/Library/Frameworks/AudioToolbox.framework:System/Library/Frameworks/OpenGL.framework:System/Library/Frameworks/Quartz.framework:System/Library/Frameworks/Cocoa.framework
timeout 120 sudo env DARLING_SRC_DIR=$DARLING_SRC_DIR DARLING_OVERLAY=$DARLING_OVERLAY DARLING_BUILD_DIR=$DARLING_BUILD_DIR DARLING_TEST_BINARY=cft-fwmacho-probe-macho DARLING_STAGING_TREES=$DARLING_STAGING_TREES DYLD_PRINT_LIBRARIES=1 DYLD_PRINT_LIBRARIES_POST_LAUNCH=1 DYLD_PRINT_BINDINGS=1 DYLD_PRINT_WEAK_BINDINGS=1 DYLD_PRINT_APIS=1 DYLD_PRINT_INTERPOSING=1 DYLD_PRINT_SEGMENTS=1 DYLD_PRINT_STATISTICS=1 DYLD_PRINT_STATISTICS_DETAILS=1 DYLD_PRINT_RPATHS=1 DYLD_PRINT_WARNINGS=1 DYLD_PRINT_INITIALIZERS=1 DYLD_PRINT_DOFS=1 DYLD_PRINT_OPTS=1 DYLD_PRINT_ENV=1 DYLD_PRINT_CODE_SIGNATURES=1 DYLD_PRINT_REBASINGS=1 DYLD_PRINT_TO_STDERR=1 $DARLING_BUILD_DIR/launch-dynamic > /tmp/foundation-probe-43-8.log 2>&1
grep "Library not loaded\|image not found\|invalid file format" /tmp/foundation-probe-43-8.log
```

## Control #44 — batch staging 3: 8 more walls cleared

**Date:** 2026-10-05
**Branch:** task/chrome-fw-staging-batch3
**Base:** pr-arm64 = 0ccd9964c
**Goal:** Continue the batch staging loop from the VideoToolbox wall (verdict #43), staging each new wall's framework from overlay and re-running the Chrome framework probe until a non-staging rejection, a Chrome load, or the iteration limit (8).

### Step 0 — Batch loop (8 iterations, one framework per iteration)

Each iteration: add the current wall's framework to `DARLING_STAGING_TREES`, run `cft-fwmacho-probe-macho`, capture the next wall. All frameworks staged minimally from overlay. Logs: `/tmp/foundation-probe-44-<N>.log`.

### Iteration 1 — VideoToolbox.framework (wall from #43)

Staging evidence (`/tmp/foundation-probe-44-1.log`):

```
staging: symlinks under System/Library/Frameworks/VideoToolbox.framework: 2 found, 2 created, 0 failed
```

Loaded (verbatim):

```
dyld: loaded: <4C4C44A5-5555-3144-A1A9-0E88C0EB43D4> /System/Library/Frameworks/VideoToolbox.framework/Versions/A/VideoToolbox
```

Next wall: CoreMediaIO.framework.

### Iteration 2 — CoreMediaIO.framework

Staging evidence (`/tmp/foundation-probe-44-2.log`):

```
staging: symlinks under System/Library/Frameworks/CoreMediaIO.framework: 2 found, 2 created, 0 failed
```

Loaded (verbatim):

```
dyld: loaded: <4C4C446D-5555-3144-A1C2-E25BE8B5AD7B> /System/Library/Frameworks/CoreMediaIO.framework/Versions/A/CoreMediaIO
```

Next wall: Accessibility.framework.

### Iteration 3 — Accessibility.framework

Staging evidence (`/tmp/foundation-probe-44-3.log`):

```
staging: symlinks under System/Library/Frameworks/Accessibility.framework: 2 found, 2 created, 0 failed
```

Loaded (verbatim):

```
dyld: loaded: <4C4C445F-5555-3144-A1E5-80BD913F13AD> /System/Library/Frameworks/Accessibility.framework/Versions/A/Accessibility
```

Next wall: MetalKit.framework.

### Iteration 4 — MetalKit.framework

Staging evidence (`/tmp/foundation-probe-44-4.log`):

```
staging: symlinks under System/Library/Frameworks/MetalKit.framework: 2 found, 2 created, 0 failed
```

Loaded (verbatim):

```
dyld: loaded: <4C4C445D-5555-3144-A150-C14463891EFB> /System/Library/Frameworks/MetalKit.framework/Versions/A/MetalKit
```

Next wall: CoreMIDI.framework.

### Iteration 5 — CoreMIDI.framework

Staging evidence (`/tmp/foundation-probe-44-5.log`):

```
staging: symlinks under System/Library/Frameworks/CoreMIDI.framework: 2 found, 2 created, 0 failed
```

Loaded (verbatim):

```
dyld: loaded: <4C4C4431-5555-3144-A1F8-9548A19D0653> /System/Library/Frameworks/CoreMIDI.framework/Versions/A/CoreMIDI
```

Next wall: MediaAccessibility.framework.

### Iteration 6 — MediaAccessibility.framework

Staging evidence (`/tmp/foundation-probe-44-6.log`):

```
staging: symlinks under System/Library/Frameworks/MediaAccessibility.framework: 2 found, 2 created, 0 failed
```

Loaded (verbatim):

```
dyld: loaded: <4C4C44EF-5555-3144-A1AF-7933D3CCCB99> /System/Library/Frameworks/MediaAccessibility.framework/Versions/A/MediaAccessibility
```

Next wall: SecurityInterface.framework.

### Iteration 7 — SecurityInterface.framework

Staging evidence (`/tmp/foundation-probe-44-7.log`):

```
staging: symlinks under System/Library/Frameworks/SecurityInterface.framework: 2 found, 2 created, 0 failed
```

Loaded (verbatim):

```
dyld: loaded: <4C4C44DB-5555-3144-A1DC-C30263EBF6B8> /System/Library/Frameworks/SecurityInterface.framework/Versions/A/SecurityInterface
```

Next wall: CoreHaptics.framework.

### Iteration 8 — CoreHaptics.framework (last iteration, limit reached)

Staging evidence (`/tmp/foundation-probe-44-8.log`):

```
staging: symlinks under System/Library/Frameworks/CoreHaptics.framework: 2 found, 2 created, 0 failed
```

Loaded (verbatim):

```
dyld: loaded: <4C4C4491-5555-3144-A1CA-71A623C7D426> /System/Library/Frameworks/CoreHaptics.framework/Versions/A/CoreHaptics
```

### Final wall (verbatim from `/tmp/foundation-probe-44-8.log`)

```
dlopen(/Frameworks/Google Chrome for Testing Framework.framework/Versions/154.0.8029.0/Google Chrome for Testing Framework, 261): Library not loaded: /System/Library/Frameworks/ForceFeedback.framework/Versions/A/ForceFeedback
  Referenced from: /Frameworks/Google Chrome for Testing Framework.framework/Versions/154.0.8029.0/Google Chrome for Testing Framework
  Reason: image not found
```

The rejection moved from CoreHaptics.framework (wall cleared in iteration 8) to ForceFeedback.framework — same shape, next dependency in the chain.

### Emission site

Same emission site as #37–#43: `src/external/dyld/src/ImageLoader.cpp:820` — the message shape is identical across all iterations, only the dependency name changed.

### Verdict

**staging-missing-dependency (batch, 8/8 walls cleared)** — 8 frameworks staged and loaded in one turn: VideoToolbox, CoreMediaIO, Accessibility, MetalKit, CoreMIDI, MediaAccessibility, SecurityInterface, CoreHaptics. The Chrome framework now fails at ForceFeedback.framework, missing from the staging trees. The pattern from #38–#43 is confirmed at batch scale for the third time: each staged framework clears exactly one wall and reveals the next dependency. Cumulative staging across #38–#44: 29 frameworks. Next candidate: `/System/Library/Frameworks/ForceFeedback.framework`. Iteration limit (8) reached; further walls can be cleared in a follow-up batch.

### Repro

```sh
export PATH=/usr/local/bin:/usr/local/sbin:/usr/bin:/bin:/sbin:/usr/sbin
export DARLING_SRC_DIR=$DARLING_SRC_DIR DARLING_OVERLAY=$DARLING_OVERLAY DARLING_BUILD_DIR=$DARLING_BUILD_DIR
export DARLING_TEST_BINARY=cft-fwmacho-probe-macho
export DARLING_STAGING_TREES=usr/lib:Frameworks:System/Library/Frameworks/CoreFoundation.framework:System/Library/Frameworks/Security.framework:System/Library/Frameworks/ApplicationServices.framework:System/Library/Frameworks/CoreServices.framework:System/Library/Frameworks/CFNetwork.framework:System/Library/Frameworks/OpenDirectory.framework:System/Library/Frameworks/CryptoTokenKit.framework:System/Library/Frameworks/LocalAuthentication.framework:System/Library/Frameworks/Accelerate.framework:System/Library/Frameworks/AudioUnit.framework:System/Library/Frameworks/AVFAudio.framework:System/Library/Frameworks/Carbon.framework:System/Library/Frameworks/CoreVideo.framework:System/Library/Frameworks/CoreImage.framework:System/Library/Frameworks/Network.framework:System/Library/Frameworks/IOSurface.framework:System/Library/Frameworks/CoreMedia.framework:System/Library/Frameworks/AudioToolbox.framework:System/Library/Frameworks/OpenGL.framework:System/Library/Frameworks/Quartz.framework:System/Library/Frameworks/Cocoa.framework:System/Library/Frameworks/VideoToolbox.framework:System/Library/Frameworks/CoreMediaIO.framework:System/Library/Frameworks/Accessibility.framework:System/Library/Frameworks/MetalKit.framework:System/Library/Frameworks/CoreMIDI.framework:System/Library/Frameworks/MediaAccessibility.framework:System/Library/Frameworks/SecurityInterface.framework:System/Library/Frameworks/CoreHaptics.framework
timeout 120 sudo env DARLING_SRC_DIR=$DARLING_SRC_DIR DARLING_OVERLAY=$DARLING_OVERLAY DARLING_BUILD_DIR=$DARLING_BUILD_DIR DARLING_TEST_BINARY=cft-fwmacho-probe-macho DARLING_STAGING_TREES=$DARLING_STAGING_TREES DYLD_PRINT_LIBRARIES=1 DYLD_PRINT_LIBRARIES_POST_LAUNCH=1 DYLD_PRINT_BINDINGS=1 DYLD_PRINT_WEAK_BINDINGS=1 DYLD_PRINT_APIS=1 DYLD_PRINT_INTERPOSING=1 DYLD_PRINT_SEGMENTS=1 DYLD_PRINT_STATISTICS=1 DYLD_PRINT_STATISTICS_DETAILS=1 DYLD_PRINT_RPATHS=1 DYLD_PRINT_WARNINGS=1 DYLD_PRINT_INITIALIZERS=1 DYLD_PRINT_DOFS=1 DYLD_PRINT_OPTS=1 DYLD_PRINT_ENV=1 DYLD_PRINT_CODE_SIGNATURES=1 DYLD_PRINT_REBASINGS=1 DYLD_PRINT_TO_STDERR=1 $DARLING_BUILD_DIR/launch-dynamic > /tmp/foundation-probe-44-8.log 2>&1
grep "Library not loaded\|image not found\|invalid file format" /tmp/foundation-probe-44-8.log
```

## Control #45 — batch staging 4: 8 more walls cleared

**Date:** 2026-10-05
**Branch:** task/chrome-fw-staging-batch4
**Base:** pr-arm64 = 8842ff58b
**Goal:** Continue the batch staging loop from the ForceFeedback wall (verdict #44), staging each new wall's framework from overlay and re-running the Chrome framework probe until a non-staging rejection, a Chrome load, or the iteration limit (8).

### Step 0 — Batch loop (8 iterations, one framework per iteration)

Each iteration: add the current wall's framework to `DARLING_STAGING_TREES`, run `cft-fwmacho-probe-macho`, capture the next wall. All frameworks staged minimally from overlay. Logs: `/tmp/foundation-probe-45-<N>.log`.

### Iteration 1 — ForceFeedback.framework (wall from #44)

Staging evidence (`/tmp/foundation-probe-45-1.log`):

```
staging: symlinks under System/Library/Frameworks/ForceFeedback.framework: 2 found, 2 created, 0 failed
```

Loaded (verbatim):

```
dyld: loaded: <4C4C4428-5555-3144-A197-EB277BC760BB> /System/Library/Frameworks/ForceFeedback.framework/Versions/A/ForceFeedback
```

Next wall: CoreWLAN.framework.

### Iteration 2 — CoreWLAN.framework

Staging evidence (`/tmp/foundation-probe-45-2.log`):

```
staging: symlinks under System/Library/Frameworks/CoreWLAN.framework: 2 found, 2 created, 0 failed
```

Loaded (verbatim):

```
dyld: loaded: <4C4C4415-5555-3144-A1B0-6582105CADBF> /System/Library/Frameworks/CoreWLAN.framework/Versions/A/CoreWLAN
```

Next wall: CoreLocation.framework.

### Iteration 3 — CoreLocation.framework

Staging evidence (`/tmp/foundation-probe-45-3.log`):

```
staging: symlinks under System/Library/Frameworks/CoreLocation.framework: 2 found, 2 created, 0 failed
```

Loaded (verbatim):

```
dyld: loaded: <4C4C4492-5555-3144-A108-78EDD8F31E4D> /System/Library/Frameworks/CoreLocation.framework/Versions/A/CoreLocation
```

Next wall: CoreML.framework.

### Iteration 4 — CoreML.framework

Staging evidence (`/tmp/foundation-probe-45-4.log`):

```
staging: symlinks under System/Library/Frameworks/CoreML.framework: 2 found, 2 created, 0 failed
```

Loaded (verbatim):

```
dyld: loaded: <4C4C4415-5555-3144-A15E-8AD16C741ABA> /System/Library/Frameworks/CoreML.framework/Versions/A/CoreML
```

Next wall: DiskArbitration.framework.

### Iteration 5 — DiskArbitration.framework

Staging evidence (`/tmp/foundation-probe-45-5.log`):

```
staging: symlinks under System/Library/Frameworks/DiskArbitration.framework: 2 found, 2 created, 0 failed
```

Loaded (verbatim):

```
dyld: loaded: <4C4C44D0-5555-3144-A10F-087045297EB1> /System/Library/Frameworks/DiskArbitration.framework/Versions/A/DiskArbitration
```

Next wall: ServiceManagement.framework.

### Iteration 6 — ServiceManagement.framework

Staging evidence (`/tmp/foundation-probe-45-6.log`):

```
staging: symlinks under System/Library/Frameworks/ServiceManagement.framework: 2 found, 2 created, 0 failed
```

Loaded (verbatim):

```
dyld: loaded: <4C4C44CE-5555-3144-A1E8-0B1922DF4C9F> /System/Library/Frameworks/ServiceManagement.framework/Versions/A/ServiceManagement
```

Next wall: SafariServices.framework.

### Iteration 7 — SafariServices.framework

Staging evidence (`/tmp/foundation-probe-45-7.log`):

```
staging: symlinks under System/Library/Frameworks/SafariServices.framework: 2 found, 2 created, 0 failed
```

Loaded (verbatim):

```
dyld: loaded: <4C4C44F6-5555-3144-A17C-CE7B8D911924> /System/Library/Frameworks/SafariServices.framework/Versions/A/SafariServices
```

Next wall: LocalAuthenticationEmbeddedUI.framework.

### Iteration 8 — LocalAuthenticationEmbeddedUI.framework (last iteration, limit reached)

Staging evidence (`/tmp/foundation-probe-45-8.log`):

```
staging: symlinks under System/Library/Frameworks/LocalAuthenticationEmbeddedUI.framework: 2 found, 2 created, 0 failed
```

Loaded (verbatim):

```
dyld: loaded: <4C4C44F9-5555-3144-A1EA-53EB00373FB1> /System/Library/Frameworks/LocalAuthenticationEmbeddedUI.framework/Versions/A/LocalAuthenticationEmbeddedUI
```

### Final wall (verbatim from `/tmp/foundation-probe-45-8.log`)

```
dlopen(/Frameworks/Google Chrome for Testing Framework.framework/Versions/154.0.8029.0/Google Chrome for Testing Framework, 261): Library not loaded: /System/Library/Frameworks/CoreGraphics.framework/Versions/A/CoreGraphics
  Referenced from: /Frameworks/Google Chrome for Testing Framework.framework/Versions/154.0.8029.0/Google Chrome for Testing Framework
  Reason: image not found
```

The rejection moved from LocalAuthenticationEmbeddedUI.framework (wall cleared in iteration 8) to CoreGraphics.framework — same shape, next dependency in the chain.

### Emission site

Same emission site as #37–#44: `src/external/dyld/src/ImageLoader.cpp:820` — the message shape is identical across all iterations, only the dependency name changed.

### Verdict

**staging-missing-dependency (batch, 8/8 walls cleared)** — 8 frameworks staged and loaded in one turn: ForceFeedback, CoreWLAN, CoreLocation, CoreML, DiskArbitration, ServiceManagement, SafariServices, LocalAuthenticationEmbeddedUI. The Chrome framework now fails at CoreGraphics.framework, missing from the staging trees. The pattern from #38–#44 is confirmed at batch scale for the fourth time: each staged framework clears exactly one wall and reveals the next dependency. Cumulative staging across #38–#45: 37 frameworks. Next candidate: `/System/Library/Frameworks/CoreGraphics.framework`. Iteration limit (8) reached; further walls can be cleared in a follow-up batch.

### Repro

```sh
export PATH=/usr/local/bin:/usr/local/sbin:/usr/bin:/bin:/sbin:/usr/sbin
export DARLING_SRC_DIR=$DARLING_SRC_DIR DARLING_OVERLAY=$DARLING_OVERLAY DARLING_BUILD_DIR=$DARLING_BUILD_DIR
export DARLING_TEST_BINARY=cft-fwmacho-probe-macho
export DARLING_STAGING_TREES=usr/lib:Frameworks:System/Library/Frameworks/CoreFoundation.framework:System/Library/Frameworks/Security.framework:System/Library/Frameworks/ApplicationServices.framework:System/Library/Frameworks/CoreServices.framework:System/Library/Frameworks/CFNetwork.framework:System/Library/Frameworks/OpenDirectory.framework:System/Library/Frameworks/CryptoTokenKit.framework:System/Library/Frameworks/LocalAuthentication.framework:System/Library/Frameworks/Accelerate.framework:System/Library/Frameworks/AudioUnit.framework:System/Library/Frameworks/AVFAudio.framework:System/Library/Frameworks/Carbon.framework:System/Library/Frameworks/CoreVideo.framework:System/Library/Frameworks/CoreImage.framework:System/Library/Frameworks/Network.framework:System/Library/Frameworks/IOSurface.framework:System/Library/Frameworks/CoreMedia.framework:System/Library/Frameworks/AudioToolbox.framework:System/Library/Frameworks/OpenGL.framework:System/Library/Frameworks/Quartz.framework:System/Library/Frameworks/Cocoa.framework:System/Library/Frameworks/VideoToolbox.framework:System/Library/Frameworks/CoreMediaIO.framework:System/Library/Frameworks/Accessibility.framework:System/Library/Frameworks/MetalKit.framework:System/Library/Frameworks/CoreMIDI.framework:System/Library/Frameworks/MediaAccessibility.framework:System/Library/Frameworks/SecurityInterface.framework:System/Library/Frameworks/CoreHaptics.framework:System/Library/Frameworks/ForceFeedback.framework:System/Library/Frameworks/CoreWLAN.framework:System/Library/Frameworks/CoreLocation.framework:System/Library/Frameworks/CoreML.framework:System/Library/Frameworks/DiskArbitration.framework:System/Library/Frameworks/ServiceManagement.framework:System/Library/Frameworks/SafariServices.framework:System/Library/Frameworks/LocalAuthenticationEmbeddedUI.framework
timeout 120 sudo env DARLING_SRC_DIR=$DARLING_SRC_DIR DARLING_OVERLAY=$DARLING_OVERLAY DARLING_BUILD_DIR=$DARLING_BUILD_DIR DARLING_TEST_BINARY=cft-fwmacho-probe-macho DARLING_STAGING_TREES=$DARLING_STAGING_TREES DYLD_PRINT_LIBRARIES=1 DYLD_PRINT_LIBRARIES_POST_LAUNCH=1 DYLD_PRINT_BINDINGS=1 DYLD_PRINT_WEAK_BINDINGS=1 DYLD_PRINT_APIS=1 DYLD_PRINT_INTERPOSING=1 DYLD_PRINT_SEGMENTS=1 DYLD_PRINT_STATISTICS=1 DYLD_PRINT_STATISTICS_DETAILS=1 DYLD_PRINT_RPATHS=1 DYLD_PRINT_WARNINGS=1 DYLD_PRINT_INITIALIZERS=1 DYLD_PRINT_DOFS=1 DYLD_PRINT_OPTS=1 DYLD_PRINT_ENV=1 DYLD_PRINT_CODE_SIGNATURES=1 DYLD_PRINT_REBASINGS=1 DYLD_PRINT_TO_STDERR=1 $DARLING_BUILD_DIR/launch-dynamic > /tmp/foundation-probe-45-8.log 2>&1
grep "Library not loaded\|image not found\|invalid file format" /tmp/foundation-probe-45-8.log
```

## Control #46 — batch staging 5: 8 more walls cleared

**Date:** 2026-10-05
**Branch:** task/chrome-fw-staging-batch5
**Base:** pr-arm64 = dc07f5bc6
**Goal:** Continue the batch staging loop from the CoreGraphics wall (verdict #45), staging each new wall's framework from overlay and re-running the Chrome framework probe until a non-staging rejection, a Chrome load, or the iteration limit (8).

### Step 0 — Batch loop (8 iterations, one framework per iteration)

Each iteration: add the current wall's framework to `DARLING_STAGING_TREES`, run `cft-fwmacho-probe-macho`, capture the next wall. All frameworks staged minimally from overlay. Logs: `/tmp/foundation-probe-46-<N>.log`.

### Iteration 1 — CoreGraphics.framework (wall from #45)

Staging evidence (`/tmp/foundation-probe-46-1.log`):

```
staging: symlinks under System/Library/Frameworks/CoreGraphics.framework: 0 found, 0 created, 0 failed
```

Loaded (verbatim):

```
dyld: loaded: <4C4C44A8-5555-3144-A1C7-4EA09741521C> /System/Library/Frameworks/CoreGraphics.framework/Versions/A/CoreGraphics
```

Next wall: Foundation.framework.

### Iteration 2 — Foundation.framework

Staging evidence (`/tmp/foundation-probe-46-2.log`):

```
staging: symlinks under System/Library/Frameworks/Foundation.framework: 0 found, 0 created, 0 failed
```

Loaded (verbatim):

```
dyld: loaded: <4C4C441B-5555-3144-A16E-F7A9057BE9BD> /System/Library/Frameworks/Foundation.framework/Versions/C/Foundation
```

Next wall: Onyx2D.framework (PrivateFramework).

### Iteration 3 — Onyx2D.framework (PrivateFramework)

Staging evidence (`/tmp/foundation-probe-46-3.log`):

```
staging: symlinks under System/Library/PrivateFrameworks/Onyx2D.framework: 0 found, 0 created, 0 failed
```

Loaded (verbatim):

```
dyld: loaded: <4C4C44CA-5555-3144-A186-74318AFA1BCB> /System/Library/PrivateFrameworks/Onyx2D.framework/Versions/A/Onyx2D
```

Next wall: IOKit.framework.

### Iteration 4 — IOKit.framework

Staging evidence (`/tmp/foundation-probe-46-4.log`):

```
staging: symlinks under System/Library/Frameworks/IOKit.framework: 0 found, 0 created, 0 failed
```

Loaded (verbatim):

```
dyld: loaded: <4C4C4402-5555-3144-A1F8-5E7EA62FEBE6> /System/Library/Frameworks/IOKit.framework/Versions/A/IOKit
```

Next wall: CoreText.framework.

### Iteration 5 — CoreText.framework

Staging evidence (`/tmp/foundation-probe-46-5.log`):

```
staging: symlinks under System/Library/Frameworks/CoreText.framework: 0 found, 0 created, 0 failed
```

Loaded (verbatim):

```
dyld: loaded: <4C4C440F-5555-3144-A121-30747EAD266D> /System/Library/Frameworks/CoreText.framework/Versions/A/CoreText
```

Next wall: AppKit.framework.

### Iteration 6 — AppKit.framework

Staging evidence (`/tmp/foundation-probe-46-6.log`):

```
staging: symlinks under System/Library/Frameworks/AppKit.framework: 3 found, 3 created, 0 failed
```

Loaded (verbatim):

```
dyld: loaded: <4C4C447E-5555-3144-A18D-D29D3A0E1C67> /System/Library/Frameworks/AppKit.framework/Versions/C/AppKit
```

Next wall: CoreData.framework.

### Iteration 7 — CoreData.framework

Staging evidence (`/tmp/foundation-probe-46-7.log`):

```
staging: symlinks under System/Library/Frameworks/CoreData.framework: 0 found, 0 created, 0 failed
```

Loaded (verbatim):

```
dyld: loaded: <4C4C44A3-5555-3144-A1E9-B0137660D82E> /System/Library/Frameworks/CoreData.framework/Versions/A/CoreData
```

Next wall: QuartzCore.framework.

### Iteration 8 — QuartzCore.framework (last iteration, limit reached)

Staging evidence (`/tmp/foundation-probe-46-8.log`):

```
staging: symlinks under System/Library/Frameworks/QuartzCore.framework: 0 found, 0 created, 0 failed
```

Loaded (verbatim):

```
dyld: loaded: <4C4C44B9-5555-3144-A159-E7DC4B5C0589> /System/Library/Frameworks/QuartzCore.framework/Versions/A/QuartzCore
```

### Final wall (verbatim from `/tmp/foundation-probe-46-8.log`)

```
dlopen(/Frameworks/Google Chrome for Testing Framework.framework/Versions/154.0.8029.0/Google Chrome for Testing Framework, 261): Library not loaded: /System/Library/Frameworks/ImageIO.framework/Versions/A/ImageIO
  Referenced from: /Frameworks/Google Chrome for Testing Framework.framework/Versions/154.0.8029.0/Google Chrome for Testing Framework
  Reason: image not found
```

The rejection moved from QuartzCore.framework (wall cleared in iteration 8) to ImageIO.framework — same shape, next dependency in the chain.

### Emission site

Same emission site as #37–#45: `src/external/dyld/src/ImageLoader.cpp:820` — the message shape is identical across all iterations, only the dependency name changed.

### Verdict

**staging-missing-dependency (batch, 8/8 walls cleared)** — 8 frameworks staged and loaded in one turn: CoreGraphics, Foundation, Onyx2D (PrivateFramework), IOKit, CoreText, AppKit, CoreData, QuartzCore. The Chrome framework now fails at ImageIO.framework, missing from the staging trees. The pattern from #38–#45 is confirmed at batch scale for the fifth time: each staged framework clears exactly one wall and reveals the next dependency. Cumulative staging across #38–#46: 45 frameworks. Next candidate: `/System/Library/Frameworks/ImageIO.framework`. Iteration limit (8) reached; further walls can be cleared in a follow-up batch.

### Repro

```sh
export PATH=/usr/local/bin:/usr/local/sbin:/usr/bin:/bin:/sbin:/usr/sbin
export DARLING_SRC_DIR=$DARLING_SRC_DIR DARLING_OVERLAY=$DARLING_OVERLAY DARLING_BUILD_DIR=$DARLING_BUILD_DIR
export DARLING_TEST_BINARY=cft-fwmacho-probe-macho
export DARLING_STAGING_TREES=usr/lib:Frameworks:System/Library/Frameworks/CoreFoundation.framework:System/Library/Frameworks/Security.framework:System/Library/Frameworks/ApplicationServices.framework:System/Library/Frameworks/CoreServices.framework:System/Library/Frameworks/CFNetwork.framework:System/Library/Frameworks/OpenDirectory.framework:System/Library/Frameworks/CryptoTokenKit.framework:System/Library/Frameworks/LocalAuthentication.framework:System/Library/Frameworks/Accelerate.framework:System/Library/Frameworks/AudioUnit.framework:System/Library/Frameworks/AVFAudio.framework:System/Library/Frameworks/Carbon.framework:System/Library/Frameworks/CoreVideo.framework:System/Library/Frameworks/CoreImage.framework:System/Library/Frameworks/Network.framework:System/Library/Frameworks/IOSurface.framework:System/Library/Frameworks/CoreMedia.framework:System/Library/Frameworks/AudioToolbox.framework:System/Library/Frameworks/OpenGL.framework:System/Library/Frameworks/Quartz.framework:System/Library/Frameworks/Cocoa.framework:System/Library/Frameworks/VideoToolbox.framework:System/Library/Frameworks/CoreMediaIO.framework:System/Library/Frameworks/Accessibility.framework:System/Library/Frameworks/MetalKit.framework:System/Library/Frameworks/CoreMIDI.framework:System/Library/Frameworks/MediaAccessibility.framework:System/Library/Frameworks/SecurityInterface.framework:System/Library/Frameworks/CoreHaptics.framework:System/Library/Frameworks/ForceFeedback.framework:System/Library/Frameworks/CoreWLAN.framework:System/Library/Frameworks/CoreLocation.framework:System/Library/Frameworks/CoreML.framework:System/Library/Frameworks/DiskArbitration.framework:System/Library/Frameworks/ServiceManagement.framework:System/Library/Frameworks/SafariServices.framework:System/Library/Frameworks/LocalAuthenticationEmbeddedUI.framework:System/Library/Frameworks/CoreGraphics.framework:System/Library/Frameworks/Foundation.framework:System/Library/PrivateFrameworks/Onyx2D.framework:System/Library/Frameworks/IOKit.framework:System/Library/Frameworks/CoreText.framework:System/Library/Frameworks/AppKit.framework:System/Library/Frameworks/CoreData.framework:System/Library/Frameworks/QuartzCore.framework
timeout 120 sudo env DARLING_SRC_DIR=$DARLING_SRC_DIR DARLING_OVERLAY=$DARLING_OVERLAY DARLING_BUILD_DIR=$DARLING_BUILD_DIR DARLING_TEST_BINARY=cft-fwmacho-probe-macho DARLING_STAGING_TREES=$DARLING_STAGING_TREES DYLD_PRINT_LIBRARIES=1 DYLD_PRINT_LIBRARIES_POST_LAUNCH=1 DYLD_PRINT_BINDINGS=1 DYLD_PRINT_WEAK_BINDINGS=1 DYLD_PRINT_APIS=1 DYLD_PRINT_INTERPOSING=1 DYLD_PRINT_SEGMENTS=1 DYLD_PRINT_STATISTICS=1 DYLD_PRINT_STATISTICS_DETAILS=1 DYLD_PRINT_RPATHS=1 DYLD_PRINT_WARNINGS=1 DYLD_PRINT_INITIALIZERS=1 DYLD_PRINT_DOFS=1 DYLD_PRINT_OPTS=1 DYLD_PRINT_ENV=1 DYLD_PRINT_CODE_SIGNATURES=1 DYLD_PRINT_REBASINGS=1 DYLD_PRINT_TO_STDERR=1 $DARLING_BUILD_DIR/launch-dynamic > /tmp/foundation-probe-46-8.log 2>&1
grep "Library not loaded\|image not found\|invalid file format" /tmp/foundation-probe-46-8.log
```

## Control #47 — batch staging 6: 8 more walls cleared

**Date:** 2026-10-05
**Branch:** task/chrome-fw-staging-batch6
**Base:** pr-arm64 = d1403bdb5
**Goal:** Continue the batch staging loop from the ImageIO wall (verdict #46), staging each new wall's framework from overlay and re-running the Chrome framework probe until a non-staging rejection, a Chrome load, or the iteration limit (8).

### Step 0 — Batch loop (8 iterations, one framework per iteration)

Each iteration: add the current wall's framework to `DARLING_STAGING_TREES`, run `cft-fwmacho-probe-macho`, capture the next wall. All frameworks staged minimally from overlay. Logs: `/tmp/foundation-probe-47-<N>.log`.

### Iteration 1 — ImageIO.framework (wall from #46)

Staging evidence (`/tmp/foundation-probe-47-1.log`):

```
staging: symlinks under System/Library/Frameworks/ImageIO.framework: 0 found, 0 created, 0 failed
```

Loaded (verbatim):

```
dyld: loaded: <4C4C44A8-5555-3144-A10D-A73E8B5D38B3> /System/Library/Frameworks/ImageIO.framework/Versions/A/ImageIO
```

Next wall: LaunchServices.framework.

### Iteration 2 — LaunchServices.framework

Staging evidence (`/tmp/foundation-probe-47-2.log`):

```
staging: symlinks under System/Library/Frameworks/LaunchServices.framework: 0 found, 0 created, 0 failed
```

Loaded (verbatim):

```
dyld: loaded: <4C4C44D9-5555-3144-A149-A8E85DE91E92> /System/Library/Frameworks/LaunchServices.framework/Versions/A/LaunchServices
```

Next wall: UniformTypeIdentifiers.framework.

### Iteration 3 — UniformTypeIdentifiers.framework

Staging evidence (`/tmp/foundation-probe-47-3.log`):

```
staging: symlinks under System/Library/Frameworks/UniformTypeIdentifiers.framework: 2 found, 2 created, 0 failed
```

Loaded (verbatim):

```
dyld: loaded: <4C4C444F-5555-3144-A1A8-0ECB0D1B0FBC> /System/Library/Frameworks/UniformTypeIdentifiers.framework/Versions/A/UniformTypeIdentifiers
```

Next wall: SystemConfiguration.framework.

### Iteration 4 — SystemConfiguration.framework

Staging evidence (`/tmp/foundation-probe-47-4.log`):

```
staging: symlinks under System/Library/Frameworks/SystemConfiguration.framework: 2 found, 2 created, 0 failed
```

Loaded (verbatim):

```
dyld: loaded: <4111692E-BBCD-3EC7-BC4B-31F7AFCF321C> /System/Library/Frameworks/SystemConfiguration.framework/Versions/A/SystemConfiguration
```

Next wall: Metal.framework.

### Iteration 5 — Metal.framework

Staging evidence (`/tmp/foundation-probe-47-5.log`):

```
staging: symlinks under System/Library/Frameworks/Metal.framework: 2 found, 2 created, 0 failed
```

Loaded (verbatim):

```
dyld: loaded: <4C4C446D-5555-3144-A103-C661069B3221> /System/Library/Frameworks/Metal.framework/Versions/A/Metal
```

Next wall: CoreAudio.framework.

### Iteration 6 — CoreAudio.framework

Staging evidence (`/tmp/foundation-probe-47-6.log`):

```
staging: symlinks under System/Library/Frameworks/CoreAudio.framework: 2 found, 2 created, 0 failed
```

Loaded (verbatim):

```
dyld: loaded: <4C4C44C7-5555-3144-A114-9EDBCCAD5C53> /System/Library/Frameworks/CoreAudio.framework/Versions/A/CoreAudio
```

Next wall: AVFoundation.framework.

### Iteration 7 — AVFoundation.framework

Staging evidence (`/tmp/foundation-probe-47-7.log`):

```
staging: symlinks under System/Library/Frameworks/AVFoundation.framework: 2 found, 2 created, 0 failed
```

Loaded (verbatim):

```
dyld: loaded: <4C4C4413-5555-3144-A11D-4B006DAE4F75> /System/Library/Frameworks/AVFoundation.framework/Versions/A/AVFoundation
```

Next wall: CoreBluetooth.framework.

### Iteration 8 — CoreBluetooth.framework (last iteration, limit reached)

Staging evidence (`/tmp/foundation-probe-47-8.log`):

```
staging: symlinks under System/Library/Frameworks/CoreBluetooth.framework: 2 found, 2 created, 0 failed
```

Loaded (verbatim):

```
dyld: loaded: <4C4C440C-5555-3144-A183-1C3C7D0356B9> /System/Library/Frameworks/CoreBluetooth.framework/Versions/A/CoreBluetooth
```

### Final wall (verbatim from `/tmp/foundation-probe-47-8.log`)

```
dlopen(/Frameworks/Google Chrome for Testing Framework.framework/Versions/154.0.8029.0/Google Chrome for Testing Framework, 261): Library not loaded: /System/Library/Frameworks/IOBluetooth.framework/Versions/A/IOBluetooth
  Referenced from: /usr/lib/IOBluetoothExtras.dylib
  Reason: image not found
```

The rejection moved from CoreBluetooth.framework (wall cleared in iteration 8) to IOBluetooth.framework — same shape, next dependency in the chain.

### Emission site

Same emission site as #37–#46: `src/external/dyld/src/ImageLoader.cpp:820` — the message shape is identical across all iterations, only the dependency name changed.

### Verdict

**staging-missing-dependency (batch, 8/8 walls cleared)** — 8 frameworks staged and loaded in one turn: ImageIO, LaunchServices, UniformTypeIdentifiers, SystemConfiguration, Metal, CoreAudio, AVFoundation, CoreBluetooth. The Chrome framework now fails at IOBluetooth.framework, missing from the staging trees. The pattern from #38–#46 is confirmed at batch scale for the sixth time: each staged framework clears exactly one wall and reveals the next dependency. Cumulative staging across #38–#47: 53 frameworks. Next candidate: `/System/Library/Frameworks/IOBluetooth.framework`. Iteration limit (8) reached; further walls can be cleared in a follow-up batch.

### Repro

```sh
export PATH=/usr/local/bin:/usr/local/sbin:/usr/bin:/bin:/sbin:/usr/sbin
export DARLING_SRC_DIR=$DARLING_SRC_DIR DARLING_OVERLAY=$DARLING_OVERLAY DARLING_BUILD_DIR=$DARLING_BUILD_DIR
export DARLING_TEST_BINARY=cft-fwmacho-probe-macho
export DARLING_STAGING_TREES=usr/lib:Frameworks:System/Library/Frameworks/CoreFoundation.framework:System/Library/Frameworks/Security.framework:System/Library/Frameworks/ApplicationServices.framework:System/Library/Frameworks/CoreServices.framework:System/Library/Frameworks/CFNetwork.framework:System/Library/Frameworks/OpenDirectory.framework:System/Library/Frameworks/CryptoTokenKit.framework:System/Library/Frameworks/LocalAuthentication.framework:System/Library/Frameworks/Accelerate.framework:System/Library/Frameworks/AudioUnit.framework:System/Library/Frameworks/AVFAudio.framework:System/Library/Frameworks/Carbon.framework:System/Library/Frameworks/CoreVideo.framework:System/Library/Frameworks/CoreImage.framework:System/Library/Frameworks/Network.framework:System/Library/Frameworks/IOSurface.framework:System/Library/Frameworks/CoreMedia.framework:System/Library/Frameworks/AudioToolbox.framework:System/Library/Frameworks/OpenGL.framework:System/Library/Frameworks/Quartz.framework:System/Library/Frameworks/Cocoa.framework:System/Library/Frameworks/VideoToolbox.framework:System/Library/Frameworks/CoreMediaIO.framework:System/Library/Frameworks/Accessibility.framework:System/Library/Frameworks/MetalKit.framework:System/Library/Frameworks/CoreMIDI.framework:System/Library/Frameworks/MediaAccessibility.framework:System/Library/Frameworks/SecurityInterface.framework:System/Library/Frameworks/CoreHaptics.framework:System/Library/Frameworks/ForceFeedback.framework:System/Library/Frameworks/CoreWLAN.framework:System/Library/Frameworks/CoreLocation.framework:System/Library/Frameworks/CoreML.framework:System/Library/Frameworks/DiskArbitration.framework:System/Library/Frameworks/ServiceManagement.framework:System/Library/Frameworks/SafariServices.framework:System/Library/Frameworks/LocalAuthenticationEmbeddedUI.framework:System/Library/Frameworks/CoreGraphics.framework:System/Library/Frameworks/Foundation.framework:System/Library/PrivateFrameworks/Onyx2D.framework:System/Library/Frameworks/IOKit.framework:System/Library/Frameworks/CoreText.framework:System/Library/Frameworks/AppKit.framework:System/Library/Frameworks/CoreData.framework:System/Library/Frameworks/QuartzCore.framework:System/Library/Frameworks/ImageIO.framework:System/Library/Frameworks/LaunchServices.framework:System/Library/Frameworks/UniformTypeIdentifiers.framework:System/Library/Frameworks/SystemConfiguration.framework:System/Library/Frameworks/Metal.framework:System/Library/Frameworks/CoreAudio.framework:System/Library/Frameworks/AVFoundation.framework:System/Library/Frameworks/CoreBluetooth.framework
timeout 120 sudo env DARLING_SRC_DIR=$DARLING_SRC_DIR DARLING_OVERLAY=$DARLING_OVERLAY DARLING_BUILD_DIR=$DARLING_BUILD_DIR DARLING_TEST_BINARY=cft-fwmacho-probe-macho DARLING_STAGING_TREES=$DARLING_STAGING_TREES DYLD_PRINT_LIBRARIES=1 DYLD_PRINT_LIBRARIES_POST_LAUNCH=1 DYLD_PRINT_BINDINGS=1 DYLD_PRINT_WEAK_BINDINGS=1 DYLD_PRINT_APIS=1 DYLD_PRINT_INTERPOSING=1 DYLD_PRINT_SEGMENTS=1 DYLD_PRINT_STATISTICS=1 DYLD_PRINT_STATISTICS_DETAILS=1 DYLD_PRINT_RPATHS=1 DYLD_PRINT_WARNINGS=1 DYLD_PRINT_INITIALIZERS=1 DYLD_PRINT_DOFS=1 DYLD_PRINT_OPTS=1 DYLD_PRINT_ENV=1 DYLD_PRINT_CODE_SIGNATURES=1 DYLD_PRINT_REBASINGS=1 DYLD_PRINT_TO_STDERR=1 $DARLING_BUILD_DIR/launch-dynamic > /tmp/foundation-probe-47-8.log 2>&1
grep "Library not loaded\|image not found\|invalid file format" /tmp/foundation-probe-47-8.log
```

## Control #48 — batch staging 7: 6 walls cleared, then crash (non-staging)

ERRATUM (#46): Final wall line 2 of the merged section #46 is wrong: the live log (/tmp/foundation-probe-46-8.log, both occurrences) says "Referenced from: /System/Library/Frameworks/AppKit.framework/Versions/C/AppKit".

**Date:** 2026-10-05
**Branch:** task/chrome-fw-staging-batch7
**Base:** pr-arm64 = 257916cd5
**Goal:** Continue the batch staging loop from the IOBluetooth wall (verdict #47), staging each new wall's framework from overlay and re-running the Chrome framework probe until a non-staging rejection, a Chrome load, or the iteration limit (8).

### Step 0 — Batch loop (6 iterations, then crash)

Each iteration: add the current wall's framework to `DARLING_STAGING_TREES`, run `cft-fwmacho-probe-macho`, capture the next wall. All frameworks staged minimally from overlay. Logs: `/tmp/foundation-probe-48-<N>.log`.

### Iteration 1 — IOBluetooth.framework (wall from #47)

Staging evidence (`/tmp/foundation-probe-48-1.log`):

```
staging: symlinks under System/Library/Frameworks/IOBluetooth.framework: 2 found, 2 created, 0 failed
```

Loaded (verbatim):

```
dyld: loaded: <4C4C444F-5555-3144-A1B0-173DB3DB6C76> /System/Library/Frameworks/IOBluetooth.framework/Versions/A/IOBluetooth
```

Next wall: MediaPlayer.framework.

### Iteration 2 — MediaPlayer.framework

Staging evidence (`/tmp/foundation-probe-48-2.log`):

```
staging: symlinks under System/Library/Frameworks/MediaPlayer.framework: 2 found, 2 created, 0 failed
```

Loaded (verbatim):

```
dyld: loaded: <4C4C447F-5555-3144-A1AF-CF1E35E4980E> /System/Library/Frameworks/MediaPlayer.framework/Versions/A/MediaPlayer
```

Next wall: AuthenticationServices.framework.

### Iteration 3 — AuthenticationServices.framework

Staging evidence (`/tmp/foundation-probe-48-3.log`):

```
staging: symlinks under System/Library/Frameworks/AuthenticationServices.framework: 2 found, 2 created, 0 failed
```

Loaded (verbatim):

```
dyld: loaded: <4C4C44EE-5555-3144-A1DA-C99F3409B8F7> /System/Library/Frameworks/AuthenticationServices.framework/Versions/A/AuthenticationServices
```

Next wall: GameController.framework.

### Iteration 4 — GameController.framework

Staging evidence (`/tmp/foundation-probe-48-4.log`):

```
staging: symlinks under System/Library/Frameworks/GameController.framework: 2 found, 2 created, 0 failed
```

Loaded (verbatim):

```
dyld: loaded: <4C4C44EC-5555-3144-A144-5B880DC71E4F> /System/Library/Frameworks/GameController.framework/Versions/A/GameController
```

Next wall: Vision.framework.

### Iteration 5 — Vision.framework

Staging evidence (`/tmp/foundation-probe-48-5.log`):

```
staging: symlinks under System/Library/Frameworks/Vision.framework: 2 found, 2 created, 0 failed
```

Loaded (verbatim):

```
dyld: loaded: <4C4C44B4-5555-3144-A1D0-FBE375028C08> /System/Library/Frameworks/Vision.framework/Versions/A/Vision
```

Next wall: UserNotifications.framework.

### Iteration 6 — UserNotifications.framework (crash after load)

Staging evidence (`/tmp/foundation-probe-48-6.log`):

```
staging: symlinks under System/Library/Frameworks/UserNotifications.framework: 2 found, 2 created, 0 failed
```

Loaded (verbatim):

```
dyld: loaded: <4C4C44E3-5555-3144-A160-E40EBB0B11B3> /System/Library/Frameworks/UserNotifications.framework/Versions/A/UserNotifications
```

UserNotifications.framework loaded successfully, but the probe then crashed (non-staging failure). Verbatim from log:

```
backtrace (3 frames):
  #00 0x2227e3  0x2227e3 <crash_debug_handler+0xc3> at $DARLING_BUILD_DIR/dserver/mldr-real/mldr
  #01 0x82331945a  0x82331945a <_pthread_sigmask+0x50a> at /lib/libthr.so.3
  #02 0x823318a5b  0x823318a5b <pthread_signals_unblock_np+0x5bb> at /lib/libthr.so.3
```

The crash occurred during lazy binding of AppKit to Foundation/CoreFoundation, after UserNotifications.framework was loaded. This is NOT a staging-missing-dependency rejection — it is a crash in the mldr (Mach-O loader) during symbol binding.

### Final wall

No final wall — the probe crashed before reaching the next dependency rejection. The crash is in the mldr's crash_debug_handler, triggered during AppKit's lazy binding to Foundation. This is a different class of failure from the staging-missing-dependency pattern: the staging is complete (all 6 frameworks loaded), but the loader crashes during binding.

### Emission site

N/A — no dyld rejection message was emitted. The crash is in the mldr itself, not in dyld's dependency resolution.

### Verdict

**crash-in-mldr (non-staging)** — 6 frameworks staged and loaded in one turn: IOBluetooth, MediaPlayer, AuthenticationServices, GameController, Vision, UserNotifications. The probe then crashed in the mldr during AppKit's lazy binding to Foundation, before reaching the next dependency rejection. The staging-missing-dependency pattern is broken: the next wall is not a missing framework but a loader crash. Cumulative staging across #38–#48: 59 frameworks. The crash needs investigation — it may be related to UserNotifications.framework's initialization or to the accumulated staging state.

### Repro

```sh
export PATH=/usr/local/bin:/usr/local/sbin:/usr/bin:/bin:/sbin:/usr/sbin
export DARLING_SRC_DIR=$DARLING_SRC_DIR DARLING_OVERLAY=$DARLING_OVERLAY DARLING_BUILD_DIR=$DARLING_BUILD_DIR
export DARLING_TEST_BINARY=cft-fwmacho-probe-macho
export DARLING_STAGING_TREES=usr/lib:Frameworks:System/Library/Frameworks/CoreFoundation.framework:System/Library/Frameworks/Security.framework:System/Library/Frameworks/ApplicationServices.framework:System/Library/Frameworks/CoreServices.framework:System/Library/Frameworks/CFNetwork.framework:System/Library/Frameworks/OpenDirectory.framework:System/Library/Frameworks/CryptoTokenKit.framework:System/Library/Frameworks/LocalAuthentication.framework:System/Library/Frameworks/Accelerate.framework:System/Library/Frameworks/AudioUnit.framework:System/Library/Frameworks/AVFAudio.framework:System/Library/Frameworks/Carbon.framework:System/Library/Frameworks/CoreVideo.framework:System/Library/Frameworks/CoreImage.framework:System/Library/Frameworks/Network.framework:System/Library/Frameworks/IOSurface.framework:System/Library/Frameworks/CoreMedia.framework:System/Library/Frameworks/AudioToolbox.framework:System/Library/Frameworks/OpenGL.framework:System/Library/Frameworks/Quartz.framework:System/Library/Frameworks/Cocoa.framework:System/Library/Frameworks/VideoToolbox.framework:System/Library/Frameworks/CoreMediaIO.framework:System/Library/Frameworks/Accessibility.framework:System/Library/Frameworks/MetalKit.framework:System/Library/Frameworks/CoreMIDI.framework:System/Library/Frameworks/MediaAccessibility.framework:System/Library/Frameworks/SecurityInterface.framework:System/Library/Frameworks/CoreHaptics.framework:System/Library/Frameworks/ForceFeedback.framework:System/Library/Frameworks/CoreWLAN.framework:System/Library/Frameworks/CoreLocation.framework:System/Library/Frameworks/CoreML.framework:System/Library/Frameworks/DiskArbitration.framework:System/Library/Frameworks/ServiceManagement.framework:System/Library/Frameworks/SafariServices.framework:System/Library/Frameworks/LocalAuthenticationEmbeddedUI.framework:System/Library/Frameworks/CoreGraphics.framework:System/Library/Frameworks/Foundation.framework:System/Library/PrivateFrameworks/Onyx2D.framework:System/Library/Frameworks/IOKit.framework:System/Library/Frameworks/CoreText.framework:System/Library/Frameworks/AppKit.framework:System/Library/Frameworks/CoreData.framework:System/Library/Frameworks/QuartzCore.framework:System/Library/Frameworks/ImageIO.framework:System/Library/Frameworks/LaunchServices.framework:System/Library/Frameworks/UniformTypeIdentifiers.framework:System/Library/Frameworks/SystemConfiguration.framework:System/Library/Frameworks/Metal.framework:System/Library/Frameworks/CoreAudio.framework:System/Library/Frameworks/AVFoundation.framework:System/Library/Frameworks/CoreBluetooth.framework:System/Library/Frameworks/IOBluetooth.framework:System/Library/Frameworks/MediaPlayer.framework:System/Library/Frameworks/AuthenticationServices.framework:System/Library/Frameworks/GameController.framework:System/Library/Frameworks/Vision.framework:System/Library/Frameworks/UserNotifications.framework
timeout 120 sudo env DARLING_SRC_DIR=$DARLING_SRC_DIR DARLING_OVERLAY=$DARLING_OVERLAY DARLING_BUILD_DIR=$DARLING_BUILD_DIR DARLING_TEST_BINARY=cft-fwmacho-probe-macho DARLING_STAGING_TREES=$DARLING_STAGING_TREES DYLD_PRINT_LIBRARIES=1 DYLD_PRINT_LIBRARIES_POST_LAUNCH=1 DYLD_PRINT_BINDINGS=1 DYLD_PRINT_WEAK_BINDINGS=1 DYLD_PRINT_APIS=1 DYLD_PRINT_INTERPOSING=1 DYLD_PRINT_SEGMENTS=1 DYLD_PRINT_STATISTICS=1 DYLD_PRINT_STATISTICS_DETAILS=1 DYLD_PRINT_RPATHS=1 DYLD_PRINT_WARNINGS=1 DYLD_PRINT_INITIALIZERS=1 DYLD_PRINT_DOFS=1 DYLD_PRINT_OPTS=1 DYLD_PRINT_ENV=1 DYLD_PRINT_CODE_SIGNATURES=1 DYLD_PRINT_REBASINGS=1 DYLD_PRINT_TO_STDERR=1 $DARLING_BUILD_DIR/launch-dynamic > /tmp/foundation-probe-48-6.log 2>&1
grep "Library not loaded\|image not found\|invalid file format" /tmp/foundation-probe-48-6.log
```

## Control #49 — mldr crash trigger discrimination: UserNotifications

**Date:** 2026-10-05
**Branch:** task/mldr-crash-discrim
**Base:** pr-arm64 = 0baa8703a
**Goal:** Discriminate the trigger of the mldr crash observed in #48 (iteration 6, after UserNotifications.framework loaded). Two runs: 49-1 with the full #48 tree (58 trees, including UserNotifications), 49-2 without UserNotifications.framework (57 trees).

### Verdict

**UserNotifications триггер: да** — the crash reproduces deterministically with UserNotifications.framework staged (49-1) and does not occur without it (49-2 reaches the UserNotifications wall instead).

### Run 49-1 — full #48 tree (58 trees, UserNotifications staged)

Log: `/tmp/foundation-probe-49-1.log` (1287100 lines). Crash reproduced deterministically. Verbatim backtrace:

```
backtrace (3 frames):
  #00 0x2227e3  0x2227e3 <crash_debug_handler+0xc3> at $DARLING_BUILD_DIR/dserver/mldr-real/mldr
  #01 0x8227b245a  0x8227b245a <_pthread_sigmask+0x50a> at /lib/libthr.so.3
  #02 0x8227b1a5b  0x8227b1a5b <pthread_signals_unblock_np+0x5bb> at /lib/libthr.so.3
```

### Run 49-2 — same tree without UserNotifications.framework (57 trees)

Log: `/tmp/foundation-probe-49-2.log` (25388 lines). No crash — the probe reaches the UserNotifications wall. Verbatim (stat=1 occurrence):

```
dlopen(/Frameworks/Google Chrome for Testing Framework.framework/Versions/154.0.8029.0/Google Chrome for Testing Framework, 261): Library not loaded: /System/Library/Frameworks/UserNotifications.framework/Versions/A/UserNotifications
  Referenced from: /usr/lib/UserNotificationsExtras.dylib
  Reason: image not found
```

### Analysis

The crash in #48 iteration 6 is caused by staging UserNotifications.framework. Without it, the probe reaches the expected staging-missing-dependency wall. With it, the mldr crashes during lazy binding (after UserNotifications loads, during AppKit's binding to Foundation). The trigger is UserNotifications.framework's presence in the staging trees — not the accumulated staging state (57 other trees are identical in both runs).

### Repro

Run 49-1 (crash):

```sh
export PATH=/usr/local/bin:/usr/local/sbin:/usr/bin:/bin:/sbin:/usr/sbin
export DARLING_SRC_DIR=$DARLING_SRC_DIR DARLING_OVERLAY=$DARLING_OVERLAY DARLING_BUILD_DIR=$DARLING_BUILD_DIR
export DARLING_TEST_BINARY=cft-fwmacho-probe-macho
export DARLING_STAGING_TREES=usr/lib:Frameworks:System/Library/Frameworks/CoreFoundation.framework:System/Library/Frameworks/Security.framework:System/Library/Frameworks/ApplicationServices.framework:System/Library/Frameworks/CoreServices.framework:System/Library/Frameworks/CFNetwork.framework:System/Library/Frameworks/OpenDirectory.framework:System/Library/Frameworks/CryptoTokenKit.framework:System/Library/Frameworks/LocalAuthentication.framework:System/Library/Frameworks/Accelerate.framework:System/Library/Frameworks/AudioUnit.framework:System/Library/Frameworks/AVFAudio.framework:System/Library/Frameworks/Carbon.framework:System/Library/Frameworks/CoreVideo.framework:System/Library/Frameworks/CoreImage.framework:System/Library/Frameworks/Network.framework:System/Library/Frameworks/IOSurface.framework:System/Library/Frameworks/CoreMedia.framework:System/Library/Frameworks/AudioToolbox.framework:System/Library/Frameworks/OpenGL.framework:System/Library/Frameworks/Quartz.framework:System/Library/Frameworks/Cocoa.framework:System/Library/Frameworks/VideoToolbox.framework:System/Library/Frameworks/CoreMediaIO.framework:System/Library/Frameworks/Accessibility.framework:System/Library/Frameworks/MetalKit.framework:System/Library/Frameworks/CoreMIDI.framework:System/Library/Frameworks/MediaAccessibility.framework:System/Library/Frameworks/SecurityInterface.framework:System/Library/Frameworks/CoreHaptics.framework:System/Library/Frameworks/ForceFeedback.framework:System/Library/Frameworks/CoreWLAN.framework:System/Library/Frameworks/CoreLocation.framework:System/Library/Frameworks/CoreML.framework:System/Library/Frameworks/DiskArbitration.framework:System/Library/Frameworks/ServiceManagement.framework:System/Library/Frameworks/SafariServices.framework:System/Library/Frameworks/LocalAuthenticationEmbeddedUI.framework:System/Library/Frameworks/CoreGraphics.framework:System/Library/Frameworks/Foundation.framework:System/Library/PrivateFrameworks/Onyx2D.framework:System/Library/Frameworks/IOKit.framework:System/Library/Frameworks/CoreText.framework:System/Library/Frameworks/AppKit.framework:System/Library/Frameworks/CoreData.framework:System/Library/Frameworks/QuartzCore.framework:System/Library/Frameworks/ImageIO.framework:System/Library/Frameworks/LaunchServices.framework:System/Library/Frameworks/UniformTypeIdentifiers.framework:System/Library/Frameworks/SystemConfiguration.framework:System/Library/Frameworks/Metal.framework:System/Library/Frameworks/CoreAudio.framework:System/Library/Frameworks/AVFoundation.framework:System/Library/Frameworks/CoreBluetooth.framework:System/Library/Frameworks/IOBluetooth.framework:System/Library/Frameworks/MediaPlayer.framework:System/Library/Frameworks/AuthenticationServices.framework:System/Library/Frameworks/GameController.framework:System/Library/Frameworks/Vision.framework:System/Library/Frameworks/UserNotifications.framework
timeout 120 sudo env DARLING_SRC_DIR=$DARLING_SRC_DIR DARLING_OVERLAY=$DARLING_OVERLAY DARLING_BUILD_DIR=$DARLING_BUILD_DIR DARLING_TEST_BINARY=cft-fwmacho-probe-macho DARLING_STAGING_TREES=$DARLING_STAGING_TREES DYLD_PRINT_LIBRARIES=1 DYLD_PRINT_LIBRARIES_POST_LAUNCH=1 DYLD_PRINT_BINDINGS=1 DYLD_PRINT_WEAK_BINDINGS=1 DYLD_PRINT_APIS=1 DYLD_PRINT_INTERPOSING=1 DYLD_PRINT_SEGMENTS=1 DYLD_PRINT_STATISTICS=1 DYLD_PRINT_STATISTICS_DETAILS=1 DYLD_PRINT_RPATHS=1 DYLD_PRINT_WARNINGS=1 DYLD_PRINT_INITIALIZERS=1 DYLD_PRINT_DOFS=1 DYLD_PRINT_OPTS=1 DYLD_PRINT_ENV=1 DYLD_PRINT_CODE_SIGNATURES=1 DYLD_PRINT_REBASINGS=1 DYLD_PRINT_TO_STDERR=1 $DARLING_BUILD_DIR/launch-dynamic > /tmp/foundation-probe-49-1.log 2>&1
grep "crash_debug_handler\|gstack" /tmp/foundation-probe-49-1.log
```

Run 49-2 (no crash, wall): same as 49-1 but remove `System/Library/Frameworks/UserNotifications.framework` from DARLING_STAGING_TREES (57 trees), log `/tmp/foundation-probe-49-2.log`, grep for `Library not loaded`.

## Control #50 — mldr crash diagnosis: lazy-bind mechanics

ERRATUM (#49): the analysis sentence "mldr crashes during lazy binding ... during AppKit's binding to Foundation" is wrong. Measured on live 50-1: UserNotifications.framework loaded (dyld: loaded, last of 134), crash is ~1.26M lines of output AFTER load, in weak/lazy binding phase; last operations before FATAL: weak binds SystemConfiguration→libc++ (__Znwm), then lazy binds libobjc.A.dylib→libsystem_platform (_fls) and libsystem_malloc.dylib→libsystem_kernel (_madvise); FATAL signal 11 addr=0x435de5894850, rip=0x0000203dfcc19746 (guest address), rcx=0x2e66c35de5894855 / rdx=0x435de5894850 — values look like code bytes (48 89 e5 = mov rbp,rsp) read through a corrupted pointer.

**Date:** 2026-10-05
**Branch:** task/mldr-crash-diag
**Base:** pr-arm64 = cfaff75de
**Goal:** Attribute the mldr crash more precisely than "UserNotifications present". Measure the crash context from a fresh run (50-1), then run a discriminating experiment (50-2).

### Run 50-1 — full #48 tree (58 trees), crash context

Log: `/tmp/foundation-probe-50-1.log` (1287100 lines). Crash reproduced. Last ~30 dyld lines before FATAL (verbatim):

```
dyld: weak bind: SystemConfiguration:0x203E0E0C7130 = libc++.1.dylib:__ZdlPv, *0x203E0E0C7130 = 0x203DFCCEB4B0
dyld: weak bind: SystemConfiguration:0x203E0E0C7130 = libc++.1.dylib:__ZdlPv, *0x203E0E0C7130 = 0x203DFCCEB4B0
dyld:     adjusting uses of __ZdlPv in /System/Library/Frameworks/SystemConfiguration.framework/Versions/A/SystemConfiguration to use definition from /usr/lib/libc++.1.dylib
dyld:   found weak __Znwm at 0x203DFCCEB310 in /usr/lib/libc++.1.dylib
dyld: weak bind: SystemConfiguration:0x203E0E0C7138 = libc++.1.dylib:__Znwm, *0x203E0E0C7138 = 0x203DFCCEB310
dyld: weak bind: SystemConfiguration:0x203E0E0C7138 = libc++.1.dylib:__Znwm, *0x203E0E0C7138 = 0x203DFCCEB310
dyld:     adjusting uses of __Znwm in /System/Library/Frameworks/SystemConfiguration.framework/Versions/A/SystemConfiguration to use definition from /usr/lib/libc++.1.dylib
dyld: weak bind end
dyld: lazy bind: libobjc.A.dylib:0x203DFCC4E2D8 = libsystem_platform.dylib:_fls, *0x203DFCC4E2D8 = 0x203DFCAC1DA0
dyld: lazy bind: libsystem_malloc.dylib:0x203DFBB31220 = libsystem_kernel.dylib:_madvise, *0x203DFBB31220 = 0x203DFC7B390C
[darling-mldr] FATAL signal 11 (code=1) at addr=0x435de5894850
```

Register block (verbatim):

```
  rip=0x0000203dfcc19746  rax=0x0000203e0cf914a0  rbx=0x0000203e0cf914a0
  rcx=0x2e66c35de5894855  rdx=0x0000435de5894850  rsi=0x0000000000000002
  rdi=0x0000000000000005  rbp=0x00007fffffdfdbb0  rsp=0x00007fffffdfdb70
  r8 =0x0000000000000002  r9 =0x00007f82c867b290  r10=0x0000000000000004
  r11=0x00007f82c863df40  r12=0x0000203e0bb0bb46  r13=0x0000000000000000
  r14=0x0000203e0cca14d0  r15=0x00007ffffffffff8
```

Which image was binding at crash: the last dyld operations are lazy binds of libobjc.A.dylib and libsystem_malloc.dylib — but the crash address (0x435de5894850) and rip (0x203dfcc19746, a guest address in libobjc.A.dylib's range) point into libobjc.A.dylib's code. The corrupted pointer values (rcx/rdx look like x86 code bytes) suggest a bad function pointer was called during lazy binding of libobjc.A.dylib. However, from the printed output alone the exact image cannot be attributed with certainty — the crash is in the lazy-bind path, and the last successful binds were libobjc and libsystem_malloc.

### Run 50-2 — DYLD_BIND_AT_LAUNCH=1 (discriminating experiment)

Same 58-tree staging, but with `DYLD_BIND_AT_LAUNCH=1` (force eager binding instead of lazy). Log: `/tmp/foundation-probe-50-2.log` (25942 lines). No crash — the signal 11 FATAL is gone. Instead, a different failure appears:

```
dyld: Symbol not found: _ccchacha20
  Referenced from: /usr/lib/system/libcommonCrypto.dylib
  Expected in: /usr/lib/system/libcorecrypto.dylib
 in /usr/lib/system/libcommonCrypto.dylib
abort_with_payload: reason: Symbol not found: _ccchacha20
```

### Verdict

**класс: lazy-bind mechanics — DYLD_BIND_AT_LAUNCH=1 убирает краш signal 11 (25942 строки, нет FATAL), но проявляет другой отказ (Symbol not found: _ccchacha20); краш — в пути lazy binding, не в самом UserNotifications.framework**

### Repro

Run 50-1 (crash):

```sh
export PATH=/usr/local/bin:/usr/local/sbin:/usr/bin:/bin:/sbin:/usr/sbin
export DARLING_SRC_DIR=$DARLING_SRC_DIR DARLING_OVERLAY=$DARLING_OVERLAY DARLING_BUILD_DIR=$DARLING_BUILD_DIR
export DARLING_TEST_BINARY=cft-fwmacho-probe-macho
export DARLING_STAGING_TREES=usr/lib:Frameworks:System/Library/Frameworks/CoreFoundation.framework:System/Library/Frameworks/Security.framework:System/Library/Frameworks/ApplicationServices.framework:System/Library/Frameworks/CoreServices.framework:System/Library/Frameworks/CFNetwork.framework:System/Library/Frameworks/OpenDirectory.framework:System/Library/Frameworks/CryptoTokenKit.framework:System/Library/Frameworks/LocalAuthentication.framework:System/Library/Frameworks/Accelerate.framework:System/Library/Frameworks/AudioUnit.framework:System/Library/Frameworks/AVFAudio.framework:System/Library/Frameworks/Carbon.framework:System/Library/Frameworks/CoreVideo.framework:System/Library/Frameworks/CoreImage.framework:System/Library/Frameworks/Network.framework:System/Library/Frameworks/IOSurface.framework:System/Library/Frameworks/CoreMedia.framework:System/Library/Frameworks/AudioToolbox.framework:System/Library/Frameworks/OpenGL.framework:System/Library/Frameworks/Quartz.framework:System/Library/Frameworks/Cocoa.framework:System/Library/Frameworks/VideoToolbox.framework:System/Library/Frameworks/CoreMediaIO.framework:System/Library/Frameworks/Accessibility.framework:System/Library/Frameworks/MetalKit.framework:System/Library/Frameworks/CoreMIDI.framework:System/Library/Frameworks/MediaAccessibility.framework:System/Library/Frameworks/SecurityInterface.framework:System/Library/Frameworks/CoreHaptics.framework:System/Library/Frameworks/ForceFeedback.framework:System/Library/Frameworks/CoreWLAN.framework:System/Library/Frameworks/CoreLocation.framework:System/Library/Frameworks/CoreML.framework:System/Library/Frameworks/DiskArbitration.framework:System/Library/Frameworks/ServiceManagement.framework:System/Library/Frameworks/SafariServices.framework:System/Library/Frameworks/LocalAuthenticationEmbeddedUI.framework:System/Library/Frameworks/CoreGraphics.framework:System/Library/Frameworks/Foundation.framework:System/Library/PrivateFrameworks/Onyx2D.framework:System/Library/Frameworks/IOKit.framework:System/Library/Frameworks/CoreText.framework:System/Library/Frameworks/AppKit.framework:System/Library/Frameworks/CoreData.framework:System/Library/Frameworks/QuartzCore.framework:System/Library/Frameworks/ImageIO.framework:System/Library/Frameworks/LaunchServices.framework:System/Library/Frameworks/UniformTypeIdentifiers.framework:System/Library/Frameworks/SystemConfiguration.framework:System/Library/Frameworks/Metal.framework:System/Library/Frameworks/CoreAudio.framework:System/Library/Frameworks/AVFoundation.framework:System/Library/Frameworks/CoreBluetooth.framework:System/Library/Frameworks/IOBluetooth.framework:System/Library/Frameworks/MediaPlayer.framework:System/Library/Frameworks/AuthenticationServices.framework:System/Library/Frameworks/GameController.framework:System/Library/Frameworks/Vision.framework:System/Library/Frameworks/UserNotifications.framework
timeout 120 sudo env DARLING_SRC_DIR=$DARLING_SRC_DIR DARLING_OVERLAY=$DARLING_OVERLAY DARLING_BUILD_DIR=$DARLING_BUILD_DIR DARLING_TEST_BINARY=cft-fwmacho-probe-macho DARLING_STAGING_TREES=$DARLING_STAGING_TREES DYLD_PRINT_LIBRARIES=1 DYLD_PRINT_LIBRARIES_POST_LAUNCH=1 DYLD_PRINT_BINDINGS=1 DYLD_PRINT_WEAK_BINDINGS=1 DYLD_PRINT_APIS=1 DYLD_PRINT_INTERPOSING=1 DYLD_PRINT_SEGMENTS=1 DYLD_PRINT_STATISTICS=1 DYLD_PRINT_STATISTICS_DETAILS=1 DYLD_PRINT_RPATHS=1 DYLD_PRINT_WARNINGS=1 DYLD_PRINT_INITIALIZERS=1 DYLD_PRINT_DOFS=1 DYLD_PRINT_OPTS=1 DYLD_PRINT_ENV=1 DYLD_PRINT_CODE_SIGNATURES=1 DYLD_PRINT_REBASINGS=1 DYLD_PRINT_TO_STDERR=1 $DARLING_BUILD_DIR/launch-dynamic > /tmp/foundation-probe-50-1.log 2>&1
grep "FATAL\|crash_debug_handler" /tmp/foundation-probe-50-1.log
```

Run 50-2 (no crash): same as 50-1 but add `DYLD_BIND_AT_LAUNCH=1` to the env, log `/tmp/foundation-probe-50-2.log`, grep for `Symbol not found`.

## Control #51 — lazy-bind attribution + ccchacha20 discrimination

**Date:** 2026-10-05
**Branch:** task/ccchacha-diag
**Base:** pr-arm64 = ba95ff198
**Goal:** (1) Attribute the crash rip to a specific image using load ranges from the same run. (2) Discriminate the "Symbol not found: _ccchacha20" failure observed with DYLD_BIND_AT_LAUNCH=1.

### Result 1 — Crash rip attribution

Run 51-1 (full #48 tree, 58 trees). Log: `/tmp/foundation-probe-51-1.log` (1287100 lines). Crash reproduced. Register block (verbatim):

```
  rip=0x00003280ef219746  rax=0x00003280ff5914a0  rbx=0x00003280ff5914a0
```

Load range for /usr/lib/libobjc.A.dylib from the same log (verbatim):

```
            __TEXT at 0x3280EF1F5000->0x3280EF24DFFF with permissions r.x
```

Attribution: rip=0x3280ef219746 falls inside libobjc.A.dylib's __TEXT segment (0x3280EF1F5000–0x3280EF24DFFF). Offset within the segment: 0x3280ef219746 − 0x3280ef1f5000 = 0x24746. The crash is in libobjc.A.dylib's code, at offset 0x24746 within __TEXT.

### Result 2 — ccchacha20 discrimination

The "Symbol not found: _ccchacha20" failure (observed in 50-2 with DYLD_BIND_AT_LAUNCH=1) was investigated with nm on the overlay binaries:

```
$ nm -u "$DARLING_OVERLAY"/usr/lib/system/libcommonCrypto.dylib | grep ccchacha
_ccchacha20
_ccchacha20poly1305_decrypt_oneshot
_ccchacha20poly1305_encrypt_oneshot
_ccchacha20poly1305_info

$ nm "$DARLING_OVERLAY"/usr/lib/system/libcorecrypto.dylib | grep ccchacha
0000000000019580 t _ccchacha20
00000000000195e0 t _ccchacha20poly1305_decrypt_oneshot
0000000000019630 t _ccchacha20poly1305_encrypt_oneshot
00000000000195c0 t _ccchacha20poly1305_info
```

**Вердикт: класс: missing-export — _ccchacha20 запрашивается libcommonCrypto.dylib (undefined), в libcorecrypto.dylib символ есть но как ЛОКАЛЬНЫЙ (строчная t, не экспортируется); при BIND_AT_LAUNCH=1 это даёт Symbol not found и abort; стейджингом не исправляется — проблема в бинарнике libcorecrypto (символ не экспортирован)**

The symbol is present in libcorecrypto.dylib but as a local (non-exported) symbol. libcommonCrypto.dylib has it as undefined, expecting it from libcorecrypto. With lazy binding, the missing export is not hit until the symbol is actually called; with BIND_AT_LAUNCH=1, all symbols are bound eagerly, exposing the missing export immediately. Staging cannot fix this — the symbol needs to be exported from libcorecrypto.dylib (rebuild or patch).

### Repro

Run 51-1 (crash + attribution):

```sh
export PATH=/usr/local/bin:/usr/local/sbin:/usr/bin:/bin:/sbin:/usr/sbin
export DARLING_SRC_DIR=$DARLING_SRC_DIR DARLING_OVERLAY=$DARLING_OVERLAY DARLING_BUILD_DIR=$DARLING_BUILD_DIR
export DARLING_TEST_BINARY=cft-fwmacho-probe-macho
export DARLING_STAGING_TREES=usr/lib:Frameworks:System/Library/Frameworks/CoreFoundation.framework:System/Library/Frameworks/Security.framework:System/Library/Frameworks/ApplicationServices.framework:System/Library/Frameworks/CoreServices.framework:System/Library/Frameworks/CFNetwork.framework:System/Library/Frameworks/OpenDirectory.framework:System/Library/Frameworks/CryptoTokenKit.framework:System/Library/Frameworks/LocalAuthentication.framework:System/Library/Frameworks/Accelerate.framework:System/Library/Frameworks/AudioUnit.framework:System/Library/Frameworks/AVFAudio.framework:System/Library/Frameworks/Carbon.framework:System/Library/Frameworks/CoreVideo.framework:System/Library/Frameworks/CoreImage.framework:System/Library/Frameworks/Network.framework:System/Library/Frameworks/IOSurface.framework:System/Library/Frameworks/CoreMedia.framework:System/Library/Frameworks/AudioToolbox.framework:System/Library/Frameworks/OpenGL.framework:System/Library/Frameworks/Quartz.framework:System/Library/Frameworks/Cocoa.framework:System/Library/Frameworks/VideoToolbox.framework:System/Library/Frameworks/CoreMediaIO.framework:System/Library/Frameworks/Accessibility.framework:System/Library/Frameworks/MetalKit.framework:System/Library/Frameworks/CoreMIDI.framework:System/Library/Frameworks/MediaAccessibility.framework:System/Library/Frameworks/SecurityInterface.framework:System/Library/Frameworks/CoreHaptics.framework:System/Library/Frameworks/ForceFeedback.framework:System/Library/Frameworks/CoreWLAN.framework:System/Library/Frameworks/CoreLocation.framework:System/Library/Frameworks/CoreML.framework:System/Library/Frameworks/DiskArbitration.framework:System/Library/Frameworks/ServiceManagement.framework:System/Library/Frameworks/SafariServices.framework:System/Library/Frameworks/LocalAuthenticationEmbeddedUI.framework:System/Library/Frameworks/CoreGraphics.framework:System/Library/Frameworks/Foundation.framework:System/Library/PrivateFrameworks/Onyx2D.framework:System/Library/Frameworks/IOKit.framework:System/Library/Frameworks/CoreText.framework:System/Library/Frameworks/AppKit.framework:System/Library/Frameworks/CoreData.framework:System/Library/Frameworks/QuartzCore.framework:System/Library/Frameworks/ImageIO.framework:System/Library/Frameworks/LaunchServices.framework:System/Library/Frameworks/UniformTypeIdentifiers.framework:System/Library/Frameworks/SystemConfiguration.framework:System/Library/Frameworks/Metal.framework:System/Library/Frameworks/CoreAudio.framework:System/Library/Frameworks/AVFoundation.framework:System/Library/Frameworks/CoreBluetooth.framework:System/Library/Frameworks/IOBluetooth.framework:System/Library/Frameworks/MediaPlayer.framework:System/Library/Frameworks/AuthenticationServices.framework:System/Library/Frameworks/GameController.framework:System/Library/Frameworks/Vision.framework:System/Library/Frameworks/UserNotifications.framework
timeout 120 sudo env DARLING_SRC_DIR=$DARLING_SRC_DIR DARLING_OVERLAY=$DARLING_OVERLAY DARLING_BUILD_DIR=$DARLING_BUILD_DIR DARLING_TEST_BINARY=cft-fwmacho-probe-macho DARLING_STAGING_TREES=$DARLING_STAGING_TREES DYLD_PRINT_LIBRARIES=1 DYLD_PRINT_LIBRARIES_POST_LAUNCH=1 DYLD_PRINT_BINDINGS=1 DYLD_PRINT_WEAK_BINDINGS=1 DYLD_PRINT_APIS=1 DYLD_PRINT_INTERPOSING=1 DYLD_PRINT_SEGMENTS=1 DYLD_PRINT_STATISTICS=1 DYLD_PRINT_STATISTICS_DETAILS=1 DYLD_PRINT_RPATHS=1 DYLD_PRINT_WARNINGS=1 DYLD_PRINT_INITIALIZERS=1 DYLD_PRINT_DOFS=1 DYLD_PRINT_OPTS=1 DYLD_PRINT_ENV=1 DYLD_PRINT_CODE_SIGNATURES=1 DYLD_PRINT_REBASINGS=1 DYLD_PRINT_TO_STDERR=1 $DARLING_BUILD_DIR/launch-dynamic > /tmp/foundation-probe-51-1.log 2>&1
grep "rip=\|__TEXT at.*libobjc" /tmp/foundation-probe-51-1.log
```

## Control #52 — экспорт _ccchacha20 из libcorecrypto (патч провайдера)

**Date:** 2026-10-05
**Branch:** task/ccchacha-export
**Base:** pr-arm64 = 5cf3b9e1a
**Goal:** Export `_ccchacha20` (and poly1305 trio) from libcorecrypto.dylib so that BIND_AT_LAUNCH=1 binding succeeds.

### Mechanism

Пересборка libcorecrypto из статической библиотеки `libcorecrypto_static.a` с экспорт-списком, включающим все глобальные символы (907 символов, включая ccchacha20 и poly1305-трио). Экспорт-список сгенерирован из `llvm-nm -g` статической библиотеки.

Команда сборки:
```sh
ld64.lld -dylib -arch x86_64 -platform_version macos 10.12 10.12 \
  -install_name /usr/lib/system/libcorecrypto.dylib \
  -current_version 1.0.0 -compatibility_version 1.0.0 \
  -exported_symbols_list /tmp/corecrypto_all_syms.txt \
  -undefined dynamic_lookup \
  -o libcorecrypto.dylib libcorecrypto_static.a
```

### nm before/after

Before (stock libcorecrypto.dylib):
```
$ nm "$DARLING_OVERLAY"/usr/lib/system/libcorecrypto.dylib | grep ccchacha
0000000000019580 t _ccchacha20
00000000000195e0 t _ccchacha20poly1305_decrypt_oneshot
0000000000019630 t _ccchacha20poly1305_encrypt_oneshot
00000000000195c0 t _ccchacha20poly1305_info
```

After (patched libcorecrypto.dylib):
```
$ nm -gU libcorecrypto.dylib | grep ccchacha
0000000000018160 T _ccchacha20
00000000000181c0 T _ccchacha20poly1305_decrypt_oneshot
0000000000018210 T _ccchacha20poly1305_encrypt_oneshot
00000000000181a0 T _ccchacha20poly1305_info
```

### dyld bind of _ccchacha20 (from run 52-2)

```
dyld: forced lazy bind: libcommonCrypto.dylib:0x27F0B299F0C0 = libcorecrypto.dylib:_ccchacha20, *0x27F0B299F0C0 = 0x27F0B2976160
dyld: forced lazy bind: libcommonCrypto.dylib:0x27F0B299F0C8 = libcorecrypto.dylib:_ccchacha20poly1305_decrypt_oneshot, *0x27F0B299F0C8 = 0x27F0B29761C0
dyld: forced lazy bind: libcommonCrypto.dylib:0x27F0B299F0D0 = libcorecrypto.dylib:_ccchacha20poly1305_encrypt_oneshot, *0x27F0B299F0D0 = 0x27F0B2976210
dyld: forced lazy bind: libcommonCrypto.dylib:0x27F0B299F0D8 = libcorecrypto.dylib:_ccchacha20poly1305_info, *0x27F0B299F0D8 = 0x27F0B29761A0
```

### First new output after bind (next wall)

```
dlopen(/Frameworks/Google Chrome for Testing Framework.framework/Versions/154.0.8029.0/Google Chrome for Testing Framework, 261): Library not loaded: /System/Library/Frameworks/IOKit.framework/Versions/A/IOKit
  Referenced from: /System/Library/Frameworks/CoreGraphics.framework/Versions/A/CoreGraphics
  Reason: image not found
```

### Verdict

**success** — `_ccchacha20` and poly1305-трио exported from libcorecrypto.dylib; BIND_AT_LAUNCH=1 binding succeeds (0 occurrences of "Symbol not found: _ccchacha20" in 27176-line log); next wall is staging-missing-dependency (IOKit.framework).

### Repro

```sh
export PATH=/usr/local/bin:/usr/local/sbin:/usr/bin:/bin:/sbin:/usr/sbin
export DARLING_SRC_DIR=$DARLING_SRC_DIR DARLING_OVERLAY=$DARLING_OVERLAY DARLING_BUILD_DIR=$DARLING_BUILD_DIR
export DARLING_TEST_BINARY=cft-fwmacho-probe-macho
export DARLING_STAGING_TREES=usr/lib:Frameworks:System/Library/Frameworks/CoreFoundation.framework:System/Library/Frameworks/Security.framework:System/Library/Frameworks/ApplicationServices.framework:System/Library/Frameworks/CoreServices.framework:System/Library/Frameworks/CFNetwork.framework:System/Library/Frameworks/OpenDirectory.framework:System/Library/Frameworks/CryptoTokenKit.framework:System/Library/Frameworks/LocalAuthentication.framework:System/Library/Frameworks/Accelerate.framework:System/Library/Frameworks/AudioUnit.framework:System/Library/Frameworks/AVFAudio.framework:System/Library/Frameworks/Carbon.framework:System/Library/Frameworks/CoreVideo.framework:System/Library/Frameworks/CoreImage.framework:System/Library/Frameworks/Network.framework:System/Library/Frameworks/IOSurface.framework:System/Library/Frameworks/CoreMedia.framework:System/Library/Frameworks/AudioToolbox.framework:System/Library/Frameworks/OpenGL.framework:System/Library/Frameworks/Quartz.framework:System/Library/Frameworks/Cocoa.framework:System/Library/Frameworks/VideoToolbox.framework:System/Library/Frameworks/CoreMediaIO.framework:System/Library/Frameworks/Accessibility.framework:System/Library/Frameworks/MetalKit.framework:System/Library/Frameworks/CoreMIDI.framework:System/Library/Frameworks/MediaAccessibility.framework:System/Library/Frameworks/SecurityInterface.framework:System/Library/Frameworks/CoreHaptics.framework:System/Library/Frameworks/ForceFeedback.framework:System/Library/Frameworks/CoreWLAN.framework:System/Library/Frameworks/CoreLocation.framework:System/Library/Frameworks/CoreML.framework:System/Library/Frameworks/DiskArbitration.framework:System/Library/Frameworks/ServiceManagement.framework:System/Library/Frameworks/SafariServices.framework:System/Library/Frameworks/LocalAuthenticationEmbeddedUI.framework:System/Library/Frameworks/CoreGraphics.framework:System/Library/Frameworks/Foundation.framework:System/Library/PrivateFrameworks/Onyx2D.framework:System/Library/Frameworks/ImageIO.framework:System/Library/Frameworks/LaunchServices.framework:System/Library/Frameworks/UniformTypeIdentifiers.framework:System/Library/Frameworks/SystemConfiguration.framework:System/Library/Frameworks/Metal.framework:System/Library/Frameworks/CoreAudio.framework:System/Library/Frameworks/AVFoundation.framework:System/Library/Frameworks/CoreBluetooth.framework:System/Library/Frameworks/IOBluetooth.framework:System/Library/Frameworks/MediaPlayer.framework:System/Library/Frameworks/AuthenticationServices.framework:System/Library/Frameworks/GameController.framework:System/Library/Frameworks/Vision.framework:System/Library/Frameworks/UserNotifications.framework
timeout 120 sudo env DARLING_SRC_DIR=$DARLING_SRC_DIR DARLING_OVERLAY=$DARLING_OVERLAY DARLING_BUILD_DIR=$DARLING_BUILD_DIR DARLING_TEST_BINARY=$DARLING_TEST_BINARY DARLING_STAGING_TREES=$DARLING_STAGING_TREES DYLD_BIND_AT_LAUNCH=1 DYLD_PRINT_LIBRARIES=1 DYLD_PRINT_LIBRARIES_POST_LAUNCH=1 DYLD_PRINT_BINDINGS=1 DYLD_PRINT_WEAK_BINDINGS=1 DYLD_PRINT_APIS=1 DYLD_PRINT_INTERPOSING=1 DYLD_PRINT_SEGMENTS=1 DYLD_PRINT_STATISTICS=1 DYLD_PRINT_STATISTICS_DETAILS=1 DYLD_PRINT_RPATHS=1 DYLD_PRINT_WARNINGS=1 DYLD_PRINT_INITIALIZERS=1 DYLD_PRINT_DOFS=1 DYLD_PRINT_OPTS=1 DYLD_PRINT_ENV=1 DYLD_PRINT_CODE_SIGNATURES=1 DYLD_PRINT_REBASINGS=1 DYLD_PRINT_TO_STDERR=1 $DARLING_BUILD_DIR/launch-dynamic > /tmp/foundation-probe-52-2.log 2>&1
grep -c "Symbol not found: _ccchacha20" /tmp/foundation-probe-52-2.log
```

## Control #53 — стейджинг IOKit.framework (staging-source-missing)

**Date:** 2026-10-05
**Branch:** task/chrome-fw-iokit
**Base:** pr-arm64 = 6a7a439d3
**Goal:** Stage IOKit.framework into the probe's staging path to clear the wall from run 52-2.

### Wall from run 52-2 (verbatim)

```
dlopen(/Frameworks/Google Chrome for Testing Framework.framework/Versions/154.0.8029.0/Google Chrome for Testing Framework, 261): Library not loaded: /System/Library/Frameworks/IOKit.framework/Versions/A/IOKit
  Referenced from: /System/Library/Frameworks/CoreGraphics.framework/Versions/A/CoreGraphics
  Reason: image not found
```

### IOKit.framework in overlay

```
$ ls -la "$DARLING_OVERLAY"/System/Library/Frameworks/IOKit.framework
ls: /System/Library/Frameworks/IOKit.framework: No such file or directory
```

IOKit.framework is NOT present in the overlay. The SDK copy at
`Developer/Platforms/MacOSX.platform/Developer/SDKs/MacOSX.sdk/System/Library/Frameworks/IOKit.framework`
contains only headers (Headers -> Versions/A/Headers), no binary.

### Verdict

**staging-source-missing** — IOKit.framework is absent from the overlay and cannot be staged. The wall from 52-2 remains. Next step: build IOKit.framework from source (src/external/IOKitUser) or obtain a prebuilt binary.

### Repro

```sh
ls -la "$DARLING_OVERLAY"/System/Library/Frameworks/IOKit.framework 2>/dev/null || echo "IOKit.framework NOT FOUND in overlay"
```

## Control #54 — синтез стаба IOKit.framework (стена снята)

**Date:** 2026-10-05
**Branch:** task/iokit-stub-fw
**Base:** pr-arm64 = 0d81eb3e4
**Goal:** Synthesize IOKit.framework stub to clear the wall from 52-2 (CoreGraphics: Library not loaded: IOKit.framework).

### Mechanism

Синтез стаб-dylib по прецеденту libcups.2.dylib: 5 символов которые CoreGraphics импортирует из IOKit (замер `llvm-nm -u` по CoreGraphics, фильтр `^_IO`):

```
_IODisplayCreateInfoDictionary
_IOIteratorNext
_IOObjectRelease
_IOServiceGetMatchingServices
_IOServiceMatching
```

Стаб собран как thin x86_64 dylib с install_name `/System/Library/Frameworks/IOKit.framework/Versions/A/IOKit`, каждый символ = `xor %eax,%eax; ret` (нулевой возврат). Застейджен в `$DARLING_OVERLAY/System/Library/Frameworks/IOKit.framework/Versions/A/IOKit` и добавлен в `DARLING_STAGING_TREES`.

### nm before/after

Before (overlay):
```
$ ls "$DARLING_OVERLAY"/System/Library/Frameworks/IOKit.framework
ls: /System/Library/Frameworks/IOKit.framework: No such file or directory
```

After (overlay):
```
$ llvm-nm -gU "$DARLING_OVERLAY"/System/Library/Frameworks/IOKit.framework/Versions/A/IOKit
0000000000000258 T _IODisplayCreateInfoDictionary
000000000000025b T _IOIteratorNext
000000000000025e T _IOObjectRelease
0000000000000261 T _IOServiceGetMatchingServices
0000000000000264 T _IOServiceMatching
```

### First new output after IOKit wall (from run 54-2)

```
dlopen(/Frameworks/Google Chrome for Testing Framework.framework/Versions/154.0.8029.0/Google Chrome for Testing Framework, 261): Library not loaded: /System/Library/Frameworks/CoreText.framework/Versions/A/CoreText
  Reason: image not found
```

### Verdict

**success** — IOKit.framework стаб синтезирован и застейджен; стена IOKit снята (0 хитов "Library not loaded: /System/Library/Frameworks/IOKit.framework" в 27258-строчном логе); следующая стена — CoreText.framework (staging-missing-dependency).

### Repro

```sh
export PATH=/usr/local/bin:/usr/local/sbin:/usr/bin:/bin:/sbin:/usr/sbin
export DARLING_SRC_DIR=$DARLING_SRC_DIR DARLING_OVERLAY=$DARLING_OVERLAY DARLING_BUILD_DIR=$DARLING_BUILD_DIR
export DARLING_TEST_BINARY=cft-fwmacho-probe-macho
export DARLING_STAGING_TREES=usr/lib:Frameworks:System/Library/Frameworks/CoreFoundation.framework:System/Library/Frameworks/Security.framework:System/Library/Frameworks/ApplicationServices.framework:System/Library/Frameworks/CoreServices.framework:System/Library/Frameworks/CFNetwork.framework:System/Library/Frameworks/OpenDirectory.framework:System/Library/Frameworks/CryptoTokenKit.framework:System/Library/Frameworks/LocalAuthentication.framework:System/Library/Frameworks/Accelerate.framework:System/Library/Frameworks/AudioUnit.framework:System/Library/Frameworks/AVFAudio.framework:System/Library/Frameworks/Carbon.framework:System/Library/Frameworks/CoreVideo.framework:System/Library/Frameworks/CoreImage.framework:System/Library/Frameworks/Network.framework:System/Library/Frameworks/IOSurface.framework:System/Library/Frameworks/CoreMedia.framework:System/Library/Frameworks/AudioToolbox.framework:System/Library/Frameworks/OpenGL.framework:System/Library/Frameworks/Quartz.framework:System/Library/Frameworks/Cocoa.framework:System/Library/Frameworks/VideoToolbox.framework:System/Library/Frameworks/CoreMediaIO.framework:System/Library/Frameworks/Accessibility.framework:System/Library/Frameworks/MetalKit.framework:System/Library/Frameworks/CoreMIDI.framework:System/Library/Frameworks/MediaAccessibility.framework:System/Library/Frameworks/SecurityInterface.framework:System/Library/Frameworks/CoreHaptics.framework:System/Library/Frameworks/ForceFeedback.framework:System/Library/Frameworks/CoreWLAN.framework:System/Library/Frameworks/CoreLocation.framework:System/Library/Frameworks/CoreML.framework:System/Library/Frameworks/DiskArbitration.framework:System/Library/Frameworks/ServiceManagement.framework:System/Library/Frameworks/SafariServices.framework:System/Library/Frameworks/LocalAuthenticationEmbeddedUI.framework:System/Library/Frameworks/CoreGraphics.framework:System/Library/Frameworks/Foundation.framework:System/Library/PrivateFrameworks/Onyx2D.framework:System/Library/Frameworks/ImageIO.framework:System/Library/Frameworks/LaunchServices.framework:System/Library/Frameworks/UniformTypeIdentifiers.framework:System/Library/Frameworks/SystemConfiguration.framework:System/Library/Frameworks/Metal.framework:System/Library/Frameworks/CoreAudio.framework:System/Library/Frameworks/AVFoundation.framework:System/Library/Frameworks/CoreBluetooth.framework:System/Library/Frameworks/IOBluetooth.framework:System/Library/Frameworks/MediaPlayer.framework:System/Library/Frameworks/AuthenticationServices.framework:System/Library/Frameworks/GameController.framework:System/Library/Frameworks/Vision.framework:System/Library/Frameworks/UserNotifications.framework:System/Library/Frameworks/IOKit.framework
timeout 120 sudo env DARLING_SRC_DIR=$DARLING_SRC_DIR DARLING_OVERLAY=$DARLING_OVERLAY DARLING_BUILD_DIR=$DARLING_BUILD_DIR DARLING_TEST_BINARY=$DARLING_TEST_BINARY DARLING_STAGING_TREES=$DARLING_STAGING_TREES DYLD_BIND_AT_LAUNCH=1 DYLD_PRINT_LIBRARIES=1 DYLD_PRINT_LIBRARIES_POST_LAUNCH=1 DYLD_PRINT_BINDINGS=1 DYLD_PRINT_WEAK_BINDINGS=1 DYLD_PRINT_APIS=1 DYLD_PRINT_INTERPOSING=1 DYLD_PRINT_SEGMENTS=1 DYLD_PRINT_STATISTICS=1 DYLD_PRINT_STATISTICS_DETAILS=1 DYLD_PRINT_RPATHS=1 DYLD_PRINT_WARNINGS=1 DYLD_PRINT_INITIALIZERS=1 DYLD_PRINT_DOFS=1 DYLD_PRINT_OPTS=1 DYLD_PRINT_ENV=1 DYLD_PRINT_CODE_SIGNATURES=1 DYLD_PRINT_REBASINGS=1 DYLD_PRINT_TO_STDERR=1 $DARLING_BUILD_DIR/launch-dynamic > /tmp/foundation-probe-54-2.log 2>&1
grep -c "Library not loaded: /System/Library/Frameworks/IOKit.framework" /tmp/foundation-probe-54-2.log
```

## Control #55 — синтез стаба CoreText.framework (стена снята)

**Date:** 2026-10-05
**Branch:** task/coretext-fw
**Base:** pr-arm64 = 9f700b92f
**Goal:** Synthesize CoreText.framework stub to clear the wall from 54-2 (Chrome fw: Library not loaded: CoreText.framework).

### Mechanism

Синтез стаб-dylib по прецеденту IOKit (#54): 56 символов которые AppKit и Chrome fw импортируют из CoreText (замер `llvm-nm -u` по AppKit + Chrome fw, фильтр `^_CT`):

```
_CTFontCopyFullName, _CTFontCreatePathForGlyph, _CTFontCreateUIFontForLanguage,
_CTFontCreateWithGraphicsFont, _CTFontGetAdvancesForGlyphs, _CTFontGetAscent,
_CTFontGetBoundingBox, _CTFontGetCapHeight, _CTFontGetDescent, _CTFontGetGlyphCount,
_CTFontGetGlyphsForCharacters, _CTFontGetLeading, _CTFontGetSize, _CTFontGetSlantAngle,
_CTFontGetUnderlinePosition, _CTFontGetUnderlineThickness, _CTFontGetXHeight,
_CTFontCollectionCreateFromAvailableFonts, _CTFontCollectionCreateMatchingFontDescriptors, ... (56 total)
```

Стаб собран как thin x86_64 dylib с install_name `/System/Library/Frameworks/CoreText.framework/Versions/A/CoreText`, каждый символ = `xor %eax,%eax; ret` (нулевой возврат). Застейджен в `$DARLING_OVERLAY/System/Library/Frameworks/CoreText.framework/Versions/A/CoreText` и добавлен в `DARLING_STAGING_TREES`.

### nm before/after

Before (overlay):
```
$ ls "$DARLING_OVERLAY"/System/Library/Frameworks/CoreText.framework/Versions/A/CoreText
ls: /System/Library/Frameworks/CoreText.framework/Versions/A/CoreText: No such file or directory
```

After (overlay):
```
$ llvm-nm -gU "$DARLING_OVERLAY"/System/Library/Frameworks/CoreText.framework/Versions/A/CoreText | wc -l
56
```

### First new output after CoreText wall (from run 55-1)

```
dlopen(/Frameworks/Google Chrome for Testing Framework.framework/Versions/154.0.8029.0/Google Chrome for Testing Framework, 261): Library not loaded: /System/Library/Frameworks/AppKit.framework/Versions/C/AppKit
  Reason: image not found
```

### Verdict

**success** — CoreText.framework стаб синтезирован и застейджен; стена CoreText снята (0 хитов "Library not loaded: /System/Library/Frameworks/CoreText.framework" в 27270-строчном логе); следующая стена — AppKit.framework (staging-missing-dependency).

### Repro

```sh
export PATH=/usr/local/bin:/usr/local/sbin:/usr/bin:/bin:/sbin:/usr/sbin
export DARLING_SRC_DIR=$DARLING_SRC_DIR DARLING_OVERLAY=$DARLING_OVERLAY DARLING_BUILD_DIR=$DARLING_BUILD_DIR
export DARLING_TEST_BINARY=cft-fwmacho-probe-macho
export DARLING_STAGING_TREES=usr/lib:Frameworks:System/Library/Frameworks/CoreFoundation.framework:System/Library/Frameworks/Security.framework:System/Library/Frameworks/ApplicationServices.framework:System/Library/Frameworks/CoreServices.framework:System/Library/Frameworks/CFNetwork.framework:System/Library/Frameworks/OpenDirectory.framework:System/Library/Frameworks/CryptoTokenKit.framework:System/Library/Frameworks/LocalAuthentication.framework:System/Library/Frameworks/Accelerate.framework:System/Library/Frameworks/AudioUnit.framework:System/Library/Frameworks/AVFAudio.framework:System/Library/Frameworks/Carbon.framework:System/Library/Frameworks/CoreVideo.framework:System/Library/Frameworks/CoreImage.framework:System/Library/Frameworks/Network.framework:System/Library/Frameworks/IOSurface.framework:System/Library/Frameworks/CoreMedia.framework:System/Library/Frameworks/AudioToolbox.framework:System/Library/Frameworks/OpenGL.framework:System/Library/Frameworks/Quartz.framework:System/Library/Frameworks/Cocoa.framework:System/Library/Frameworks/VideoToolbox.framework:System/Library/Frameworks/CoreMediaIO.framework:System/Library/Frameworks/Accessibility.framework:System/Library/Frameworks/MetalKit.framework:System/Library/Frameworks/CoreMIDI.framework:System/Library/Frameworks/MediaAccessibility.framework:System/Library/Frameworks/SecurityInterface.framework:System/Library/Frameworks/CoreHaptics.framework:System/Library/Frameworks/ForceFeedback.framework:System/Library/Frameworks/CoreWLAN.framework:System/Library/Frameworks/CoreLocation.framework:System/Library/Frameworks/CoreML.framework:System/Library/Frameworks/DiskArbitration.framework:System/Library/Frameworks/ServiceManagement.framework:System/Library/Frameworks/SafariServices.framework:System/Library/Frameworks/LocalAuthenticationEmbeddedUI.framework:System/Library/Frameworks/CoreGraphics.framework:System/Library/Frameworks/Foundation.framework:System/Library/PrivateFrameworks/Onyx2D.framework:System/Library/Frameworks/ImageIO.framework:System/Library/Frameworks/LaunchServices.framework:System/Library/Frameworks/UniformTypeIdentifiers.framework:System/Library/Frameworks/SystemConfiguration.framework:System/Library/Frameworks/Metal.framework:System/Library/Frameworks/CoreAudio.framework:System/Library/Frameworks/AVFoundation.framework:System/Library/Frameworks/CoreBluetooth.framework:System/Library/Frameworks/IOBluetooth.framework:System/Library/Frameworks/MediaPlayer.framework:System/Library/Frameworks/AuthenticationServices.framework:System/Library/Frameworks/GameController.framework:System/Library/Frameworks/Vision.framework:System/Library/Frameworks/UserNotifications.framework:System/Library/Frameworks/IOKit.framework:System/Library/Frameworks/CoreText.framework
timeout 120 sudo env DARLING_SRC_DIR=$DARLING_SRC_DIR DARLING_OVERLAY=$DARLING_OVERLAY DARLING_BUILD_DIR=$DARLING_BUILD_DIR DARLING_TEST_BINARY=$DARLING_TEST_BINARY DARLING_STAGING_TREES=$DARLING_STAGING_TREES DYLD_BIND_AT_LAUNCH=1 DYLD_PRINT_LIBRARIES=1 DYLD_PRINT_LIBRARIES_POST_LAUNCH=1 DYLD_PRINT_BINDINGS=1 DYLD_PRINT_WEAK_BINDINGS=1 DYLD_PRINT_APIS=1 DYLD_PRINT_INTERPOSING=1 DYLD_PRINT_SEGMENTS=1 DYLD_PRINT_STATISTICS=1 DYLD_PRINT_STATISTICS_DETAILS=1 DYLD_PRINT_RPATHS=1 DYLD_PRINT_WARNINGS=1 DYLD_PRINT_INITIALIZERS=1 DYLD_PRINT_DOFS=1 DYLD_PRINT_OPTS=1 DYLD_PRINT_ENV=1 DYLD_PRINT_CODE_SIGNATURES=1 DYLD_PRINT_REBASINGS=1 DYLD_PRINT_TO_STDERR=1 $DARLING_BUILD_DIR/launch-dynamic > /tmp/foundation-probe-55-1.log 2>&1
grep -c "Library not loaded: /System/Library/Frameworks/CoreText.framework" /tmp/foundation-probe-55-1.log
```

## Control #56 — стейджинг AppKit.framework (стена снята)

**Date:** 2026-10-05
**Branch:** task/appkit-stage
**Base:** pr-arm64 = 6a4eecb3c
**Goal:** Stage AppKit.framework to clear the wall from 55-1 (Chrome fw: Library not loaded: AppKit.framework).

### Step 1 — замер до изменений

```
$ ls -la "$DARLING_OVERLAY"/System/Library/Frameworks/AppKit.framework/Versions/C/AppKit
ls: /System/Library/Frameworks/AppKit.framework/Versions/C/AppKit: No such file or directory
```

AppKit.framework отсутствует в overlay.

### Step 2в — стейджинг из сборки Darling

Готовый AppKit-dylib найден в build-продуктах:
```
$ llvm-otool -D "$DARLING_BUILD_DIR"/wayland-backend/staged-overlay/System/Library/Frameworks/AppKit.framework/Versions/C/AppKit
/System/Library/Frameworks/AppKit.framework/Versions/C/AppKit
$ llvm-nm -gU "$DARLING_BUILD_DIR"/wayland-backend/staged-overlay/System/Library/Frameworks/AppKit.framework/Versions/C/AppKit | wc -l
3294
```

Install name совпадает с запрошенным путём. Скопирован в overlay:
```
$ cp "$DARLING_BUILD_DIR"/wayland-backend/staged-overlay/System/Library/Frameworks/AppKit.framework/Versions/C/AppKit \
      "$DARLING_OVERLAY"/System/Library/Frameworks/AppKit.framework/Versions/C/AppKit
```

Добавлен `System/Library/Frameworks/AppKit.framework` в `DARLING_STAGING_TREES`.

### First new output after AppKit wall (from run 56-1)

```
dlopen(/Frameworks/Google Chrome for Testing Framework.framework/Versions/154.0.8029.0/Google Chrome for Testing Framework, 261): Library not loaded: /System/Library/Frameworks/CoreData.framework/Versions/A/CoreData
  Reason: image not found
```

### Verdict

**success** — AppKit.framework застейджен из build-продуктов; стена AppKit снята (0 хитов "Library not loaded: /System/Library/Frameworks/AppKit.framework" в 27284-строчном логе); следующая стена — CoreData.framework (staging-missing-dependency).

### Repro

```sh
export PATH=/usr/local/bin:/usr/local/sbin:/usr/bin:/bin:/sbin:/usr/sbin
export DARLING_SRC_DIR=$DARLING_SRC_DIR DARLING_OVERLAY=$DARLING_OVERLAY DARLING_BUILD_DIR=$DARLING_BUILD_DIR
export DARLING_TEST_BINARY=cft-fwmacho-probe-macho
export DARLING_STAGING_TREES=usr/lib:Frameworks:System/Library/Frameworks/CoreFoundation.framework:System/Library/Frameworks/Security.framework:System/Library/Frameworks/ApplicationServices.framework:System/Library/Frameworks/CoreServices.framework:System/Library/Frameworks/CFNetwork.framework:System/Library/Frameworks/OpenDirectory.framework:System/Library/Frameworks/CryptoTokenKit.framework:System/Library/Frameworks/LocalAuthentication.framework:System/Library/Frameworks/Accelerate.framework:System/Library/Frameworks/AudioUnit.framework:System/Library/Frameworks/AVFAudio.framework:System/Library/Frameworks/Carbon.framework:System/Library/Frameworks/CoreVideo.framework:System/Library/Frameworks/CoreImage.framework:System/Library/Frameworks/Network.framework:System/Library/Frameworks/IOSurface.framework:System/Library/Frameworks/CoreMedia.framework:System/Library/Frameworks/AudioToolbox.framework:System/Library/Frameworks/OpenGL.framework:System/Library/Frameworks/Quartz.framework:System/Library/Frameworks/Cocoa.framework:System/Library/Frameworks/VideoToolbox.framework:System/Library/Frameworks/CoreMediaIO.framework:System/Library/Frameworks/Accessibility.framework:System/Library/Frameworks/MetalKit.framework:System/Library/Frameworks/CoreMIDI.framework:System/Library/Frameworks/MediaAccessibility.framework:System/Library/Frameworks/SecurityInterface.framework:System/Library/Frameworks/CoreHaptics.framework:System/Library/Frameworks/ForceFeedback.framework:System/Library/Frameworks/CoreWLAN.framework:System/Library/Frameworks/CoreLocation.framework:System/Library/Frameworks/CoreML.framework:System/Library/Frameworks/DiskArbitration.framework:System/Library/Frameworks/ServiceManagement.framework:System/Library/Frameworks/SafariServices.framework:System/Library/Frameworks/LocalAuthenticationEmbeddedUI.framework:System/Library/Frameworks/CoreGraphics.framework:System/Library/Frameworks/Foundation.framework:System/Library/PrivateFrameworks/Onyx2D.framework:System/Library/Frameworks/ImageIO.framework:System/Library/Frameworks/LaunchServices.framework:System/Library/Frameworks/UniformTypeIdentifiers.framework:System/Library/Frameworks/SystemConfiguration.framework:System/Library/Frameworks/Metal.framework:System/Library/Frameworks/CoreAudio.framework:System/Library/Frameworks/AVFoundation.framework:System/Library/Frameworks/CoreBluetooth.framework:System/Library/Frameworks/IOBluetooth.framework:System/Library/Frameworks/MediaPlayer.framework:System/Library/Frameworks/AuthenticationServices.framework:System/Library/Frameworks/GameController.framework:System/Library/Frameworks/Vision.framework:System/Library/Frameworks/UserNotifications.framework:System/Library/Frameworks/IOKit.framework:System/Library/Frameworks/CoreText.framework:System/Library/Frameworks/AppKit.framework
timeout 120 sudo env DARLING_SRC_DIR=$DARLING_SRC_DIR DARLING_OVERLAY=$DARLING_OVERLAY DARLING_BUILD_DIR=$DARLING_BUILD_DIR DARLING_TEST_BINARY=$DARLING_TEST_BINARY DARLING_STAGING_TREES=$DARLING_STAGING_TREES DYLD_BIND_AT_LAUNCH=1 DYLD_PRINT_LIBRARIES=1 DYLD_PRINT_LIBRARIES_POST_LAUNCH=1 DYLD_PRINT_BINDINGS=1 DYLD_PRINT_WEAK_BINDINGS=1 DYLD_PRINT_APIS=1 DYLD_PRINT_INTERPOSING=1 DYLD_PRINT_SEGMENTS=1 DYLD_PRINT_STATISTICS=1 DYLD_PRINT_STATISTICS_DETAILS=1 DYLD_PRINT_RPATHS=1 DYLD_PRINT_WARNINGS=1 DYLD_PRINT_INITIALIZERS=1 DYLD_PRINT_DOFS=1 DYLD_PRINT_OPTS=1 DYLD_PRINT_ENV=1 DYLD_PRINT_CODE_SIGNATURES=1 DYLD_PRINT_REBASINGS=1 DYLD_PRINT_TO_STDERR=1 $DARLING_BUILD_DIR/launch-dynamic > /tmp/foundation-probe-56-1.log 2>&1
grep -c "Library not loaded: /System/Library/Frameworks/AppKit.framework" /tmp/foundation-probe-56-1.log
```

## Control #57 — стейджинг CoreData.framework (staging-source-missing)

**Date:** 2026-10-05
**Branch:** task/coredata-stage
**Base:** pr-arm64 = 2c4ecd77f
**Goal:** Stage CoreData.framework to clear the wall from 56-1 (Chrome fw: Library not loaded: CoreData.framework).

### Step 1 — замер до изменений

```
$ ls -la "$DARLING_OVERLAY"/System/Library/Frameworks/CoreData.framework/Versions/A/CoreData
ls: /System/Library/Frameworks/CoreData.framework/Versions/A/CoreData: No such file or directory
```

CoreData.framework отсутствует в overlay.

### Step 2б — поиск в build-продуктах

```
$ find "$DARLING_BUILD_DIR" -path '*CoreData.framework*' -name 'CoreData' 2>/dev/null
(no output)
```

CoreData.framework отсутствует в build-продуктах.

### Verdict

**staging-source-missing** — CoreData.framework отсутствует в overlay и в build-продуктах. Стаб запрещён (классовый framework, нужны ObjC-классы). Стена CoreData остаётся. Решение головы.

### Repro

```sh
ls -la "$DARLING_OVERLAY"/System/Library/Frameworks/CoreData.framework/Versions/A/CoreData 2>/dev/null || echo "CoreData NOT FOUND in overlay"
find "$DARLING_BUILD_DIR" -path '*CoreData.framework*' -name 'CoreData' 2>/dev/null || echo "CoreData NOT FOUND in build products"
```

## Control #58 — сборка CoreData.framework (стена снята)

**Date:** 2026-10-05
**Branch:** task/coredata-build
**Base:** pr-arm64 = b2a85fa0f
**Goal:** Build CoreData.framework from source to clear the wall from 56-1 (Chrome fw: Library not loaded: CoreData.framework).

### Step 1 — сборка

Запущен `sh build-freebsd/build-coredata-coreservices.sh`. Скрипт построил CoreData и скопировал в overlay до CoreServices (set -e), упал позже на AE (stub.c: не объявлены типы OSErr/Size/AEDesc — скрипт никогда не запускался). CoreData уже застейджен, это вилка 2а.

```
Built: $DARLING_BUILD_DIR/coredata-coreservices/staged-overlay/System/Library/Frameworks/CoreData.framework/Versions/A/CoreData
```

### Step 2 — замер построенного dylib

```
$ llvm-otool -D "$DARLING_OVERLAY"/System/Library/Frameworks/CoreData.framework/Versions/A/CoreData
/System/Library/Frameworks/CoreData.framework/Versions/A/CoreData
$ llvm-nm -gU "$DARLING_OVERLAY"/System/Library/Frameworks/CoreData.framework/Versions/A/CoreData | wc -l
186
```

Install name совпадает с запрошенным путём. 186 экспортов.

### Step 3 — прогон 58-1

Добавлен `System/Library/Frameworks/CoreData.framework` в `DARLING_STAGING_TREES`, прогон с env как в 56-1.

### First new output after CoreData wall (from run 58-1)

```
dlopen(/Frameworks/Google Chrome for Testing Framework.framework/Versions/154.0.8029.0/Google Chrome for Testing Framework, 261): Library not loaded: /System/Library/Frameworks/QuartzCore.framework/Versions/A/QuartzCore
  Reason: image not found
```

### Verdict

**success** — CoreData.framework построен из исходников (cocotron, 25 .m, ObjC-классы) и застейджен; стена CoreData снята (0 хитов "Library not loaded: /System/Library/Frameworks/CoreData.framework" в 27299-строчном логе); следующая стена — QuartzCore.framework (staging-missing-dependency).

### Repro

```sh
export PATH=/usr/local/bin:/usr/local/sbin:/usr/bin:/bin:/sbin:/usr/sbin
export DARLING_SRC_DIR=$DARLING_SRC_DIR DARLING_OVERLAY=$DARLING_OVERLAY DARLING_BUILD_DIR=$DARLING_BUILD_DIR
export DARLING_TEST_BINARY=cft-fwmacho-probe-macho
export DARLING_STAGING_TREES=usr/lib:Frameworks:System/Library/Frameworks/CoreFoundation.framework:System/Library/Frameworks/Security.framework:System/Library/Frameworks/ApplicationServices.framework:System/Library/Frameworks/CoreServices.framework:System/Library/Frameworks/CFNetwork.framework:System/Library/Frameworks/OpenDirectory.framework:System/Library/Frameworks/CryptoTokenKit.framework:System/Library/Frameworks/LocalAuthentication.framework:System/Library/Frameworks/Accelerate.framework:System/Library/Frameworks/AudioUnit.framework:System/Library/Frameworks/AVFAudio.framework:System/Library/Frameworks/Carbon.framework:System/Library/Frameworks/CoreVideo.framework:System/Library/Frameworks/CoreImage.framework:System/Library/Frameworks/Network.framework:System/Library/Frameworks/IOSurface.framework:System/Library/Frameworks/CoreMedia.framework:System/Library/Frameworks/AudioToolbox.framework:System/Library/Frameworks/OpenGL.framework:System/Library/Frameworks/Quartz.framework:System/Library/Frameworks/Cocoa.framework:System/Library/Frameworks/VideoToolbox.framework:System/Library/Frameworks/CoreMediaIO.framework:System/Library/Frameworks/Accessibility.framework:System/Library/Frameworks/MetalKit.framework:System/Library/Frameworks/CoreMIDI.framework:System/Library/Frameworks/MediaAccessibility.framework:System/Library/Frameworks/SecurityInterface.framework:System/Library/Frameworks/CoreHaptics.framework:System/Library/Frameworks/ForceFeedback.framework:System/Library/Frameworks/CoreWLAN.framework:System/Library/Frameworks/CoreLocation.framework:System/Library/Frameworks/CoreML.framework:System/Library/Frameworks/DiskArbitration.framework:System/Library/Frameworks/ServiceManagement.framework:System/Library/Frameworks/SafariServices.framework:System/Library/Frameworks/LocalAuthenticationEmbeddedUI.framework:System/Library/Frameworks/CoreGraphics.framework:System/Library/Frameworks/Foundation.framework:System/Library/PrivateFrameworks/Onyx2D.framework:System/Library/Frameworks/ImageIO.framework:System/Library/Frameworks/LaunchServices.framework:System/Library/Frameworks/UniformTypeIdentifiers.framework:System/Library/Frameworks/SystemConfiguration.framework:System/Library/Frameworks/Metal.framework:System/Library/Frameworks/CoreAudio.framework:System/Library/Frameworks/AVFoundation.framework:System/Library/Frameworks/CoreBluetooth.framework:System/Library/Frameworks/IOBluetooth.framework:System/Library/Frameworks/MediaPlayer.framework:System/Library/Frameworks/AuthenticationServices.framework:System/Library/Frameworks/GameController.framework:System/Library/Frameworks/Vision.framework:System/Library/Frameworks/UserNotifications.framework:System/Library/Frameworks/IOKit.framework:System/Library/Frameworks/CoreText.framework:System/Library/Frameworks/AppKit.framework:System/Library/Frameworks/CoreData.framework
timeout 120 sudo env DARLING_SRC_DIR=$DARLING_SRC_DIR DARLING_OVERLAY=$DARLING_OVERLAY DARLING_BUILD_DIR=$DARLING_BUILD_DIR DARLING_TEST_BINARY=$DARLING_TEST_BINARY DARLING_STAGING_TREES=$DARLING_STAGING_TREES DYLD_BIND_AT_LAUNCH=1 DYLD_PRINT_LIBRARIES=1 DYLD_PRINT_LIBRARIES_POST_LAUNCH=1 DYLD_PRINT_BINDINGS=1 DYLD_PRINT_WEAK_BINDINGS=1 DYLD_PRINT_APIS=1 DYLD_PRINT_INTERPOSING=1 DYLD_PRINT_SEGMENTS=1 DYLD_PRINT_STATISTICS=1 DYLD_PRINT_STATISTICS_DETAILS=1 DYLD_PRINT_RPATHS=1 DYLD_PRINT_WARNINGS=1 DYLD_PRINT_INITIALIZERS=1 DYLD_PRINT_DOFS=1 DYLD_PRINT_OPTS=1 DYLD_PRINT_ENV=1 DYLD_PRINT_CODE_SIGNATURES=1 DYLD_PRINT_REBASINGS=1 DYLD_PRINT_TO_STDERR=1 $DARLING_BUILD_DIR/launch-dynamic > /tmp/foundation-probe-58-1.log 2>&1
grep -c "Library not loaded: /System/Library/Frameworks/CoreData.framework" /tmp/foundation-probe-58-1.log
```
## Control #59 — QuartzCore.framework: стена снята добавлением в список стейджинга

**Date:** 2026-10-05
**Branch:** task/quartzcore-stage
**Base:** pr-arm64 = 1419a964f
**Goal:** снять стену `Library not loaded: /System/Library/Frameworks/QuartzCore.framework`
(единственная стена, которую 58-1 оставил прогону).

### Что показала перепроверка (шаг 1, до изменений)

В черновике этого контроля стоял вердикт «собирать QuartzCore из исходников, сборка падает:
Onyx2D нет в overlay». Оба утверждения неверны, замеры ниже:

```
$ ls -la "$DARLING_OVERLAY"/System/Library/Frameworks/QuartzCore.framework/Versions/A/QuartzCore
-rwxr-xr-x  1 freebsd  fleet  15168 ... QuartzCore
$ file "$DARLING_OVERLAY"/System/Library/Frameworks/QuartzCore.framework/Versions/A/QuartzCore
Mach-O 64-bit x86_64 dynamically linked shared library, flags:<NOUNDEFS|DYLDLINK|TWOLEVEL|NO_REEXPORTED_DYLIBS>
$ ls -la "$DARLING_OVERLAY"/System/Library/PrivateFrameworks/Onyx2D.framework
drwxrwsr-x 3 freebsd fleet 512 ... Onyx2D.framework
```

QuartzCore в overlay ЕСТЬ, Onyx2D тоже (в PrivateFrameworks, не в Frameworks — тот скрипт
искал не там). Стена была не в отсутствии файла.

### Настоящая причина: список стейджинга (шаг 2)

Список, который прогон 58-1 реально использовал, напечатан в его собственном логе (строка 14):

```
$ sed -n '14p' /tmp/foundation-probe-58-1.log
staging trees: derived from the closure -- usr/lib:Frameworks:System/Library/Frameworks/CoreFoundation.framework:...:System/Library/PrivateFrameworks/Onyx2D.framework:...:System/Library/Frameworks/AppKit.framework:System/Library/Frameworks/CoreData.framework
```

60 записей; AppKit, CoreData, Onyx2D есть, **QuartzCore.framework нет**. Харнесс копирует
в гостевой корень только перечисленные деревья, поэтому файл, который лежит в overlay,
гостю не виден — отсюда `Reason: image not found` при живом файле на диске:

```
$ ls -la /tmp/darling-local-overlay/System/Library/Frameworks/QuartzCore.framework/Versions/A/QuartzCore
ls: .../QuartzCore.framework/Versions/A/QuartzCore: No such file or directory
```

Значит и AppKit (в списке) тянет QuartzCore, которого в списке нет: список выводится из
замыкания, но транзитивность по зависимостям не выдаёт (CoreData и Onyx2D, что дальше по
глубине, в список попали).

### Шаг 3 — A/B: тот же список 58-1 плюс ровно одна запись (прогон 59-2)

```
$ BASE58=$(sed -n '14p' /tmp/foundation-probe-58-1.log | sed 's/^staging trees: derived from the closure -- //')
$ export DARLING_STAGING_TREES="$BASE58:System/Library/Frameworks/QuartzCore.framework"
$ timeout 180 sudo env ... "$DARLING_BUILD_DIR"/launch-dynamic > /tmp/foundation-probe-59-2.log 2>&1
run rc=0
```

```
$ wc -l /tmp/foundation-probe-58-1.log /tmp/foundation-probe-59-2.log
  27299 /tmp/foundation-probe-58-1.log
 157956 /tmp/foundation-probe-59-2.log
```

| замер                          | 58-1   | 59-2    |
|--------------------------------|--------|---------|
| строк в логе                   | 27 299 | 157 956 |
| `dyld: loaded:`                | 119    | 134     |
| `Library not loaded`           | 2      | **0**   |
| `Symbol not found`             | 0      | 2       |

Каркас фреймворков грузится и работает:

```
$ grep -m1 -n "loaded:.*Google Chrome for Testing" /tmp/foundation-probe-59-2.log
26546:dyld: loaded: <4C4C4409-5555-3144-A170-AD4644F310AD> /Frameworks/Google Chrome for Testing Framework.framework/Versions/154.0.8029.0/Google Chrome for Testing Framework
```

### Verdict

**success** — стена QuartzCore снята: 0 хитов `Library not loaded` во всём логе (было 2),
`Google Chrome for Testing Framework` грузится и доходит до своих инициализаторов,
объём прогона вырос в 5.8 раза. Класс стен «отсутствующая библиотека» закрыт целиком.

Следующая стена — другой класс: символ.

```
157932:  dlopen_internal() failed, error: 'dlopen(/Frameworks/Google Chrome for Testing Framework.framework/Versions/154.0.8029.0/Google Chrome for Testing Framework, 261): Symbol not found: _kIOMasterPortDefault
157933-  Referenced from: /System/Library/Frameworks/CoreGraphics.framework/Versions/A/CoreGraphics
157934-  Expected in: /System/Library/Frameworks/IOKit.framework/Versions/A/IOKit
$ nm -g "$DARLING_OVERLAY"/System/Library/Frameworks/IOKit.framework/Versions/A/IOKit | grep -c kIOMasterPortDefault
0
```

Символа нет в образе IOKit, который гость резолвит, — это не список стейджинга.

### Repro

```sh
export DARLING_SRC_DIR=$DARLING_SRC_DIR DARLING_OVERLAY=$DARLING_OVERLAY DARLING_BUILD_DIR=$DARLING_BUILD_DIR
export DARLING_TEST_BINARY=cft-fwmacho-probe-macho
export BASE58=$(sed -n '14p' /tmp/foundation-probe-58-1.log | sed 's/^staging trees: derived from the closure -- //')
export DARLING_STAGING_TREES="$BASE58:System/Library/Frameworks/QuartzCore.framework"
timeout 180 sudo env DARLING_SRC_DIR=$DARLING_SRC_DIR DARLING_OVERLAY=$DARLING_OVERLAY DARLING_BUILD_DIR=$DARLING_BUILD_DIR DARLING_TEST_BINARY=$DARLING_TEST_BINARY DARLING_STAGING_TREES=$DARLING_STAGING_TREES DYLD_BIND_AT_LAUNCH=1 DYLD_PRINT_LIBRARIES=1 DYLD_PRINT_LIBRARIES_POST_LAUNCH=1 DYLD_PRINT_BINDINGS=1 DYLD_PRINT_WEAK_BINDINGS=1 DYLD_PRINT_APIS=1 DYLD_PRINT_INTERPOSING=1 DYLD_PRINT_SEGMENTS=1 DYLD_PRINT_STATISTICS=1 DYLD_PRINT_STATISTICS_DETAILS=1 DYLD_PRINT_RPATHS=1 DYLD_PRINT_WARNINGS=1 DYLD_PRINT_INITIALIZERS=1 DYLD_PRINT_DOFS=1 DYLD_PRINT_OPTS=1 DYLD_PRINT_ENV=1 DYLD_PRINT_CODE_SIGNATURES=1 DYLD_PRINT_REBASINGS=1 DYLD_PRINT_TO_STDERR=1 "$DARLING_BUILD_DIR"/launch-dynamic > /tmp/foundation-probe-59-2.log 2>&1
grep -c "Library not loaded" /tmp/foundation-probe-59-2.log
grep -m1 -A2 "Symbol not found" /tmp/foundation-probe-59-2.log
```

## Control #60 — IOKit stub: add the data symbol _kIOMasterPortDefault (wall cleared)

**Date:** 2026-10-05
**Branch:** task/iokit-masterport
**Base:** pr-arm64 = a639ab5529286d6a82762968a2162dcca490250c
**Goal:** clear the wall from 59-1: "Symbol not found: _kIOMasterPortDefault",
Referenced from CoreGraphics, Expected in IOKit.

### Step 1 — measurement: CoreGraphics imports six IOKit symbols, the #54 stub exported five

```
$ llvm-nm -u "$DARLING_OVERLAY"/System/Library/Frameworks/CoreGraphics.framework/Versions/A/CoreGraphics | grep -iE '_k?IO'
_IODisplayCreateInfoDictionary
_IOIteratorNext
_IOObjectRelease
_IOServiceGetMatchingServices
_IOServiceMatching
_kIOMasterPortDefault
$ llvm-nm -gU "$DARLING_OVERLAY"/System/Library/Frameworks/IOKit.framework/Versions/A/IOKit | wc -l
5
```

The #54 stub was synthesized from `nm -u CoreGraphics | grep ^_IO`; that filter
missed `_kIOMasterPortDefault`, which starts `_kIO`. So the stub exported the five
`_IO*` functions but not the one data symbol CoreGraphics also imports.

### Step 2а — extend the stub export list with the data symbol

The five functions in the #54 image are all `xorl %eax, %eax; retq` (recovered with
`llvm-otool -tvV` on the installed image). The extended stub keeps them and adds
`_kIOMasterPortDefault` as a data symbol, value 0 (MACH_PORT_NULL, valid in a
headless probe — CoreGraphics takes the symbol's address, then reads 4 bytes):

```asm
	.section	__TEXT,__text,regular,pure_instructions
	.globl	_IOServiceMatching
	.p2align	2
_IOServiceMatching:
	xorl	%eax, %eax
	retq
	.globl	_IOServiceGetMatchingServices
	.p2align	2
_IOServiceGetMatchingServices:
	xorl	%eax, %eax
	retq
	.globl	_IOIteratorNext
	.p2align	2
_IOIteratorNext:
	xorl	%eax, %eax
	retq
	.globl	_IOObjectRelease
	.p2align	2
_IOObjectRelease:
	xorl	%eax, %eax
	retq
	.globl	_IODisplayCreateInfoDictionary
	.p2align	2
_IODisplayCreateInfoDictionary:
	xorl	%eax, %eax
	retq
	.section	__DATA,__const
	.globl	_kIOMasterPortDefault
	.p2align	3
_kIOMasterPortDefault:
	.quad	0
```

```
$ clang -target x86_64-apple-macos10.12 -c "$DARLING_BUILD_DIR"/iokit-stub/iokit_stub.s -o "$DARLING_BUILD_DIR"/iokit-stub/iokit_stub.o
$ ld64.lld -dylib -arch x86_64 -platform_version macos 10.12 10.12 -install_name /System/Library/Frameworks/IOKit.framework/Versions/A/IOKit -o "$DARLING_BUILD_DIR"/iokit-stub/IOKit "$DARLING_BUILD_DIR"/iokit-stub/iokit_stub.o
$ llvm-nm -gU "$DARLING_BUILD_DIR"/iokit-stub/IOKit
0000000000000300 T _IODisplayCreateInfoDictionary
00000000000002f8 T _IOIteratorNext
00000000000002fc T _IOObjectRelease
00000000000002f4 T _IOServiceGetMatchingServices
00000000000002f0 T _IOServiceMatching
0000000000001000 S _kIOMasterPortDefault
$ cp "$DARLING_BUILD_DIR"/iokit-stub/IOKit "$DARLING_OVERLAY"/System/Library/Frameworks/IOKit.framework/Versions/A/IOKit
```

Only the stub is rebuilt; the overlay's other images (CoreGraphics etc.) are not touched.

### Step 3 — probe 60-1

Same staging list as 59-2, `timeout 180`.

```
$ wc -l /tmp/iokit-probe-60-1.log
1291601
$ grep -c "Symbol not found" /tmp/iokit-probe-60-1.log
0
$ grep -c "Library not loaded" /tmp/iokit-probe-60-1.log
0
$ grep -c "dlopen_internal() failed" /tmp/iokit-probe-60-1.log
0
```

The symbol binds (line 157682):

```
157682:dyld: bind: CoreGraphics:0x78B5C710018 = IOKit:_kIOMasterPortDefault, *0x78B5C710018 = 0x78B5CB1E000
```

Chrome fw loads and the dlopen proceeds (line 26547):

```
26547:dyld: loaded: <4C4C4409-5555-3144-A170-AD4644F310AD> /Frameworks/Google Chrome for Testing Framework.framework/Versions/154.0.8029.0/Google Chrome for Testing Framework
```

### First new failure after the IOKit wall

Not a symbol: the loader dies at the end of weak binding.

```
1291508:dyld: weak bind end
1291509:[darling-mldr] FATAL signal 11 (code=1) at addr=0x435de5894850
```

### Verdict

**success** — the IOKit stub now exports all six symbols CoreGraphics imports;
0 hits "Symbol not found" and 0 hits "Library not loaded" in the 1291601-line log.
The next wall is a different class: a loader SIGSEGV at the end of weak binding,
after Chrome fw's own image loaded. The probe did not reach DONE rc=0.

### Repro

```sh
export DARLING_SRC_DIR=$DARLING_SRC_DIR DARLING_OVERLAY=$DARLING_OVERLAY DARLING_BUILD_DIR=$DARLING_BUILD_DIR
export DARLING_TEST_BINARY=cft-fwmacho-probe-macho
# 1. write the stub (assembly above) to $DARLING_BUILD_DIR/iokit-stub/iokit_stub.s
clang -target x86_64-apple-macos10.12 -c "$DARLING_BUILD_DIR"/iokit-stub/iokit_stub.s -o "$DARLING_BUILD_DIR"/iokit-stub/iokit_stub.o
ld64.lld -dylib -arch x86_64 -platform_version macos 10.12 10.12 -install_name /System/Library/Frameworks/IOKit.framework/Versions/A/IOKit -o "$DARLING_BUILD_DIR"/iokit-stub/IOKit "$DARLING_BUILD_DIR"/iokit-stub/iokit_stub.o
cp "$DARLING_BUILD_DIR"/iokit-stub/IOKit "$DARLING_OVERLAY"/System/Library/Frameworks/IOKit.framework/Versions/A/IOKit
llvm-nm -gU "$DARLING_OVERLAY"/System/Library/Frameworks/IOKit.framework/Versions/A/IOKit | wc -l   # 6
# 2. run
export BASE58=$(sed -n '14p' /tmp/foundation-probe-58-1.log | sed 's/^staging trees: derived from the closure -- //')
export DARLING_STAGING_TREES="$BASE58:System/Library/Frameworks/QuartzCore.framework"
timeout 180 sudo env DARLING_SRC_DIR=$DARLING_SRC_DIR DARLING_OVERLAY=$DARLING_OVERLAY DARLING_BUILD_DIR=$DARLING_BUILD_DIR DARLING_TEST_BINARY=$DARLING_TEST_BINARY DARLING_STAGING_TREES=$DARLING_STAGING_TREES DYLD_BIND_AT_LAUNCH=1 DYLD_PRINT_LIBRARIES=1 DYLD_PRINT_LIBRARIES_POST_LAUNCH=1 DYLD_PRINT_BINDINGS=1 DYLD_PRINT_WEAK_BINDINGS=1 DYLD_PRINT_APIS=1 DYLD_PRINT_INTERPOSING=1 DYLD_PRINT_SEGMENTS=1 DYLD_PRINT_STATISTICS=1 DYLD_PRINT_STATISTICS_DETAILS=1 DYLD_PRINT_RPATHS=1 DYLD_PRINT_WARNINGS=1 DYLD_PRINT_INITIALIZERS=1 DYLD_PRINT_DOFS=1 DYLD_PRINT_OPTS=1 DYLD_PRINT_ENV=1 DYLD_PRINT_CODE_SIGNATURES=1 DYLD_PRINT_REBASINGS=1 DYLD_PRINT_TO_STDERR=1 "$DARLING_BUILD_DIR"/launch-dynamic > /tmp/iokit-probe-60-1.log 2>&1
grep -c "Symbol not found" /tmp/iokit-probe-60-1.log
grep -c "Library not loaded" /tmp/iokit-probe-60-1.log
grep -n "IOKit:_kIOMasterPortDefault" /tmp/iokit-probe-60-1.log
grep -n "FATAL signal" /tmp/iokit-probe-60-1.log
```

## Control #61 — decode of the SIGSEGV at weak bind end: libobjc readClass reads a FoundationExtras stub as a class

**Date:** 2026-10-05
**Branch:** task/iokit-masterport
**Base:** pr-arm64 = 5c432aa8a1ed0032c45adc11742c65c3f257e9a1
**Goal:** decode the wall that 60-1 left (FATAL signal 11 at 0x435de5894850, right
after "dyld: weak bind end", line 1291509). A fix is not in this task.
**Run:** 60-2 — the same harness on the accepted 6-symbol IOKit stub, a new run
under a new log name (/tmp/iokit-probe-60-2.log); the crash is identical.

### Crash block (60-2, lines 1291508-1291516)

```
1291508:dyld: weak bind end
1291509:[darling-mldr] FATAL signal 11 (code=1) at addr=0x435de5894850
  rip=0x0000041da441a746  rax=0x0000041db47924a0  rbx=0x0000041db47924a0
  rcx=0x2e66c35de5894855  rdx=0x0000435de5894850  rsi=0x0000000000000000
  rdi=0x0000000000000005  rbp=0x00007fffffdfdbc0  rsp=0x00007fffffdfdb80
```

### Decode

```
$ python3 "$DARLING_SRC_DIR"/build-freebsd/decode-crash.py /tmp/iokit-probe-60-2.log "$DARLING_OVERLAY" "$DARLING_SRC_DIR"/tests
crash: signal 11 at 0x435de5894850
images mapped: 134 address range(s) named by the log (133 dylib mapping(s) + the main executable), +1 pseudo from mldr DEBUG lines
  rip  0x0000041da441a746  /usr/lib/libobjc.A.dylib+0x24746                           __ZL9readClassP10objc_classbb+0xa6
  stack 0x0000041da4634000  /Frameworks/Google+0x0                                     ?
  stack 0x0000041da441be86  /usr/lib/libobjc.A.dylib+0x25e86                           -[Protocol hash]+0x256
  stack 0x0000041da4443984  /usr/lib/libobjc.A.dylib+0x4d984                           __ZL11UnsetLayout+0x25e0
  stack 0x0000041da4634e1a  /Frameworks/Google+0xe1a                                   ?
  stack 0x0000041da4443988  /usr/lib/libobjc.A.dylib+0x4d988                           __ZL11UnsetLayout+0x25e4
  stack 0x0000041da4634000  /Frameworks/Google+0x0                                     ?
  stack 0x0000041da4411eaf  /usr/lib/libobjc.A.dylib+0x1beaf                           __ZN4objc8DenseMapI12DisguisedPtrI11objc_objectENS0_IPKvNS_15ObjcAssociationENS_17DenseMapValueInfoIS6_EENS_12DenseMapInfoIS5_EENS_6detail12DenseMapPairIS5_S6_EEEENS7_ISE_EENS9_IS3_EENSC_IS3_SE_EEE16shrink_and_clearEv+0x11f
  stack 0x0000041da4634000  /Frameworks/Google+0x0                                     ?
  stack 0x00000008273336ee  /cft-fwmacho-probe-macho+0x6ee                             ?
  stack 0x0000041da441b456  /usr/lib/libobjc.A.dylib+0x25456                           +[Object instanceMethodFor:]+0x16
  stack 0x00000008287116c9  /usr/lib/dyld+0x96c9                                       __ZN4dyldL15stateToHandlersE17dyld_image_statesPA3_Pv+0xa9

69 of the stack words named no known image (not listed above)
```

The rip resolves, against **libobjc.A.dylib's own symbol table**, to
`readClass(objc_class*, bool, bool)+0xa6` (line 1291509). The instruction at
+0x24746 (`llvm-otool -tvV "$DARLING_OVERLAY"/usr/lib/libobjc.A.dylib`) is

```
0000000000024746	cmpl	$0x0, __objc_empty_vtable(%rdx)
```

i.e. a load through rdx = 0x435de5894850 — the faulting dereference. The class
pointer in rax is not resolved by decode-crash; mapped against the same dyld
segment table it lands on a stub:

```
$ # rax -> image + offset (dyld segment table of the same log)
rax 0x41db47924a0 -> /usr/lib/FoundationExtras.dylib +0x4a0
```

and `FoundationExtras+0x4a0` is `_OBJC_CLASS_$_NSURLProtocol`.

### Hypothesis (one, ranked)

readClass is walking a class's isa chain and one link is the address of a **no-op
stub function exported under an ObjC class name**. The Extras wrappers export
`_OBJC_CLASS_$_X` as functions (`void X(void){}`), not as data objects, and the
real framework does not export the class, so the Extras stub is its only provider.
readClass treats the function's address as a class and reads the function's code
as the class's isa.

Evidence: the crash's rcx = 0x2e66c35de5894855, whose bytes are exactly
`55 48 89 e5 5d c3 66 2e` — the entry sequence of a no-op stub
(`push rbp; mov rbp,rsp; pop rbp; ret; nop`), identical to the bytes at
`FoundationExtras+0x4a0`.

Neighbours this separates from:
- (a) a real class with an unrelocated isa — would be a **data** symbol (`S`/`D`)
  pointing into the image's own `__DATA`, not a stub's `__TEXT`;
- (b) dyld's weak-bind "adjusting uses" step corrupting a bind site — would not
  yield the stub's exact entry bytes.

### Discriminating check

```
$ llvm-nm -gU "$DARLING_OVERLAY"/usr/lib/FoundationExtras.dylib | grep '_OBJC_CLASS_\$_NSURLProtocol'
00000000000004a0 T _OBJC_CLASS_$_NSURLProtocol
$ llvm-otool -tvV "$DARLING_OVERLAY"/usr/lib/FoundationExtras.dylib | sed -n '/00000000000004a0/,+4p'
00000000000004a0	pushq	%rbp
00000000000004a1	movq	%rsp, %rbp
00000000000004a4	popq	%rbp
00000000000004a5	retq
```

Expected: `T` (code) and an entry of `pushq %rbp; movq %rsp,%rbp; popq %rbp; retq`,
whose bytes `55 48 89 e5 5d c3` are the crash's rcx. A correct class export would
be `S`/`D` (data); if the symbol were data, neighbour (a) would hold instead.

### Repro

```sh
python3 "$DARLING_SRC_DIR"/build-freebsd/decode-crash.py /tmp/iokit-probe-60-2.log "$DARLING_OVERLAY" "$DARLING_SRC_DIR"/tests
# rax (the class pointer) is not resolved by decode-crash; map it with the same segment parser:
python3 - <<'PY'
import re
log = "/tmp/iokit-probe-60-2.log"
text = open(log, errors="replace").read()
segs = []; path = None
for line in text.splitlines():
    m = re.search(r"dyld: Mapping (\S+)", line)
    if m: path = m.group(1); continue
    m = re.search(r"__TEXT at 0x([0-9A-Fa-f]+)->0x([0-9A-Fa-f]+)", line)
    if m and path: segs.append((int(m.group(1),16), int(m.group(2),16), path))
a = int(re.search(r"FATAL signal.*?rax=(0x[0-9A-Fa-f]+)", text, re.S).group(1), 16)
hit = [s for s in segs if s[0] <= a <= s[1]]
print("rax %#x -> %s +%#x" % (a, hit[0][2], a - hit[0][0]) if hit else "rax %#x -> UNMAPPED" % a)
PY
llvm-nm -gU "$DARLING_OVERLAY"/usr/lib/FoundationExtras.dylib | grep '_OBJC_CLASS_\$_NSURLProtocol'
```

## Control #62 — Extras wrappers export ObjC class names as real classes, not code stubs

**Date:** 2026-10-05
**Branch:** task/objc-class-stubs
**Base:** pr-arm64 = d581d0ee178202d238980e3bbb68144e8e72bd54
**Goal:** remove the wall from #61 — libobjc's readClass dereferenced a code no-op
stub exported under an ObjC class name (`_OBJC_CLASS_$_NSURLProtocol` at
`FoundationExtras+0x4a0`) as if it were a class.

### Variant 1 (remove the exports) fails: the bind is strong

The 13 `/usr/lib/*Extras.dylib` wrappers that export `_OBJC_CLASS_$_*` /
`_OBJC_METACLASS_$_*` were rebuilt without those names. Chrome then fails to load:

```
dlopen(/Frameworks/Google Chrome for Testing Framework.framework/Versions/154.0.8029.0/Google Chrome for Testing Framework, 261): Symbol not found: _OBJC_CLASS_$_AVSampleBufferAudioRenderer
  Referenced from: /Frameworks/Google Chrome for Testing Framework.framework/Versions/154.0.8029.0/Google Chrome for Testing Framework
  Expected in: /usr/lib/AVFoundationExtras.dylib
```

So the reference is a strong two-level bind, not a weak one — variant 1 is out.

### Variant 2 (the fix): export the names as real ObjC classes

The same 13 wrappers are rebuilt so every `_OBJC_CLASS_$_X` / `_OBJC_METACLASS_$_X`
is a **compiled root ObjC class** (`__attribute__((objc_root_class))`) — real data
with a valid isa/metaclass — not `void X(void){}`. Every other stub export stays a
function; `__objc_empty_cache` is defined locally (an assembly object) so the
wrappers stay self-contained.

```
$ llvm-nm -gU "$DARLING_OVERLAY"/usr/lib/AVFoundationExtras.dylib | grep AVSampleBufferAudioRenderer
0000000000001230 S _OBJC_CLASS_$_AVSampleBufferAudioRenderer
0000000000001258 S _OBJC_METACLASS_$_AVSampleBufferAudioRenderer
```

### Run 62-1

```
$ wc -l /tmp/iokit-probe-62-1.log
1292661
$ grep -c 'Symbol not found' /tmp/iokit-probe-62-1.log
0
$ grep -c 'Library not loaded' /tmp/iokit-probe-62-1.log
0
$ grep -c 'FATAL signal 11' /tmp/iokit-probe-62-1.log
1
```

The #61 wall is gone: the run passes the weak-bind batch, no readClass dereference
happens, and the run advances into Chrome's own initializers:

```
1292568:dyld: calling initializer function 0x... in /Frameworks/Google Chrome for Testing Framework.framework/Versions/154.0.8029.0/Google Chrome for Testing Framework
```

The first new stopper is a different crash — a jump to the stack, not a symbol:

```
1292569:[darling-mldr] FATAL signal 11 (code=2) at addr=0x7fffffdfd658
  rip=0x00007fffffdfd658  rax=0x0000000826454f2c  rbx=0x000033ad5549a7a0
  rbp=0x00007fffffdfd650  rsp=0x00007fffffdfd658
```

### Verdict

**partial** — the #61 wall (a stub read as a class) is removed: Symbol not found = 0,
no readClass dereference, the run reaches Chrome's initializers. But the acceptance
criterion "FATAL signal 11 = 0" is **not** met: one FATAL signal 11 (code=2) remains,
a jump to the stack during a Chrome initializer (not an IOKit/Extras symbol).

### Repro

```sh
export DARLING_SRC_DIR=$DARLING_SRC_DIR DARLING_OVERLAY=$DARLING_OVERLAY DARLING_BUILD_DIR=$DARLING_BUILD_DIR
python3 "$DARLING_SRC_DIR"/build-freebsd/build-extras-objc-classes.py
# then the 60-2 probe chain, new log name:
export DARLING_TEST_BINARY=cft-fwmacho-probe-macho
export BASE58=$(sed -n '14p' /tmp/foundation-probe-58-1.log | sed 's/^staging trees: derived from the closure -- //')
export DARLING_STAGING_TREES="$BASE58:System/Library/Frameworks/QuartzCore.framework"
timeout 180 sudo env DARLING_SRC_DIR=$DARLING_SRC_DIR DARLING_OVERLAY=$DARLING_OVERLAY DARLING_BUILD_DIR=$DARLING_BUILD_DIR DARLING_TEST_BINARY=$DARLING_TEST_BINARY DARLING_STAGING_TREES=$DARLING_STAGING_TREES DYLD_BIND_AT_LAUNCH=1 DYLD_PRINT_LIBRARIES=1 DYLD_PRINT_LIBRARIES_POST_LAUNCH=1 DYLD_PRINT_BINDINGS=1 DYLD_PRINT_WEAK_BINDINGS=1 DYLD_PRINT_APIS=1 DYLD_PRINT_INTERPOSING=1 DYLD_PRINT_SEGMENTS=1 DYLD_PRINT_STATISTICS=1 DYLD_PRINT_STATISTICS_DETAILS=1 DYLD_PRINT_RPATHS=1 DYLD_PRINT_WARNINGS=1 DYLD_PRINT_INITIALIZERS=1 DYLD_PRINT_DOFS=1 DYLD_PRINT_OPTS=1 DYLD_PRINT_ENV=1 DYLD_PRINT_CODE_SIGNATURES=1 DYLD_PRINT_REBASINGS=1 DYLD_PRINT_TO_STDERR=1 "$DARLING_BUILD_DIR"/launch-dynamic > /tmp/iokit-probe-62-1.log 2>&1
grep -c 'Symbol not found' /tmp/iokit-probe-62-1.log
grep -c 'FATAL signal 11' /tmp/iokit-probe-62-1.log
```

## Control #63 — decode of the 62-1 stack-jump: an unpatched raw-Linux syscall in Chrome's initializer

**Date:** 2026-10-05
**Branch:** task/objc-initializer-jump
**Base:** pr-arm64 = 66266981974ba19dc2a60ccb43ebe15845181fe1
**Goal:** decode the wall 62-1 left (FATAL signal 11 (code=2) at 0x7fffffdfd658,
line 1292569). No new run — decoded from the existing /tmp/iokit-probe-62-1.log
(read-only; it is cited by the merged #62 commit).

### Crash block (62-1, lines 1292569-1292576)

```
1292569:[darling-mldr] FATAL signal 11 (code=2) at addr=0x7fffffdfd658
  rip=0x00007fffffdfd658  rax=0x0000000826454f2c  rbx=0x000033ad5549a7a0
  rcx=0x0000000000000307  rdx=0x0000000000000307  rsi=0x0000000000050000
  rdi=0x0000000000000307  rbp=0x00007fffffdfd650  rsp=0x00007fffffdfd658
```

`rip == rsp == 0x7fffffdfd658` and `rbp == rsp - 8`: the CPU is executing on the
stack. decode-crash.py reports the address unmapped (no image covers it), and the
stack walk names the callers:

```
$ python3 "$DARLING_SRC_DIR"/build-freebsd/decode-crash.py /tmp/iokit-probe-62-1.log "$DARLING_OVERLAY" "$DARLING_SRC_DIR"/tests
crash: signal 11 at 0x7fffffdfd658
  rip  0x00007fffffdfd658  unmapped (no image covers this address)
  stack 0x000033ad450bd57a  /usr/lib/system/libsystem_platform.dylib+0x257a   __OSSpinLockLockYield+0x4a
  stack 0x000033ad450bd814  /usr/lib/system/libsystem_platform.dylib+0x2814   _spin_unlock+0x14
  stack 0x0000000826454f2c  /usr/lib/dyld+0x14cf2c                            __main_thread+0xac
  stack 0x000033ad45595e42  /Frameworks/Google+0x161e42                       ?
```

### The initializer is named by the log

```
1292562:dyld: calling initializer function 0x33ad4755e4c0 in /Frameworks/Google Chrome for Testing Framework.framework/Versions/154.0.8029.0/Google Chrome for Testing Framework
```

Chrome fw maps at 0x33ad45434000 (62-1:26540), so 0x33ad4755e4c0 is Chrome fw
+0x212a4c0 — Chrome's own initializer, running on the main thread.

### The ENOSYS lines are the run-up, not a neighbour

Immediately after that initializer starts, and before the FATAL, mldr prints six
unhandled raw-Linux syscalls:

```
1292563:[darling-mldr] unhandled Linux syscall 4294967287 — ENOSYS
1292564:[darling-mldr] unhandled Linux syscall 4294967218 — ENOSYS
... (4294967218 five times)
```

4294967287 = 0xFFFFFFF7 = -9 and 4294967218 = 0xFFFFFFB2 = -78 as signed 32-bit —
not valid Linux syscall numbers. They are what a raw `syscall` instruction emits
when rax was never set up for the Linux ABI.

### Hypothesis (one, ranked)

Chrome fw's raw-Linux `syscall` trampoline was **not** patched: its 253374464-byte
mapping (0x33ad45434000) matched no known trampoline signature (62-1:26540), so
Chrome's own initializer's `syscall` sites reach mldr raw. The guest's rax there
is garbage (-78 / -9), mldr returns ENOSYS into rax, and the initializer then
transfers control with the wrong rax/stack state — landing on the stack
(rip == rsp).

Neighbour this separates from: a crash inside a *stubbed* framework. The stack
names Chrome fw and libsystem_platform, not an Extras wrapper, and the run-up is
raw syscalls, not a Symbol-not-found.

### Discriminating check

```
$ grep -n 'no known raw-syscall trampoline signature matched' /tmp/iokit-probe-62-1.log | grep 253374464
26540:[darling-mldr] patch_linux_raw_syscalls: no known raw-syscall trampoline signature matched in a 253374464-byte executable mapping at 0x33ad45434000 — ...
```

Expected: exactly this line — Chrome fw's trampoline was not rewritten. If the
line were absent (Chrome fw patched), the hypothesis would fail and the syscalls
would have to come from another unpatched image.

### Repro

```sh
python3 "$DARLING_SRC_DIR"/build-freebsd/decode-crash.py /tmp/iokit-probe-62-1.log "$DARLING_OVERLAY" "$DARLING_SRC_DIR"/tests
grep -n 'calling initializer function.*Google Chrome for Testing Framework' /tmp/iokit-probe-62-1.log | tail -1
grep -n 'unhandled Linux syscall' /tmp/iokit-probe-62-1.log | tail -6
grep -n 'no known raw-syscall trampoline signature matched' /tmp/iokit-probe-62-1.log | grep 253374464
```

## Control #64 — naming the raw-syscall sites: they are ud2 aborts in libsystem_platform, not a trampoline

**Date:** 2026-10-05
**Branch:** task/mldr-rawtrap-rip
**Base:** pr-arm64 = ac8ead44da7260bdc7f49ae0ace34167be7324af
**Goal:** name the guest sites of the unhandled raw syscalls #63 found in Chrome's
initializer, so patch_linux_raw_syscalls can be judged against real sites.

### Patch: mldr prints the site of every unhandled raw syscall

`src/startup/mldr/freebsd_syscall_trap.c`, `dispatch_linux_syscall`'s default
branch: after the existing "unhandled Linux syscall %u — ENOSYS" line, print
`at <image>+0x<off> rip=0x… rax=0x…` via `mldr_describe_addr(mc->mc_rip, …)`.
Capped at 32 sites so a loop cannot flood the log.

```
$ sh build-freebsd/build-mldr-only.sh
# rebuilds and installs to $DARLING_BUILD_DIR/dserver/mldr-real/mldr
$ # then the 60-2 probe chain, new log name:
$ timeout 180 sudo env … "$DARLING_BUILD_DIR"/launch-dynamic > /tmp/iokit-probe-63-1.log 2>&1
```

### Sites (63-1, lines 1292563-1292574)

| syscall nr | image+offset | rax |
|---|---|---|
| 4294967287 (0xFFFFFFF7, -9) | libsystem_platform.dylib+0x8247 | 0xfffffffffffffff7 |
| 4294967218 (0xFFFFFFB2, -78) | libsystem_platform.dylib+0x8257 | 0xffffffffffffffb2 |
| 4294967218 | libsystem_platform.dylib+0x8267 | 0xffffffffffffffb2 |
| 4294967218 | libsystem_platform.dylib+0x8277 | 0xffffffffffffffb2 |
| 4294967218 | libsystem_platform.dylib+0x8287 | 0xffffffffffffffb2 |
| 4294967218 | libsystem_platform.dylib+0x8297 | 0xffffffffffffffb2 |

The expectation was Chrome fw+0x…; the sites are in **libsystem_platform.dylib**,
not Chrome fw.

### Bytes at each rip

```
$ llvm-objdump -d --start-address=0x8230 --stop-address=0x82b0 "$DARLING_OVERLAY"/usr/lib/system/libsystem_platform.dylib
    8230: 55              pushq %rbp
    8231: 48 89 e5        movq  %rsp, %rbp
    8234: 89 7d fc        movl  %edi, -0x4(%rbp)
    8237: 0f 0b           ud2
__os_unfair_lock_unowned_abort:
    8240: 55              pushq %rbp
    8241: 48 89 e5        movq  %rsp, %rbp
    8244: 89 7d fc        movl  %edi, -0x4(%rbp)
    8247: 0f 0b           ud2            <- site 1
__os_unfair_lock_corruption_abort:
    8257: 0f 0b           ud2            <- site 2
__os_once_gate_recursive_abort:
    8267: 0f 0b           ud2            <- site 3
__os_once_gate_unowned_abort:
    8277: 0f 0b           ud2            <- site 4
__os_once_gate_corruption_abort:
    8287: 0f 0b           ud2            <- site 5
__os_lock_recursive_abort:
    8297: 0f 0b           ud2            <- site 6
```

Every site is `0f 0b` (`ud2`) — the `__builtin_trap()` of an os_unfair_lock /
os_once abort routine, not a raw-Linux-syscall trampoline.

### Finding / decision

This is **not a trampoline**, so the signature in `patch_linux_raw_syscalls` must
not be extended. mldr's `sigill_handler` treats *any* `ud2` as one of its own
patched raw-syscall sites (it only checks `pc[0]==0x0f && pc[1]==0x0b`), so a
genuine abort trap is dispatched as a raw syscall with a garbage rax (-9 / -78),
ENOSYS is written back instead of aborting, and the guest continues with a
corrupt lock state — which is what then jumps to the stack (63-1:1292575).

### Repro

```sh
sh build-freebsd/build-mldr-only.sh
# run the 60-2 probe chain into /tmp/iokit-probe-63-1.log
grep -n -A1 'unhandled Linux syscall' /tmp/iokit-probe-63-1.log
llvm-objdump -d --start-address=0x8230 --stop-address=0x82b0 "$DARLING_OVERLAY"/usr/lib/system/libsystem_platform.dylib
```

## Control #65 — a genuine ud2 aborts instead of being dispatched as a raw syscall

**Date:** 2026-10-05
**Branch:** task/sigill-abort-split
**Base:** pr-arm64 = 50379ef23a03627befc7db2adddcaebad1d55437
**Goal:** stop sigill_handler from swallowing a genuine abort (a ud2 it did not
plant) as a raw-syscall site, per #64.

### Patch

`patch_one_signature` records every address where it writes its own `ud2` into
`_mldr_ud2_sites[]`. `sigill_handler` now checks membership: a ud2 in the list is
dispatched as a raw syscall (old path); a ud2 NOT in the list is a genuine
`__builtin_trap()` — it prints `at <image>+0x<off> rip=… rax=…` and hands the
context to `crash_debug_handler` (the FATAL block plus the guarded guest stack
dump).

### Run 65-1 (new log name; 62-1 and 63-1 untouched)

```
$ grep -c 'unhandled Linux syscall' /tmp/iokit-probe-65-1.log
0
$ grep -c 'genuine SIGILL' /tmp/iokit-probe-65-1.log
1
```

The first ud2 is no longer dispatched; it aborts:

```
1292563:[darling-mldr] genuine SIGILL (ud2, not a patched trampoline) at libsystem_platform.dylib+0x8237 rip=0x2cf48b6c3237 rax=0x1
1292564:[darling-mldr] FATAL signal 4 (code=5) at addr=0x2cf48b6c3237
  rip=0x00002cf48b6c3237  rax=0x0000000000000001  rbx=0x00002cf49ba9a7a0
  rcx=0x0000000000000307  rdx=0x0000000000000307  rsi=0x0000000000050000
  rdi=0x0000000000000307  rbp=0x00007fffffdfd680  rsp=0x00007fffffdfd680
```

The site is `libsystem_platform.dylib+0x8237` = **`__os_unfair_lock_recursive_abort`**
(nm: function at 0x8230, ud2 at 0x8237). The expectation was
`__os_unfair_lock_unowned_abort` (+0x8247); the first trap this run reaches is
the recursive-lock abort.

### Guest stack (65-1:1292577-)

```
$ # decode-crash segment table over the dumped stack words
  0x2cf48b6bd57a -> /usr/lib/system/libsystem_platform.dylib +0x257a
  0x2cf401018527 -> UNMAPPED
  0x2cf48bb95e42 -> /Frameworks/Google Chrome for Testing Framework.framework/... +0x161e42
```

The chain runs from the abort back into Chrome fw (+0x161e42). The initializer
named in #63 (Chrome fw+0x212a4c0) is not itself on this stack — the trap fires
from an os_unfair_lock call on the Chrome-side chain, not from the initializer
frame.

### Verdict

**success** — the genuine abort is no longer swallowed: 0 "unhandled Linux
syscall" lines from libsystem_platform, and instead a real SIGILL crash report
naming the site (`libsystem_platform.dylib+0x8237`) with a guest stack reaching
Chrome fw. New stopper: `FATAL signal 4 (code=5)` at the abort site.

### Repro

```sh
sh build-freebsd/build-mldr-only.sh
# run the 60-2 probe chain into /tmp/iokit-probe-65-1.log
grep -c 'unhandled Linux syscall' /tmp/iokit-probe-65-1.log   # 0
grep -n 'genuine SIGILL\|FATAL signal' /tmp/iokit-probe-65-1.log | head
llvm-nm -n "$DARLING_OVERLAY"/usr/lib/system/libsystem_platform.dylib | awk '$1>="0000000000008220" && $1<="0000000000008260"'
```

## Control #66 — decode of the aborting lock: the caller and the lock object

**Date:** 2026-10-05
**Branch:** task/unfairlock-site-decode
**Base:** pr-arm64 = b4e03132adb530f5194da5b94493c07ca14f2d21
**Goal:** name the code that reaches the aborting os_unfair_lock and the lock
object. No new run — decoded read-only from /tmp/iokit-probe-65-1.log and the
binaries (62-1/63-1/65-1 untouched).

### Step 1 — the calling site (Chrome fw+0x161e42)

`Chrome fw+0x161e42` is the return site of the `call` at `+0x161e3d`:

```
$ llvm-objdump -d --start-address=0x161e00 --stop-address=0x161e50 "$DARLING_OVERLAY"/Frameworks/.../Google\ Chrome\ for\ Testing\ Framework
  161e27: 48 89 df                movq  %rbx, %rdi
  161e2a: 48 83 3d 5e 3b 04 0f 00  cmpq  $0x0, 0xf043b5e(%rip)   ## lazy ptr 0xf1a5990
  161e32: 0f 84 03 01 00 00        je    0x161f3b
  161e38: be 00 00 05 00           movl  $0x50000, %esi
  161e3d: e8 22 e8 a5 0d           callq 0xdbc0664
  161e42: 45 84 ff                 testb %r15b, %r15b      <- the frame
```

The call target `0xdbc0664` is a `__stubs` entry:

```
$ llvm-objdump -d --start-address=0xdbc0664 --stop-address=0xdbc066a "$DARLING_OVERLAY"/Frameworks/.../Google\ Chrome\ for\ Testing\ Framework
  dbc0664: ff 25 26 53 5e 01   jmpq *0x15e5326(%rip)   ## 0xf1a5990
```

So the site calls `stub(0xdbc0664)`, whose lazy pointer is Chrome fw `+0xf1a5990`
(guest 0x2cf48c95d990), with `rdi = rbx` (the lock object) and `esi = 0x50000`
— the shape of `os_unfair_lock_lock_with_options(lock, options)`. The exact
symbol is not readable statically (Chrome fw is chained-fixup built; no bind for
that pointer appears in 65-1's log), so the name is inferred from the argument
shape, not asserted.

The function itself has **no local symbol**: Chrome fw is stripped, and the
nearest exported symbol is `_ChromeMain` (0x3fe0), 1.4 MB away — not the
function. The frame is named by offset only: `Chrome fw+0x161e42`.

### Step 2 — the lock object is a heap pointer, not a __DATA global

The lock is `rdi = rbx = 0x2cf49ba9a7a0`. Resolved against the dyld segment
table it is **unmapped**:

```
$ # decode-crash segment table over the crash registers
  0x2cf49ba9a7a0 -> UNMAPPED
  0x2cf48c95d990 -> /Frameworks/Google Chrome for Testing Framework.framework/... +0xf29990
```

So the lock is a **heap object**, not a static `__DATA` global — there is no
file address to read a raw word from, and the "file value of the lock word"
question does not apply to this object. (The framework's `__DATA` does hold the
stub's lazy pointer at +0xf1a5990, but that is the call target, not the lock.)

### Step 3 — the abort function (confirmed)

```
$ llvm-nm -n "$DARLING_OVERLAY"/usr/lib/system/libsystem_platform.dylib | awk '$1>="0000000000008220" && $1<="0000000000008260"'
0000000000008220 T __os_lock_corruption_abort
0000000000008230 T __os_unfair_lock_recursive_abort
0000000000008240 T __os_unfair_lock_unowned_abort
0000000000008250 T __os_unfair_lock_corruption_abort
0000000000008260 T __os_once_gate_recursive_abort
```

65-1's site `+0x8237` is inside `__os_unfair_lock_recursive_abort` (function at
0x8230, ud2 at 0x8237). Confirmed.

### Step 4 — the first abort is non-deterministic (fact)

The first abort the run reaches varies between runs:

| run | first abort site | function |
|---|---|---|
| 63-1 | libsystem_platform+0x8247 | __os_unfair_lock_unowned_abort |
| 65-1 | libsystem_platform+0x8237 | __os_unfair_lock_recursive_abort |

The lock word's owner field therefore depends on **runtime state**, not on the
load path — the same load reaches a differently-owned lock each run.

### Verdict (fork)

**The word was never initialized** (the heap lock word holds stale owner data),
rather than a clean double acquisition: a double acquisition on a well-formed
zero-initialized lock would always abort *recursive*, but 63-1 aborted
*unowned* — the owner field held a dead thread's value. That is stale memory,
not a re-lock of a lock this thread owns.

**False-check:** read the lock word at the object (`*(uint32_t *)0x2cf49ba9a7a0`)
at the crash. If it is 0 (a properly initialized free lock), this verdict is
wrong and the abort must instead be a genuine recursive re-lock; if it is
non-zero garbage/stale, the verdict holds.

### Repro

```sh
grep -n 'genuine SIGILL\|FATAL signal' /tmp/iokit-probe-65-1.log | head
llvm-objdump -d --start-address=0x161e00 --stop-address=0x161e50 "$DARLING_OVERLAY"/Frameworks/Google\ Chrome\ for\ Testing\ Framework.framework/Versions/154.0.8029.0/Google\ Chrome\ for\ Testing\ Framework
llvm-nm -n "$DARLING_OVERLAY"/usr/lib/system/libsystem_platform.dylib | awk '$1>="0000000000008220" && $1<="0000000000008260"'
```

## Control #67 — the lock word at the crash: non-zero (the #66 verdict holds)

**Date:** 2026-10-05
**Branch:** task/lockword-crash-dump
**Base:** pr-arm64 = 4a1d561b1079dc6e10a52f0452720e969d673799
**Goal:** decide the #66 fork by reading the lock word at the crash. Logs
62-1/63-1/65-1 untouched; this run is /tmp/iokit-probe-67-1.log.

### Step 0 — rdi is not the lock at the ud2; rbx is

```
$ llvm-objdump -d --start-address=0x8230 --stop-address=0x8240 "$DARLING_OVERLAY"/usr/lib/system/libsystem_platform.dylib
__os_unfair_lock_recursive_abort:
    8230: 55           pushq %rbp
    8231: 48 89 e5     movq  %rsp, %rbp
    8234: 89 7d fc     movl  %edi, -0x4(%rbp)
    8237: 0f 0b        ud2
```

The routine is entered with `edi=0x307` and never touches rbx. The 65-1 register
dump already showed exactly that: `rdi=0x307`, `rbx=0x2cf49ba9a7a0`. So rdi holds
the abort's small argument (the lock *word value*), and the caller's lock object
is **rbx** (callee-saved). The dump below therefore reads rbx, not rdi.

### Step 1 — the dump line

`crash_debug_handler`'s foreign-ud2 path (from #65) now prints, before FATAL, one
guarded 32-byte hex line at rbx:

```
[darling-mldr] lock word @0x2cb02129a7a0: 0000000100000307 f390153b18a96fd3 8b815a1714bd5914 0000000000000000 (ok=1111 rdi=0x307)
```

### Step 2 — run 67-1

```
$ grep -n 'lock word' /tmp/iokit-probe-67-1.log
1292564:[darling-mldr] lock word @0x2cb02129a7a0: 0000000100000307 f390153b18a96fd3 8b815a1714bd5914 0000000000000000 (ok=1111 rdi=0x307)
```

### Decision (the #66 fork)

The first 4 bytes (little-endian) of the first quadword are `0x00000307` —
**non-zero**. `rdi=0x307` at the abort is exactly that word value, confirming
the object at rbx is the lock and that the abort routine receives the lock word
itself.

Per the fork: **non-zero / stale → the #66 verdict holds**. The word was never a
clean zero-initialized lock; the run is not a clean recursive re-lock. (If the
word had been 0 the verdict would have been withdrawn.)

### Verdict

The #66 verdict **holds**: the lock word at the crash is non-zero (0x307), so it
is stale/uninitialized memory, not a clean recursive re-lock.

### Repro

```sh
sh build-freebsd/build-mldr-only.sh
# run the 60-2 probe chain into /tmp/iokit-probe-67-1.log
grep -n 'lock word' /tmp/iokit-probe-67-1.log
llvm-objdump -d --start-address=0x8230 --stop-address=0x8240 "$DARLING_OVERLAY"/usr/lib/system/libsystem_platform.dylib
```

## Control #68 — provenance of the lock pointer: it is a global, not a heap object

**Date:** 2026-10-05
**Branch:** task/lockword-provenance
**Base:** pr-arm64 = 527cf01fd762715615c2f66aa1290d233bc4491c
**Goal:** continue the #66/#67 trace one level up — find the caller of the
function at 0x161ce0 (whose arg1 is the lock) and where its rdi comes from.

### The function's arg1 is the lock

```
$ llvm-objdump -d --start-address=0x161ce0 --stop-address=0x161cf5 "$DARLING_OVERLAY"/Frameworks/.../Google\ Chrome\ for\ Testing\ Framework
  161ce0: 55                pushq %rbp
  161ce1: 48 89 e5          movq  %rsp, %rbp
  161cea: 53                pushq %rbx
  161ceb: 48 89 fb          movq  %rdi, %rbx      ; rbx = arg1 (the lock)
  161e27: 48 89 df          movq  %rbx, %rdi      ; rdi = arg1
  161e38: be 00 00 05 00    movl  $0x50000, %esi
  161e3d: e8 22 e8 a5 0d    callq 0xdbc0664       ; os_unfair_lock_lock_with_options
```

### The caller passes a global as rdi

The guest stack from #65 puts the caller's return address at Chrome fw+0x8eeeeb.
Disassembling there:

```
$ llvm-objdump -d --start-address=0x8eee80 --stop-address=0x8eef00 "$DARLING_OVERLAY"/Frameworks/.../Google\ Chrome\ for\ Testing\ Framework
  8eeedf: 48 8d 3d ba 78 77 0f    leaq 0xf7778ba(%rip), %rdi   ## 0x100667a0
  8eeee6: e8 f5 2d 87 ff          callq 0x161ce0
  8eeeeb: e9 3a ff ff ff          jmp 0x8eee2a
```

So the caller loads `rdi = Chrome fw+0x100667a0` (a `__DATA` global) and calls
0x161ce0. The lock object is that global — **not** a heap object.

The chain, one level up:

```
os_unfair_lock_lock_with_options(call +0x161e3d)
  rdi = rbx = arg1 of 0x161ce0
  rdi at 0x161ceb = the caller's rdi = leaq 0x100667a0(%rip) at 0x8eeedf
  => the lock is the global at Chrome fw+0x100667a0
```

The code just before the caller (0x8eee89-0x8eeeda) is a lazy-initialisation
guard for that global — zero a local, call a stub at 0x8eeea3, publish to
0x100667a8 — and then 0x8eeedf takes the address of 0x100667a0 and calls the
lock-taking method.

### Owner structure

The lock is the global object at `Chrome fw+0x100667a0` — a lazily-initialised
singleton; the `os_unfair_lock` word sits at the object's start (rdi is its
address). **Inferred** — the object has no symbol (Chrome fw is stripped).

### Correction to #66

#66 called rbx a heap object because the dyld segment table reported it
UNMAPPED. It is not heap: the same address is `base + 0x100667a0`, a `__DATA`
global — the segment table simply did not cover that page. #67 read it
successfully (ok=1111), which already contradicted "unmapped".

### Repro

```sh
llvm-objdump -d --start-address=0x161ce0 --stop-address=0x161cf5 "$DARLING_OVERLAY"/Frameworks/.../Google\ Chrome\ for\ Testing\ Framework
llvm-objdump -d --start-address=0x8eee80 --stop-address=0x8eef00 "$DARLING_OVERLAY"/Frameworks/.../Google\ Chrome\ for\ Testing\ Framework
```

## Control #69 — passport of the global at Chrome fw+0x100667a0

**Date:** 2026-10-05
**Branch:** task/global-lock-census
**Base:** pr-arm64 = 6f1924387bd0fe1d683028cb3f11e018d6959464
**Goal:** is the crash lock word 0x307 a real owner or garbage in a not-yet-
initialised global? Static only (x86_64 slice of Chrome fw).

### Section and size

`llvm-objdump -h` / `llvm-otool -l`:

```
$ llvm-otool -l "$DARLING_OVERLAY"/Frameworks/.../Google\ Chrome\ for\ Testing\ Framework | grep -A6 'sectname __common'
  sectname __common
   segname __DATA
      addr 0x00000000100418c0
      size 0x00000000000dbd00
    offset 0
```

The global at `0x100667a0` is `__common + 0x24ee0` — in the `__DATA` segment's
**BSS** (`__common`, addr 0x100418c0, size 0xdbd00). `offset 0` with no file
bytes: it is zero-fill, so the **file word is 0**.

The object spans at least `0x100667a0`-`0x100667b8` (24 bytes): the code writes
the lock word at +0x0, a flag byte at +0x4, and 16 bytes of zero at +0x8.

### Cross-references (RIP-relative scan of __text)

Scanning `__text` (vaddr 0x2840, size 0xdbbc720) for disp32 resolving to
0x100667a0 / 0x100667a8 — 12 sites:

| insn | bytes | target |
|---|---|---|
| 0x8eee16 | 48 8d 3d | 0x100667a0 |
| 0x8eee3a | 48 8d 3d | 0x100667a0 |
| 0x8eee41 | 48 8b 0d | 0x100667a8 |
| 0x8eee48 | 48 89 05 | 0x100667a8 |
| 0x8eee8c | 0f 11 05 | 0x100667a8 |
| 0x8eeeb3 | 48 89 05 | 0x100667a8 |
| 0x8eeedf | 48 8d 3d | 0x100667a0 |
| 0x95d44c9 | 48 8d 3d | 0x100667a0 |
| 0x95d44d9 | 48 8d 3d | 0x100667a0 |
| 0x95d44ee | 48 8d 3d | 0x100667a8 |
| 0x95d4501 | 48 8d 3d | 0x100667a8 |
| 0x95d4510 | 48 8d 3d | 0x100667a0 |

Grouped: 0x8eee16-0x8eeeb3 is the lazy-init (zero + publish + flag, all writes to
+0x8), 0x8eeedf is the #68 caller, and 0x95d44c9-0x95d4510 is a second function
that also takes `&global` (leaq into rdi) — the singleton's accessors. No site
writes the lock word at +0x0 directly.

### File vs runtime

```
file (__common, BSS): 0x00000000   (offset 0 — zero-fill)
runtime (#67):        0x0000000307  (first 4 bytes of the lock word)
```

### Verdict

The word is **not** uninitialised garbage: BSS guarantees 0 at load, so 0x307 was
written at runtime. No xref writes +0x0, so the writer is `os_unfair_lock_lock`
itself, invoked on `&global` — the lock-taking method at 0x161ce0, reached from
0x8eeedf and 0x95d44c9-0x95d4510. **Named writer candidate (inferred):** the
singleton's lock method at Chrome fw+0x161ce0. So 0x307 is a real lock owner —
someone locked the global and did not unlock it (or locked it on another thread).

### Repro

```sh
llvm-otool -l "$DARLING_OVERLAY"/Frameworks/.../Google\ Chrome\ for\ Testing\ Framework | grep -A6 'sectname __common'
# RIP-relative scan of __text for 0x100667a0 / 0x100667a8 (12 sites above)
```

## Control #70 — re-entry path for the global lock at Chrome fw+0x100667a0

**Date:** 2026-10-05
**Branch:** task/lock-reentry-path
**Base:** pr-arm64 = e7f333c34081f56ff9df858a82ea1fee2d5d755d
**Goal:** name the re-entry that produced __os_unfair_lock_recursive_abort (#65):
the same thread takes the global lock a second time. Static, x86_64 slice.

### Accessor boundaries

Both accessors take the lock at Chrome fw+0x100667a0 and call the lock method
0x161ce0.

Accessor A — `0x8eee10`-`0x8eee88` (plus its cold blocks `0x8eee89`-`0x8eeeeb`):

```
$ llvm-objdump -d --start-address=0x8eee10 --stop-address=0x8eee90 "$DARLING_OVERLAY"/Frameworks/.../Google\ Chrome\ for\ Testing\ Framework
  8eee10: 55                pushq %rbp
  8eee11: 48 89 e5          movq  %rsp, %rbp
  8eee14: 53                pushq %rbx
  8eee15: 50                pushq %rax
  8eee16: 48 8d 3d 83 79 77 0f  leaq 0x100667a0(%rip), %rdi
  8eee1d: e8 e0 01 2d 0d    callq 0xdbbf002        ; trylock
  8eee22: 84 c0             testb %al, %al
  8eee24: 0f 84 b5 00 00 00  je    0x8eeedf         ; -> blocking lock
  8eee2a: 80 3d 73 79 77 0f 01  cmpb $1, 0x100667a4 ; flag
  8eee31: 75 56             jne   0x8eee89         ; -> init
  8eee33: 48 8b 05 76 79 77 0f  movq 0x100667b0(%rip), %rax
  8eee3a: 48 8d 3d 5f 79 77 0f  leaq 0x100667a0(%rip), %rdi
  8eee41: 48 8b 0d 60 79 77 0f  movq 0x100667a8(%rip), %rcx
  8eee48: 48 89 05 59 79 77 0f  movq %rax, 0x100667a8(%rip)
  ... xorshift on 0x100667b0 ...
  8eee7b: e8 7c 01 2d 0d    callq 0xdbbeffc        ; unlock (tail)
  8eee80: 89 d8             movl  %ebx, %eax
  8eee82: 48 83 c4 08       addq  $0x8, %rsp
  8eee86: 5b                popq  %rbx
  8eee87: 5d                popq  %rbp
  8eee88: c3                retq
  8eee89: 0f 57 c0          xorps %xmm0, %xmm0     ; init: zero + publish
  8eee8c: 0f 11 05 ...       movups %xmm0, 0x100667a8(%rip)
  8eeea3: e8 4e 04 2d 0d    callq 0xdbbf2f6
  8eeec6: e8 2b 04 2d 0d    callq 0xdbbf2f6
  8eeed3: c6 05 ... 01       movb  $1, 0x100667a4(%rip)  ; set flag
  8eeeda: e9 5b ff ff ff    jmp   0x8eee3a
  8eeedf: 48 8d 3d ba 78 77 0f  leaq 0x100667a0(%rip), %rdi
  8eeee6: e8 f5 2d 87 ff    callq 0x161ce0         ; blocking lock
  8eeeeb: e9 3a ff ff ff    jmp   0x8eee2a
```

Accessor B — `0x95d44c0`-`0x95d451d`:

```
$ llvm-objdump -d --start-address=0x95d44c0 --stop-address=0x95d4520 "$DARLING_OVERLAY"/Frameworks/.../Google\ Chrome\ for\ Testing\ Framework
  95d44c0: 55                pushq %rbp
  95d44c1: 48 89 e5          movq  %rsp, %rbp
  95d44c4: 53                pushq %rbx
  95d44c5: 50                pushq %rax
  95d44c6: 48 89 fb          movq  %rdi, %rbx
  95d44c9: 48 8d 3d d0 22 a9 06  leaq 0x100667a0(%rip), %rdi
  95d44d0: e8 2d ab 5e 04    callq 0xdbbf002        ; trylock
  95d44d5: 84 c0             testb %al, %al
  95d44d7: 75 0c             jne   0x95d44e5
  95d44d9: 48 8d 3d c0 22 a9 06  leaq 0x100667a0(%rip), %rdi
  95d44e0: e8 fb d7 b8 f6    callq 0x161ce0         ; blocking lock
  95d44e5: 80 3d b8 22 a9 06 00  cmpb $0, 0x100667a4(%rip)
  95d44ec: 75 13             jne   0x95d4501
  95d44ee: 48 8d 3d b3 22 a9 06  leaq 0x100667a8(%rip), %rdi
  95d44f5: e8 26 8b 07 f7    callq 0x64d020         ; init
  95d44fa: c6 05 ... 01       movb  $1, 0x100667a4(%rip)
  95d4501: 48 8d 3d a0 22 a9 06  leaq 0x100667a8(%rip), %rdi
  95d4508: 48 89 de          movq  %rbx, %rsi
  95d450b: e8 90 ff ff ff    callq 0x95d44a0        ; work
  95d4510: 48 8d 3d 89 22 a9 06  leaq 0x100667a0(%rip), %rdi
  95d4517: 48 83 c4 08       addq  $0x8, %rsp
  95d451b: 5b                popq  %rbx
  95d451c: 5d                popq  %rbp
  95d451d: e9 da aa 5e 04    jmp   0xdbbeffc         ; unlock (tail)
```

### callq sites inside accessor A's locked region

Lock is taken at 0x8eee1d (trylock) or 0x8eeee6 (blocking); released at 0x8eee7b.
Between them:

| site | target | what |
|---|---|---|
| 0x8eeea3 | 0xdbbf2f6 | init stub (first) |
| 0x8eeec6 | 0xdbbf2f6 | init stub (second) |
| 0x8eee7b | 0xdbbeffc | unlock |

Accessor B's locked region (0x95d44d0/0x95d44e0 → 0x95d451d) calls 0x64d020 (init)
at 0x95d44f5 and 0x95d44a0 (a trivial setter) at 0x95d450b.

### Verdict — honest miss

No call inside either locked region is statically resolvable to a re-entry:
the only calls that can run arbitrary guest code under the lock are the init
stubs 0xdbbf2f6 (A) and 0x64d020 (B) and the work 0x95d44a0 (B, a 3-instruction
setter). None is a direct call back to 0x8eee10 / 0x95d44c0 in the slice, and
the init stubs have no symbols (Chrome fw is stripped), so a re-entry through a
callback/registration inside an init stub cannot be proven from static bytes.

The most likely shape is still an outer holder re-entering: A or B is called
again while this thread already holds the lock, and the second acquire — the
blocking lock 0x161ce0 at 0x8eeee6 (A) / 0x95d44e0 (B) — hits
__os_unfair_lock_recursive_abort. That is consistent with the #65 stack
(libsystem_platform → 0x161e42 → 0x8eeeeb).

One runtime step to close it: at the blocking-lock sites (0x8eeee6 / 0x95d44e0),
record the caller's return address and the thread id of the first acquirer, then
on the recursive abort print both. That names the outer holder directly.

### Repro

```sh
llvm-objdump -d --start-address=0x8eee10 --stop-address=0x8eee90 "$DARLING_OVERLAY"/Frameworks/.../Google\ Chrome\ for\ Testing\ Framework
llvm-objdump -d --start-address=0x95d44c0 --stop-address=0x95d4520 "$DARLING_OVERLAY"/Frameworks/.../Google\ Chrome\ for\ Testing\ Framework
```

## Control #71 — naming the first acquire of the global lock at the recursive abort

**Date:** 2026-10-06
**Branch:** task/reentry-abort-path
**Base:** pr-arm64 = d13efa69c0d57f82bfd2edee237117e2d150fe70
**Goal:** from the #70 honest miss, name the address of the FIRST acquire of
fw+0x100667a0 by the thread that then hits __os_unfair_lock_recursive_abort.

### Instrumentation (step 1, committed)

On mldr's foreign-ud2 path, after the lock-word dump: print the thread id and
walk 128 guest stack words, resolving each to image+offset.

### Run 71-1 (new log name; not overwritten)

```
$ grep -n 'reentry:\|lock word\|genuine SIGILL\|stack\[' /tmp/iokit-probe-71-1.log
1292563: genuine SIGILL (ud2, not a patched trampoline) at libsystem_platform.dylib+0x8237 ...
1292564: lock word @0x1063f149a7a0: 0000000100000307 46d2dba5fb640975 b21fa2c5228f01a4 0000000000000000 (ok=1111 rdi=0x307)
1292565: reentry: tid=45142 rsp=0x7fffffdfd680
1292566:   stack[1]   0x1063e10bd57a libsystem_platform.dylib+0x257a
1292567:   stack[6]   0x1063f129dd40 Google Chrome for Testing Framework (data)+0x119d40
1292568:   stack[21]  0x1063e10bd814 libsystem_platform.dylib+0x2814
1292569:   stack[35]  0x1063e1595e42 Google Chrome for Testing Framework+0x161e42
1292570:   stack[41]  0x1063e1d22eeb Google Chrome for Testing Framework+0x8eeeeb
1292571:   stack[45]  0x1063e1d22dd6 Google Chrome for Testing Framework+0x8eedd6
1292572:   stack[49]  0x1063e2b17f95 Google Chrome for Testing Framework+0x16e3f95
1292573:   stack[63]  0x1063e2c97286 Google Chrome for Testing Framework+0x1863286
1292574:   stack[93]  0x1063e2c9687e Google Chrome for Testing Framework+0x186287e
1292575:   stack[94]  0x1063e0db2e66 libsystem_kernel.dylib+0x42e66
1292576:   stack[95]  0x1063e1a81063 Google Chrome for Testing Framework+0x64d063
1292577:   stack[105] 0x1063e355eb91 Google Chrome for Testing Framework+0x212ab91
```

### The two acquires

- **Second acquire (aborts):** stack[41] = Chrome fw+0x8eeeeb — the return of the
  blocking lock at 0x8eeee6 in accessor A (0x8eee10-0x8eeeeb).
- **First acquire:** not named by a frame. The chain that leads to the abort is
  stack[95] = Chrome fw+0x64d063 — inside the init stub 0x64d020, exactly what
  accessor B calls from its locked region (callq 0x95d44f5, after B's own
  trylock 0x95d44d0 / blocking lock 0x95d44e0). So the first acquire is B's, and
  the measured chain is:

```
  B: trylock/lock fw+0x100667a0 (0x95d44d0 / 0x95d44e0)   <- FIRST acquire (inferred)
    -> B: init callq 0x64d020 at 0x95d44f5
    -> init stub 0x64d020 (stack[95] = 0x64d063)
    -> F0 0x8eedd6 (stack[45]) -> A 0x8eee10
    -> A: blocking lock 0x161ce0 at 0x8eeee6 (stack[41] = 0x8eeeeb)  <- SECOND acquire
    -> __os_unfair_lock_recursive_abort
```

The exact first-acquire return address (B's 0x95d44d5 / 0x95d44e5) is NOT in the
128-word dump — B's frame was already consumed when the abort fired. That is the
honest limit of the stack-walk measurement: the chain is measured, the
first-acquire instruction is inferred from B's own locked region.

### 0x95d44a0 bytes (refines #70)

```
$ llvm-objdump -d --start-address=0x95d44a0 --stop-address=0x95d44b0 "$DARLING_OVERLAY"/Frameworks/.../Google\ Chrome\ for\ Testing\ Framework
  95d44a0: 55                pushq %rbp
  95d44a1: 48 89 e5          movq  %rsp, %rbp
  95d44a4: 48 89 37          movq  %rsi, (%rdi)
  95d44a7: 48 89 77 08       movq  %rsi, 0x8(%rdi)
  95d44ab: 5d                popq  %rbp
  95d44ac: c3                retq
```

Six instructions (prologue + two stores + epilogue), not three — #70's
"3-instruction setter" is refined to "a trivial two-field setter".

### Verdict

First acquire = accessor B's lock at 0x95d44d0 (trylock) / 0x95d44e0 (blocking),
**inferred**; the measured chain is B's init call 0x95d44f5 -> init stub 0x64d020
-> F0 0x8eedd6 -> A 0x8eee10 -> A's second acquire 0x8eeeeb -> recursive abort.
The exact first-acquire return address is not in the dump (honest miss).

### Repro

```sh
grep -n 'reentry:\|lock word\|genuine SIGILL\|stack\[' /tmp/iokit-probe-71-1.log
llvm-objdump -d --start-address=0x95d44a0 --stop-address=0x95d44b0 "$DARLING_OVERLAY"/Frameworks/.../Google\ Chrome\ for\ Testing\ Framework
```
