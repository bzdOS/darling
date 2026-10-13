# Control #167 step 46 — the stub's own methods: the fix that makes chrome's death unreachable

**Date:** 2026-10-13
**Branch:** task/stub-methods-fix (base: pr-arm64 = 93c5a3bc9)
**Method:** generator edit + overlay rebuild + live probe run, in that order.

Follows Control #166 (failed: inheriting NSObject makes the guest refuse to
load the wrapper — `Symbol not found: _OBJC_METACLASS_$_NSObject`) and the
three live measurements behind the choice (the orchestrator's
dispatch of 2026-10-13 06:41): (F1) the guest binds both NSObject class symbols from an
EXECUTABLE linked against libobjc (cls=base+0x5c050, meta=base+0x5c000);
(F2) a DYLIB with the same references keeps them as unbound imports and does
not load — the wrapper link is not anomalous, the exe/dylib asymmetry is a
measured guest fact; (F3) the failing name was the metaclass's.

## 0. The change — exactly the named volume

`build-freebsd/build-extras-objc-classes.py`, class-emission block only: every
stub class keeps its root-class shape and gains three real methods —

```
- (id)description          { return self; }
- (id)copyWithZone:(void*) { (void)zone; return self; }
- (id)copy                 { return self; }
```

No link-line change, no import of libobjc, no bind surface at all — so the
load-time failure class that killed step 45 is physically unreachable.

One further edit in the same generator, required by the rebuild itself: the
restored wrappers export `__objc_empty_cache` (injected by the original
build), so the rebuild's C-stub pass re-defined it and collided with the
locally built `empty_cache.o` (`duplicate symbol: __objc_empty_cache`, all 13
wrappers refused). The keep-list now excludes that name.

## 1. Live acceptance — run BEFORE this write-up, acceptance named before the run

Command (the same as the series' step 38 recipe):

```sh
sh $DARLING_SRC_DIR/build-freebsd/build-constdict-desc-probe.sh   # re-stages the fixed CoreFoundationExtras
sh $DARLING_SRC_DIR/build-freebsd/run-constdict-desc-probe.sh 60
```

Expected, named before the run: the probe loads with no `Symbol not found`;
`method-list-count=3`; `description` returns the receiver (a mapped address,
`same-as-receiver=yes`) instead of the residue small integer; and the added
`[<return> copy]` send (chrome's actual death shape, Control #160) also
returns the receiver.

Measured (log `build/constdict-desc-probe-1013-065442.log`):

```
CONSTDICT stub method-list-count=3
CONSTDICT stub method[0]=copyWithZone: imp=0x78c36a65700
CONSTDICT stub method[1]=copy         imp=0x78c36a65720
CONSTDICT stub method[2]=description  imp=0x78c36a656e0
CONSTDICT stub responds(description)=1 getMethodImplementation=0x78c36a656e0
CONSTDICT where-stub-getimp ... fname=/usr/lib/CoreFoundationExtras.dylib sname=-[NSConstantDictionary description]
CONSTDICT stub receiver=0x7fffffdffd60
CONSTDICT stub description=0x7fffffdffd60 same-as-receiver=yes
CONSTDICT stub before-copy-send / after-copy-send
CONSTDICT stub copy-ret=0x7fffffdffd60 same-as-receiver=yes
```

Four acceptance points, four measured. The image's offline shape is unchanged
otherwise: export set identical to the pre-fix backup, `__objc_classlist`
still 2 entries, zero NSObject undefined symbols. The `desc-imp=…` acceptance
line's old `match=` comparison is now meaningless (the value is a real
object, not a residue pointer) — recorded as superseded by the two
`same-as-receiver` lines.

## 2. What this does and does not prove

- **Proves:** the send that poisoned chrome now resolves a real method; the
  dead `[<return> copy]` send resolves too. The measured chain of Controls
  #160–#165 is broken at its user-visible end with the smallest possible
  edit, and the load-time failure class of step 45 is structurally excluded.
- **Does NOT prove:** that chrome's run gets past this point — the chrome run
  itself is a separate, heavier measurement (the recipe and its acceptance
  line are the run log's death signature, same as before); nor that
  `description` returning `self` is semantically what chrome's code wants
  from `description` (it wants a printable object) — the next question after
  the run, not an assumption; nor anything about variant-b's corrupted-isa
  crash, which is a different path and still dies as measured (unchanged).

## 3. Artifacts

- `build-freebsd/build-extras-objc-classes.py` — the methods + the keep-list fix.
- `tests/objc-constdict-desc.m` — the probe gained the receiver print and the
  post-return `copy` send (chrome's death shape) with its two acceptance lines.
- Logs on disk (not committed): `build/constdict-desc-probe-1013-065442.log`.
- The rebuilt overlay wrappers are artifacts, backed up in
  `$DARLING_BUILD_DIR/extras-prestubclass-backup/`.
