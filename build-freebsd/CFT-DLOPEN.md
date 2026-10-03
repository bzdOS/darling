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
