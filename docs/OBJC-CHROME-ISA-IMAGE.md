# Control #165 step 44 — chrome's isa binds to MY stub: the ordinal resolution, and what it excludes

**Date:** 2026-10-13
**Branch:** task/isa-import-resolve (base: task/chain-synthesis = 86c0a6a63)
**Method:** offline, one new tool `build-freebsd/resolve-chained-import.py` +
two `llvm-nm` cross-checks. No build, no run. This is the measurement the
synthesis (`docs/OBJC-EMPTY-FORWARD-SYNTHESIS.md` §2) named to separate the
three fix candidates.

## 0. Result — measured: the provider is `/usr/lib/CoreFoundationExtras.dylib`, and only it

Resolving the chrome framework's chained-fixups import ordinal 0x33d (the
ordinal Control #156 read out of the isa bytes at framework file offset
0xfe712e8):

```
$ python3 build-freebsd/resolve-chained-import.py "$DARLING_OVERLAY/Frameworks/Google Chrome for Testing Framework.framework/Versions/154.0.8029.0/Google Chrome for Testing Framework" 0x33d
fixups: dataoff=0xfe73000 datasize=0x150c0 version=0 starts_offset=0x20
        imports_offset=0x1a08 symbols_offset=0x4428 imports_count=2696
        imports_format=1 (IMPORT)
import[0x33d] = lib_ordinal 1 (0x1), weak=0, name=_OBJC_CLASS_$_NSConstantDictionary
  -> provided by: /usr/lib/CoreFoundationExtras.dylib
  -> dylib list has 66 entries
```

Cross-checks:

```
$ llvm-nm $DARLING_OVERLAY/System/Library/Frameworks/CoreFoundation.framework/Versions/A/CoreFoundation | grep -c _OBJC_CLASS_$_NSConstantDictionary
0
$ llvm-nm $DARLING_OVERLAY/usr/lib/CoreFoundationExtras.dylib | grep _OBJC_CLASS_$_NSConstantDictionary
0000000000002050 S _OBJC_CLASS_$_NSConstantDictionary
```

1. Chrome's receiver class is bound to **`CoreFoundationExtras.dylib`, my own
   generated stub image**, at link ordinal 1 (the first dylib in its 66-entry
   closure), weak=0.
2. The overlay's **real CoreFoundation does not export the class at all**
   (0 definitions) — so the stub is not shadowing a real export; it is the
   only provider in this tree.
3. The stub's class is the empty root class of the whole chain (synthesis P2):
   flags 0x2, super 0, baseMethods 0.

## 1. What this excludes, and what remains

Of the synthesis §2's three candidates:

- **Excluded: "chrome should bind the real CoreFramework class" (fix 3).**
  There is no real class in the closure to bind to (cross-check 2); chrome's
  link closure genuinely resolves the symbol to the stub. Nothing to unshadow.
- **Remaining and now single-valued: the stub's class shape (fix 1).**
  The class chrome binds has no methods AND **no superclass** — every send on
  it (and on its metaclass) misses to the residue chain of P3–P5. On macOS
  `NSConstantDictionary` is an `NSObject` subclass; as a root class it is
  outside every method table in the overlay. Giving the generated stub a real
  superclass chain (`NSObject` — present in the overlay's Foundation) makes
  `-description` resolve to `NSObject`'s real method, which returns a real
  object — the send never reaches the residue path at all.
- **Remaining as the track primitive: the handler (fix 2).** Independent of
  this class, the guest still returns uninitialized RAX for any unresolvable
  send (P4–P5). That is worth fixing on its own for the track — it turns every
  future "class has no method" into a loud stop instead of a landmine for the
  next send — but it is no longer the fix that unblocks THIS send.

## 2. What is still not proven

- That re-generating the stub with a real superclass resolves chrome's send:
  the generation code is in the tree (the CoreFoundationExtras producer), and
  the change must be measured by the same run recipe (build the stub, run
  chrome-macho, compare the death) — not assumed.
- That nothing else dies right after: the run has never passed this send, so
  no later blocker is observed. Both unknowns are named, not guessed.

## 3. Repro

```sh
export PATH=/usr/local/bin:/usr/local/sbin:/usr/bin:/bin:/sbin:/usr/sbin
python3 $DARLING_SRC_DIR/build-freebsd/resolve-chained-import.py \
    "$DARLING_OVERLAY/Frameworks/Google Chrome for Testing Framework.framework/Versions/154.0.8029.0/Google Chrome for Testing Framework" 0x33d
# imports_format=1 (IMPORT): struct dyld_chained_import { lib_ordinal:8,
# weak_import:1, name_offset:23 } — dyld mach-o/fixup-chains.h; the tool
# seeks, it never reads the whole 267 MB image.
llvm-nm $DARLING_OVERLAY/System/Library/Frameworks/CoreFoundation.framework/Versions/A/CoreFoundation | grep -c _OBJC_CLASS__\$NSConstantDictionary
llvm-nm $DARLING_OVERLAY/usr/lib/CoreFoundationExtras.dylib | grep _OBJC_CLASS_\$NSConstantDictionary
```

## 4. Artifacts

- `build-freebsd/resolve-chained-import.py` — the seek-based ordinal resolver.
- This doc.
- No build, no run.
