# Control #163 step 41 — the full guest forward chain, and CoreFoundation at its end

**Date:** 2026-10-13
**Branch:** task/fwd-chain-cf (base: pr-arm64 = e417791f1, step 40 merged)
**Method:** new runtime probe `tests/objc-got-slot-undoc.m` (build/run recipes in
`build-freebsd/{build,run}-got-slot-undoc.sh`) + the offline `__got` resolver
`build-freebsd/got-slot-resolve.py`. Two runs, identical structure:
`build/got-slot-undoc-1013-032308.log`, `build/got-slot-undoc-1013-032422.log`.

Closes Control #162's two open reads — the GOT slot's bound symbol and the IMP
the guest's lookup returns — and corrects Control #162 §0 item 6.

## 0. Result — measured: the guest's chain is STOCK through the lookup, and ends in CoreFoundation

```
GOTSLOT anchors objc_msgSend=0x30a2b9eb10c0 lookUpImpOrForward=0x30a2b9e964a0
GOTSLOT libobjc base=0x30a2b9e69000 (anchor - 0x480c0)
GOTSLOT slot-addr=0x30a2b9ec2070 value=0x30a2b9eb1e4c
GOTSLOT where-gotslot ... fname=/usr/lib/libobjc.A.dylib sname=_objc_msgForward_impcache saddr=<base+0x48e4c>
GOTSLOT impcache-gadget=<base+0x48e4c> equals-slot=yes
GOTSLOT stub-class=0x30a2b9e67050 name=NSConstantDictionary
GOTSLOT bucket=0x30a2b9eb4218 sel-slot=0x0 imp-stored=0x0
GOTSLOT lookup ret=0x30a2b9eb1e4c
GOTSLOT where-lookup ... sname=_objc_msgForward_impcache
GOTSLOT post getimp=0x30a2b9eb1e80 (__objc_msgForward) equals-lookup=no
GOTSLOT send ret=0xa
GOTSLOT handler-slot=0x30a2b9ec5258 value=0x30a2bab9d310
GOTSLOT where-handler ... fname=/System/Library/Frameworks/CoreFoundation.framework/Versions/A/CoreFoundation
                                        sname=_CF_forwarding_prep_0 saddr=<CF base+0x19d310>
```

1. **The GOT slot at libobjc vmaddr 0x59070 holds `_objc_msgForward_impcache`**
   (dladdr: `sname=_objc_msgForward_impcache`, `saddr = base+0x48e4c` — the TEXT
   gadget `je __objc_msgForward_stret; jmp __objc_msgForward`). So
   `lookUpImpOrForward`'s tail comparison against 0x59070 is **stock Apple
   behaviour**: the slot is a GOT entry that carries the impcache gadget's
   address because this build hides the symbol across translation units.
   Control #162 §0 item 6's "non-stock sentinel" reading is **corrected**: the
   comparison is stock, the guest's lookup is not patched.
2. **The guest's lookup returns the stock miss sentinel.** Called directly
   with the send path's exact register layout — `(rdi=obj, rsi=sel,
   rdx=class, ecx=3)`, from `__objc_msgSend_uncached`'s own prologue
   (0x48c10: saves all argument registers, `movq %r10,%rdx`, `movl $0x3,%ecx`,
   `callq _lookUpImpOrForward`, `jmpq *%r11`) — it returns
   `_objc_msgForward_impcache`. `class_getMethodImplementation` returns
   `__objc_msgForward` (base+0x48e80), the gadget's second instruction
   target: the same answer in two spellings.
3. **The fast path's bucket is a libobjc-internal empty bucket**
   (`class+0x10` → base+0x4b218 in this run, sel=0 imp=0), not the class
   image's in-file bucket at 0x21d0: the file state (buckets=0x21d0, mask=0,
   bucket imp=0/sel=0) is the pre-realization snapshot; after realization
   the cache points at runtime memory. Step 40's offline read stands as the
   *file* state and this as the *runtime* state — both recorded.
4. **The chain's last link, measured: `__objc_forward_handler` holds
   `_CF_forwarding_prep_0`** (libobjc data slot at base+0x5c258 → a pointer
   into CoreFoundation at its base+0x19d310; dladdr `sname=_CF_forwarding_prep_0`).
   So the forward path does NOT dead-end at the fatal default: CoreFoundation
   has installed a handler that runs. **The small integers the send returns
   (0x32 twice in Control #162's runs, 0xa in this run) are
   `_CF_forwarding_prep_0`'s return values for a stub-class receiver** —
   which also explains why the value varies run to run while the lookup
   before it is deterministic.

## 1. The complete guest chain for `[stub description]`, as measured

```
objc_msgSend fast path: isa → class → cache.buckets (libobjc empty bucket)
  → bucket.sel(0) ≠ sel → miss
__objc_msgSend_uncached → _lookUpImpOrForward(obj, sel, class, 3)
  → STOCK miss sentinel _objc_msgForward_impcache
jmp *impcache → je/jmp __objc_msgForward
  → movq __objc_forward_handler,%r11 → _CF_forwarding_prep_0 (CoreFoundation)
  → returns a small integer (0xa this run) as the message's value
```

And in chrome: `[arg1 description]` returned `Foundation+0x790B0`
(`&__NSCFCopyDescription2`) — the same handler's output for chrome's
receiver/class state, consistent with CoreFoundation's forward preparation
finding a real method or returning a code pointer for a warm, chained-fixup-
bound object. The residual is now a single named question, not a class of
artifacts.

## 2. What stays open (the next Control)

What `_CF_forwarding_prep_0` (CoreFoundation+0x19d310) does for a receiver
whose class is the CoreFoundationExtras empty stub — read offline from the
staged CoreFoundation, then confirm with a probe that hands it a receiver
shape matching chrome's (isa = the stub class, warm/realized) and prints
what it returns with dladdr.

## 3. Repro

```sh
export PATH=/usr/local/bin:/usr/local/sbin:/usr/bin:/bin:/sbin:/usr/sbin
export DARLING_SRC_DIR=<src> DARLING_OVERLAY=<overlay> DARLING_BUILD_DIR=<build>

# build + run the probe (acceptance: gotslot slot=<p> ; lookup ret=<p>)
sh $DARLING_SRC_DIR/build-freebsd/build-got-slot-undoc.sh
sh $DARLING_SRC_DIR/build-freebsd/run-got-slot-undoc.sh 60

# offline: which symbol a __got slot is reserved for by the file's bind info
# (libobjc.A.dylib is a fat binary; the tool picks the x86_64 slice)
python3 $DARLING_SRC_DIR/build-freebsd/got-slot-resolve.py \
    $DARLING_OVERLAY/usr/lib/libobjc.A.dylib 0x59070
# → this image's stream has no __got entries (0 bound pointers) — the slot is
#   filled at load; the runtime read above is the authority.

# offline: the gadget and the lookup's register contract, for the record
llvm-objdump -d --start-address=0x48e4c --stop-address=0x48e88 $OBJC   # impcache gadget
llvm-objdump -d --start-address=0x48c10 --stop-address=0x48c5b $OBJC  # uncached prologue
```

## 4. Artifacts

- `tests/objc-got-slot-undoc.m` — probe source (GOT slot, direct lookup, bucket, handler slot).
- `build-freebsd/{build,run}-got-slot-undoc.sh` — recipes.
- `build-freebsd/got-slot-resolve.py` — offline `__got` slot resolver (fat-aware).
- Logs on disk (not committed): `build/got-slot-undoc-1013-032308.log`,
  `build/got-slot-undoc-1013-032422.log`.
