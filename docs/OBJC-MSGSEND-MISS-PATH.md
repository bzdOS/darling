# Control #162 step 40 — the guest's msgSend miss path: API says forward, send returns a small integer

**Date:** 2026-10-13
**Branch:** task/msgSend-miss-path (base: task/fwd-handler-negative = d712643b4, Control #161, not yet merged)
**Base:** pr-arm64 = 0aebe4e87 (step 38 merged)
**Method:** new runtime probe `tests/objc-msgSend-imp-undoc.m` (build/run recipes in
`build-freebsd/{build,run}-msgSend-imp-undoc.sh`) + offline reading of the staged
images. Two runs: `build/msgSend-imp-undoc-1013-021123.log`,
`build/msgSend-imp-undoc-1013-021554.log` (identical results).

Continues Control #161: the API path reports `_objc_msgForward` for
`description` on the stub class, yet the send returns a value. This step
measures WHERE the two paths part.

## 0. Result — measured: the send path and the API path disagree, and the send path is not stock

Probe: fake receiver (isa = `NSConstantDictionary`, zeros after), one
`description` send, `class_getMethodImplementation` before and after, repeat
sends for stability, five more selectors for selector-independence, and a
real-object control.

```
SENDUNDOC stub-class=0x132bda867050 name=NSConstantDictionary super=(nil/root) meta=0
SENDUNDOC stub method-list-count=0
SENDUNDOC selref description=0x132bdc610eda dladdr=1 fname=/usr/lib/system/libsystem_trace.dylib fbase=0x132bdc60b000
SENDUNDOC regname description=0x132bdc610eda        <- selref == sel_registerName, name=description
SENDUNDOC pre  responds=0 getimp=0x132bda8b1e80 is_msgforward=yes   (dladdr: libobjc+0x48e80 _objc_msgForward)
SENDUNDOC send-desc-1 ret=0x32    dladdr=0 (not a mapped address)
SENDUNDOC post getimp=0x132bda8b1e80 is_msgforward=yes wrote-cache=no
SENDUNDOC send-desc-2 ret=0x32    (stable across repeats)
SENDUNDOC sel=class        ret=0x20
SENDUNDOC sel=hash         ret=0x1f
SENDUNDOC sel=length       ret=0x21
SENDUNDOC sel=isEqual:     ret=0x23
SENDUNDOC sel=retainCount  ret=0x26
SENDUNDOC control real-obj=0x7f9df817cb00 class=__NSCFDictionary ret=0x7f9df817c8e0  (sound)
undoc pre=0x132bda8b1e80 ret1=0x32 post=0x132bda8b1e80 ret2=0x32 stable=no
```

1. **The API path never sees what the send used.** `getMethodImplementation`
   returns `_objc_msgForward` before AND after the send, and the send
   "wrote-cache=no": no cache the API reads shows the value.
2. **The send returns a small integer, per-selector and stable** — 0x32,
   0x20, 0x1f, 0x21, 0x23, 0x26 for description/class/hash/length/isEqual:/
   retainCount; identical across two runs. It is not a mapped address
   (dladdr=0). No fatal, no signal.
3. **selector references are bound correctly**: `@selector(description)`
   (a selref slot the loader fills) equals `sel_registerName("description")`
   (0x132bdc610eda = libsystem_trace+0x5eda), and the name resolves. So
   the wrong value is not a broken selref binding.

Offline complements:

4. **CoreFoundationExtras.dylib has NO chained fixups** — the class image
   cannot be re-pointed by a bind chain in its own image
   (`chained-fixups-inspect.py` → "no LC_DYLD_CHAINED_FIXUPS").
5. **The stub class's inline cache is a valid single EMPTY bucket, in the
   image, not zero**: on disk at file offset 0x2050:
   `isa=0x2078 (metaclass), super=0 (root), cache.buckets=0x21d0,
   cache.mask=0 (capacity 1)`; the bytes at 0x21d0 are `imp=0, sel=0`
   (empty bucket). So the fast-path lookup reads one empty bucket, misses,
   and goes to `__objc_msgSend_uncached` — which is STOCK Apple: it calls
   `_lookUpImpOrForward` with `ecx=3` and `jmpq *%r11` to whatever IMP
   comes back.
6. **The guest's `_lookUpImpOrForward` carries a non-stock sentinel
   comparison**: its tail compares the found IMP against the 8 bytes at
   vmaddr `0x59070` (both rip-relative constants at `0x2d64d+7` and
   `0x2d6c5+7` resolve to 0x59070). By the image's section map, **0x59070
   is a `__got` slot** (__got spans 0x59008..0x590e8) — a bound imported
   symbol — while the address stock Apple's code uses for this comparison
   is `__objc_msgForward_impcache`, which in this image is the TEXT gadget
   at `0x48e4c` (`je __objc_msgForward_stret; jmp __objc_msgForward`).
   The comparison is therefore against whatever function that GOT slot
   binds at runtime, not against the impcache gadget.

So the send jumped to a real IMP (the guest did not crash on the jump), and
that IMP returned the small integer — which is consistent with the guest's
lookup returning an address other than the stock forward path for this
empty root class, and with step 38's pristine return being the SELECTOR
itself (libsystem_trace+0x5eda) in that run: the returned value's KIND
varies run to run (selector pointer vs small integer).

## 1. What stays open (the next Control)

Which GOT entry lives at 0x59070 at runtime, and what IMP the guest's
`_lookUpImpOrForward` returns for this class/selector (the thing the send
jumped to, which then returned 0x32). Both are runtime reads of the guest:
- the GOT slot's bound symbol (readable at runtime with a tiny probe
  dladdr'ing the slot's contents, or via the bind ordinates offline);
- the IMP the lookup returns (instrumentable by splitting the probe's send
  from the API call and dumping the returned IMP's address, since the send
  already returns it as the value — `ret1` = the raw rax).

## 2. Repro

```sh
export PATH=/usr/local/bin:/usr/local/sbin:/usr/bin:/bin:/sbin:/usr/sbin
export DARLING_SRC_DIR=<src> DARLING_OVERLAY=<overlay> DARLING_BUILD_DIR=<build>

# build + run the probe (acceptance line: undoc pre=... ret1=... post=... stable=...)
sh $DARLING_SRC_DIR/build-freebsd/build-msgSend-imp-undoc.sh
sh $DARLING_SRC_DIR/build-freebsd/run-msgSend-imp-undoc.sh 60

# offline complements
python3 $DARLING_SRC_DIR/build-freebsd/chained-fixups-inspect.py \
    $DARLING_OVERLAY/usr/lib/CoreFoundationExtras.dylib        # no chained fixups
llvm-nm   $DARLING_OVERLAY/usr/lib/libobjc.A.dylib | grep -i forward
llvm-objdump -h $DARLING_OVERLAY/usr/lib/libobjc.A.dylib       # __got = 0x59008..0x590e8
llvm-objdump -d $DARLING_OVERLAY/usr/lib/libobjc.A.dylib > /tmp/libobjc.asm
grep -n 'rip), %rax' /tmp/libobjc.asm | sed -n '1,3p'          # sentinel compares
python3 - <<'PY'                                                # the stub class's cache on disk
import struct
d=open("$DARLING_OVERLAY/usr/lib/CoreFoundationExtras.dylib","rb").read()
u64=lambda o:struct.unpack_from('<Q',d,o)[0]
o=0x2050
print('isa=%016x super=%016x cache.buckets=%016x cache.mask=%016x'%(u64(o),u64(o+8),u64(o+0x10),u64(o+0x18)))
o2=0x21d0
print('bucket imp=%016x sel=%016x'%(u64(o2),u64(o2+8)))
PY
```

## 3. Artifacts

- `tests/objc-msgSend-imp-undoc.m` — probe source.
- `build-freebsd/build-msgSend-imp-undoc.sh` — build recipe.
- `build-freebsd/run-msgSend-imp-undoc.sh` — run recipe (timeout, liveness, pid stop).
- Logs on disk (not committed): `build/msgSend-imp-undoc-1013-021123.log`,
  `build/msgSend-imp-undoc-1013-021554.log`.
