# Control #160 step 38 — runtime probe: which §2 branch fires for [NSConstantDictionary description]

**Date:** 2026-10-10
**Branch:** task/objc-desc-imp-probe
**Base:** pr-arm64 = 093a07699 (step 37 merged)
**Method:** runtime probe `tests/objc-constdict-desc.m` built against the staged
overlay and run under the guest via `launch-dynamic`. Two runs: pristine
(no argument) and `stamp` (probe installs its own `objc_setForwardHandler`).

Continues step 37 (`docs/OBJC-MSGSEND-STUB-PATH.md`): the static analysis
enumerated every `lookUpImpOrForward` branch that returns an IMP without
raising (§2, branches #1–#5) but could not say which one fires for
`[dict description]` on the empty stub class `NSConstantDictionary`. This
step measures it.

---

## 1. The probe

`tests/objc-constdict-desc.m` (built by `build-freebsd/build-constdict-desc-probe.sh`,
run by `build-freebsd/run-constdict-desc-probe.sh`):

1. Prints ASLR anchors (runtime address − file offset = image load base):
   `_NSLog` (Foundation+0x790f0), `_CFStringGetLength` (CF+0xef8b0),
   `_objc_msgSend` (libobjc+0x480c0), `_OBJC_CLASS_$_NSConstantDictionary`
   (CoreFoundationExtras+0x2050), `_close` (libSystem+0xf706).
2. Controls: sends `description` to two REAL Foundation objects
   (`[NSDictionary dictionary]`, `[[NSDictionary alloc] init]`) — proves the
   probe's send path is sound.
3. The case under investigation: `objc_getClass("NSConstantDictionary")`,
   queries the runtime's own tables (method list, `respondsToSelector:`,
   `class_getMethodImplementation`), then sends `description` to a minimal
   fake instance (isa = class, zeros after) and prints the raw return.
4. Variant B: same fake but isa = a code pointer with a real prologue
   (`55 48 89 e5`) — the corrupted-receiver shape of step 34.

Every line carries the marker `CONSTDICT`; stdout is unbuffered so a FATAL
signal cannot lose markers. The acceptance line is
`desc-imp=<value> foundation_790b0=<expected> match=<yes|no>`.

## 2. Measured result (pristine run, log `constdict-desc-probe-1010-004005.log`)

```
CONSTDICT anchors NSLog=0x76d867610f0 CFStringGetLength=0x76d864ef8b0 objc_msgSend=0x76d858b10c0 CFE-NSConstantDictionary=0x76d85867050 close=0x76d873f9968
CONSTDICT empty-dict real-obj=0x7fb86bc7c970 class=__NSCFDictionary
CONSTDICT empty-dict before-send
CONSTDICT empty-dict after-send
CONSTDICT empty-dict description=0x7fb86bc7c410 (expected_foundation_790b0=0x76d867610b0 match=no)
CONSTDICT placeholder real-obj=0x7fb86bc7cfb0 class=__NSCFDictionary
CONSTDICT placeholder before-send
CONSTDICT placeholder after-send
CONSTDICT placeholder description=0x7fb86bc7cd60 (expected_foundation_790b0=0x76d867610b0 match=no)
CONSTDICT stub-class=0x76d85867050 name=NSConstantDictionary
CONSTDICT stub method-list-count=0
CONSTDICT stub responds(description)=0 getMethodImplementation=0x76d858b1e80
CONSTDICT where-stub-getimp value=0x76d858b1e80 dladdr=1 fname=/usr/lib/libobjc.A.dylib fbase=0x76d85869000 sname=_objc_msgForward saddr=0x76d858b1e80
CONSTDICT stub isa=0x76d85867050 class=NSConstantDictionary expected_foundation_790b0=0x76d867610b0
CONSTDICT stub before-send
CONSTDICT stub after-send
CONSTDICT stub description=0x76d87610eda
CONSTDICT stub value=0x76d87610eda dladdr=1 fname=/usr/lib/system/libsystem_trace.dylib fbase=0x76d8760b000 sname=sbuf_delete saddr=0x76d8760f050
CONSTDICT variant-b code-isa=0x826c41e10 prologue=55 48 89 e5
[darling-mldr] FATAL signal 11 (code=1) at addr=0x222225f8
  rip=0x0000076d85895975  rax=0x00000000222225f8  rbx=0x0000000826c42116
```

Image bases (anchor − file offset):

| image | base | from |
|---|---|---|
| Foundation | `0x76d867610f0 − 0x790f0` = `0x76d866e8000` | `_NSLog` |
| CoreFoundation | `0x76d864ef8b0 − 0xef8b0` = `0x76d86400000` | `_CFStringGetLength` |
| libobjc | `0x76d858b10c0 − 0x480c0` = `0x76d85869000` | `_objc_msgSend` |
| CoreFoundationExtras | `0x76d85867050 − 0x2050` = `0x76d85865000` | `_OBJC_CLASS_$_NSConstantDictionary` |
| libSystem | `0x76d873f9968 − 0xf706` = `0x76d873ea262` | `_close` |

## 3. Which §2 branch fires: **#4 — the forwarding IMP**

The runtime's own tables, queried before the send:

- `class_copyMethodList(stub)` → **0 methods** (empty root class, step 36).
- `class_respondsToSelector(stub, "description")` → **0**.
- `class_getMethodImplementation(stub, "description")` →
  `0x76d858b1e80` = libobjc+0x48e80 = **`_objc_msgForward`**
  (dladdr: `sname=_objc_msgForward`).

So the selector is not in the class and the lookup returns the forwarding
IMP — step 37 §2 **branch #4** (`leaq __objc_msgForward_impcache(%rip),
%r13` @ `0x2d6fa`). The send then jumps to `__objc_msgForward`
(libobjc+0x48e80), which is a two-instruction stub:

```
48e80: movq __objc_forward_handler(%rip), %r11
48e87: jmpq  *%r11
```

**Proof that forwarding really fires** (stamp run, log
`constdict-desc-probe-1009-234956.log`): the probe installs its own handler
via `objc_setForwardHandler` before any send, and the log shows

```
CONSTDICT stub before-send
CONSTDICT forward-handler-called self=0x7fffffdffd50 cmd=description
CONSTDICT stub after-send
CONSTDICT stub description=0x45
```

The handler is called with `self` = the fake instance and `cmd` =
`description` — the runtime routes the unresolvable selector through the
forward handler. (The `0x45` return is garbage in `rax` because the stamp
handler returns void; it only proves the call path.)

## 4. The returned value and the residual difference with chrome

Pristine run: `description = 0x76d87610eda`. Attribution:

- libsystem_trace base = `0x76d8760b000` (dladdr `fbase`).
- offset = `0x76d87610eda − 0x76d8760b000` = **0x5eda**.
- `0x5eda` lies in `__TEXT,__objc_classname` (VMA `0x5dc4..0x5de6`), inside
  the string `"OS_os_log"` — i.e. the default forward handler (installed by
  the environment, not by the probe) returned a pointer into a class-name
  string in libsystem_trace.dylib. It is **not** a code pointer and **not**
  `Foundation+0x790B0`.

`match=no` against chrome's measured `Foundation+0x790B0`
(`&__NSCFCopyDescription2`).

**Residual difference with chrome** (step 35: `[dict description]` returned
`Foundation+0x790B0`, a code pointer):

| | chrome (step 35) | this probe (step 38) |
|---|---|---|
| receiver | real object, isa = chained-fixup bind ordinal 0x33d | fake instance, isa = `NSConstantDictionary` class |
| cache state | warm (chrome had run a long time; the guest resolver/mldr path had populated or corrupted the cache) | cold (first send for this class in a fresh process) |
| branch that fired | a found-method branch (#1/#2/#3) or a cache hit whose IMP was `Foundation+0x790B0` — the code pointer | **#4 forwarding IMP** → `__objc_msgForward` → forward handler |
| returned | `Foundation+0x790B0` (code pointer) | `libsystem_trace+0x5eda` (data pointer, class-name string) |

So chrome's `Foundation+0x790B0` was **not** the forwarding path: with a
cold cache the same class has no method and no cached IMP, and the runtime
goes to forwarding. Chrome's code pointer is the guest cache/resolution
artifact classified in step 36 (classification b): the warm cache (or the
mldr/dyld resolution path) already held a `description → __NSCFCopyDescription2`
entry, so lookup found a "method" and forwarding never fired.

## 5. Variant B (isa = code pointer, step 34's corrupted receiver)

```
CONSTDICT variant-b code-isa=0x826c41e10 prologue=55 48 89 e5
[darling-mldr] FATAL signal 11 (code=1) at addr=0x222225f8
  rip=0x0000076d85895975  rax=0x00000000222225f8
```

- `rip = 0x76d85895975` = libobjc+0x2c975, inside
  `objc_class::demangledName(bool)` (libobjc+0x2c930) — the lookup read
  the code pointer as a class, dereferenced its "data" at `0x20(%r14)`,
  and crashed on the garbage pointer `0x222225f8`.
- This is outcome (c) of the dispatch: a code-pointer isa is not a class;
  the lookup dies inside libobjc (no forward handler is consulted, no
  `objc_fatal` raise — the crash is a raw SIGSEGV on a garbage pointer).

## 6. Repro

```sh
export PATH=/usr/local/bin:/usr/local/sbin:/usr/bin:/bin:/sbin:/usr/sbin
# DARLING_SRC_DIR — root of the darling source tree
# DARLING_OVERLAY — staged overlay (libobjc, Foundation, CoreFoundationExtras, …)
# DARLING_BUILD_DIR — scratch build dir (launch-dynamic lives here)
export DARLING_SRC_DIR=<src> DARLING_OVERLAY=<overlay> DARLING_BUILD_DIR=<build>

# build the probe (reuses build-real-macho-tests.sh staging; does NOT
# rebuild Foundation)
sh $DARLING_SRC_DIR/build-freebsd/build-constdict-desc-probe.sh

# pristine run (acceptance line: desc-imp=... match=no)
sh $DARLING_SRC_DIR/build-freebsd/run-constdict-desc-probe.sh 45

# stamp run (proves the forward handler is consulted)
sh $DARLING_SRC_DIR/build-freebsd/run-constdict-desc-probe.sh 45 stamp
```

Run rule (project card): the whole run is bounded by `timeout -k 5 300`;
after mldr dies the server must stay alive > 60 s (so mldr's death is the
probe's fault, not a collapsed server), then the server is stopped by pid.
The run script implements exactly this (`pgrep -x darlingserver`, printed,
killed; no `pkill`).

## 7. Artifacts

- `tests/objc-constdict-desc.m` — probe source.
- `build-freebsd/build-constdict-desc-probe.sh` — build recipe.
- `build-freebsd/run-constdict-desc-probe.sh` — run recipe (timeout, liveness, pid-based stop).
- Logs: `build/constdict-desc-probe-*.log` (not committed; `*.log` is gitignored).
