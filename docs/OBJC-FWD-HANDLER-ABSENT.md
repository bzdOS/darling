# Control #161 step 39 — no image installs a forward handler; forwarding never ran in either run

**Date:** 2026-10-13
**Branch:** task/fwd-handler-negative
**Base:** pr-arm64 = 0aebe4e87 (step 38 merged)
**Method:** static verification of the staged overlay images + the two run
logs. No build, no new guest run.

Follows step 38 (`docs/OBJC-DESC-IMP-PROBE.md`), whose §4 attributed the
pristine probe's return value (`libsystem_trace+0x5eda`) to "the default
forward handler (installed by the environment)". This step measures whether
any forward handler can run at all in the guest, and flips that attribution.

## 0. Result — measured: forwarding never ran; step 38 §4's handler attribution is wrong

`__objc_msgForward` (libobjc+0x48e80) is
`movq __objc_forward_handler(%rip),%r11; jmp *%r11` —
the value in `__objc_forward_handler` (libobjc data, nm `D`) is the only
thing that can run on the forward path.

1. The only writer of `__objc_forward_handler` is `_objc_setForwardHandler`
   (libobjc+0x1f664, `movq %rdi,__objc_forward_handler(%rip)`).
2. No staged image calls it or references the slot at all:

   | image (staged overlay) | refs found |
   |---|---|
   | Foundation | 0 |
   | CoreFoundation | 0 |
   | CoreFoundationExtras.dylib | 0 |
   | libsystem_trace.dylib | 0 |
   | AppKit | 0 |

   `grep -rn 'objc_setForwardHandler\|__objc_forward_handler' src/startup/`
   → 0 hits (nothing in mldr/dserver sets it); the only sources referencing
   it under `tests/` are my own two probes (the stamp variants).
3. This image's default handler `objc_defaultForwardHandler`
   (libobjc+0x4a1e6) ends unconditionally in
   `_objc_fatal("%c[%s %s]: unrecognized selector sent to instance %p
   (no message forward handler is installed)")` (0x4a1e6..0x4a23e:
   getClass → isMetaClass picks `+`/`-` → getClassName → sel_getName →
   _objc_fatal). It cannot return a value.
4. Empirical, both sides: `grep -c 'unrecognized selector'` = **0** in
   `diag/chrome-fw-run.log` (the chrome death, Control #33) and **0** in all
   13 `build/constdict-desc-probe-*.log` (pristine AND stamp runs).

So in the guest `__objc_forward_handler` is never overwritten and the value
that would run is libobjc's own default — a fatal. Neither run shows that
fatal line, so **the `[… description]` sends never went through forwarding**
in either the pristine probe or chrome.

## 1. What this corrects in step 38

- Step 38 §4 read the pristine return `libsystem_trace+0x5eda` (a pointer
  into `__TEXT,__objc_classname`, inside the string `"OS_os_log"`) as "the
  default forward handler returned a pointer into a class-name string".
  Measured here: that handler cannot return; the value came from a **found
  IMP** on the guest's send path, not from forwarding.
- The stamp run is unaffected by this: it installs its own handler, so it
  only proves *a* handler is consulted *when one exists*. It says nothing
  about the pristine path.
- Step 38 §3 stands unchanged: `class_getMethodImplementation` on the stub
  returns `_objc_msgForward` — and that is the sharp new fact: moments
  before the send, the API path reports "no method, would forward", yet the
  send seconds later **returned a value** (`libsystem_trace+0x5eda` cold;
  `Foundation+0x790B0` = `&__NSCFCopyDescription2` in chrome). The guest's
  `objc_msgSend` miss path therefore produces an IMP where the API path
  says there is none — the two paths disagree inside the guest.

## 2. Consequence for the chain

Chrome's death (step 33/34: rip = libobjc+0x480DD = `_objc_msgSend+0x1d`,
rdi = `Foundation+0x790B0` = the code pointer itself as receiver, rax =
garbage class read from the code bytes) is a **second** send whose receiver
was the first send's return value. The residual question is no longer
"which forwarding branch fires" (control closed: none) but **"what IMP does
the guest's send path bind for `description` on this class"**: a classname
string in the cold probe, a function address in chrome's warm run. That is
the target of the next Control: compare `objc_msgSend`'s cache-miss
resolution against `lookUpImpOrForward`'s answer, and against the mldr
fixup/bind path that could seed the cache.

## 3. Repro

```sh
export PATH=/usr/local/bin:/usr/local/sbin:/usr/bin:/bin:/sbin:/usr/sbin
# DARLING_OVERLAY — staged overlay on this build machine
OBJC=$DARLING_OVERLAY/usr/lib/libobjc.A.dylib

llvm-nm $OBJC | grep -i forward
# 4a1e6 __Z26objc_defaultForwardHandlerP11objc_objectP13objc_selector
# 5c258 D __objc_forward_handler
# 1f660 T _objc_setForwardHandler

# the only writer of the handler slot:
llvm-objdump -d $OBJC | grep -n 'objc_forward_handler'
#   1f664: movq %rdi, __objc_forward_handler(%rip)   <- the write
#   48e80: movq __objc_forward_handler(%rip), %r11   <- msgForward's read

# no other staged image references it or the setter:
for img in System/Library/Frameworks/Foundation.framework/Versions/C/Foundation \
           System/Library/Frameworks/CoreFoundation.framework/Versions/A/CoreFoundation \
           usr/lib/CoreFoundationExtras.dylib usr/lib/system/libsystem_trace.dylib \
           System/Library/Frameworks/AppKit.framework/Versions/C/AppKit; do
  llvm-objdump -d $DARLING_OVERLAY/$img | grep -c 'objc_setForwardHandler\|objc_forward_handler'
done   # 0 0 0 0 0

# the default handler cannot return a value (body 0x4a1e6..0x4a23e):
llvm-objdump -d --start-address=0x4a1e6 --stop-address=0x4a242 $OBJC

# empirical: neither run ever hit the forward fatal:
grep -c 'unrecognized selector' <chrome log>            # 0
grep -c 'unrecognized selector' <constdict probe logs>  # 0, all 13 files
```

## 4. Artifacts

- This doc only — no code change, no build, no run.
- Logs on disk (not committed): `build/constdict-desc-probe-*.log`,
  `$DARLING_SRC_DIR/../diag/chrome-fw-run.log`.
