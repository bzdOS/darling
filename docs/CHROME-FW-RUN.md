# Control #155 step 33 — patched launcher template: chrome-macho's own dlopen opens the framework

**Date:** 2026-10-09
**Branch:** task/chrome-fw-run
**Base:** pr-arm64 = 61717a8a0 (step 32 merged)
**Method:** rewrite the launcher's framework-path cstring (`../Frameworks/…` →
`Frameworks/…`) with `build-freebsd/chrome-template-patch.py`, then one clean
run (recipe `docs/SIGTRAP-RIP.md`, no dtrace, no `MLDR_TRAP_AT`/`RESUME_AT`,
`timeout -k 5 300`). Log: `$DARLING_SRC_DIR/../diag/chrome-fw-run.log`.

---

## 0. Result

- **chrome-macho's own `dlopen` no longer dies on `//../`.** With the template
  patched to `Frameworks/…`, the constructed path is `//Frameworks/…` and the
  framework **opens**: `dlopen_internal(//Frameworks/…/Google Chrome for Testing
  Framework) ==> 0x7f85af57…`. The dependency tree loads (Foundation,
  CoreFoundation, libicucore, …) and the framework's initializers run.
- **New stop, named with its owner:** `SIGSEGV` (`signal 11 (code=1)`) inside
  **`_objc_msgSend`** at **`libobjc.A.dylib+0x480DD`** —
  `andl 0x18(%r10), %r11d` reading `0x18(%r10)` with `r10` (the object's masked
  isa) `= 0x348e5894850`, a garbage pointer; faulting address `0x348e5894868`.
- `grep -c 'errno=22'` → **0**; `grep -c 'SIGTRAP-AUDIT-DELIVER'` → **0**.

So the step-32 blocker (the launcher's own `//../Frameworks/…` dlopen) is gone;
the run now reaches the framework and stops on a bad ObjC object pointer.

## 1. Patch verification

`chrome-template-patch.py <launcher> <out>` output:

```
template @ 10928: slot=118 new=115 padded=3
read-back: NEW at 10928, following bytes: 00 00 00 4e 31 30 5f 5f
LCs BEFORE:
  LC_LOAD_DYLINKER cmdsize=32 /usr/lib/dyld
  LC_LOAD_DYLIB cmdsize=56 cur=0x054c0000 compat=0x00010000 /usr/lib/libSystem.B.dylib
LCs AFTER:
  LC_LOAD_DYLINKER cmdsize=32 /usr/lib/dyld
  LC_LOAD_DYLIB cmdsize=56 cur=0x054c0000 compat=0x00010000 /usr/lib/libSystem.B.dylib
```

Exact-slot write (118 → 115 bytes + 3 NUL pad); read-back shows the new string
followed by `00 00 00`; load commands byte-identical (`cur=0x054c0000
compat=0x00010000` untouched). chrome-macho is a gitignored build artefact.

**On the staging:** the harness copies `tests/<bin>` to
`$LOCAL_OVERLAY/<bin>` and execs that copy (`launch-dynamic-smoke.c:720-726`,
`:983`). The copy→exec gap is 4 log lines (the framework staging is cached), so
a poller on the staged copy loses the race; the **source** `tests/chrome-macho`
was patched (the harness then stages a copy of the patched launcher) and
**restored after** the run. Staging dirs were not removed and the overlay was
not patched.

## 2. The run's end point

```
  dlopen_internal(//Frameworks/…/Google Chrome for Testing Framework) ==> 0x7f85af57…   <- opens
dyld: calling initializer function 0x5e4e3675300 in //Frameworks/…/Google Chrome for Testing Framework
…
[darling-mldr] FATAL signal 11 (code=1) at addr=0x348e5894868
  rip=0x000005e4e141a0dd  r10=0x00000348e5894850  r11=0x000005e4e131094d …
```

`rip 0x5e4e141a0dd` is in the image loaded at `__TEXT 0x5E4E13D2000` =
**libobjc.A.dylib**, offset **0x480DD**:

```
_objc_msgSend:
480d7: andq  (%rdi), %r10        ; r10 = isa
480dd: andl  0x18(%r10), %r11d   ; <-- faults: r10 = 0x348e5894850 is not a class
```

The faulting read is the isa→class dereference with a garbage isa; the caller
passed an object whose `isa` is `0x348e5894850` (bytes that look like code).
This is a new class of stop (a bad object pointer in the message dispatch), not
the allocator/SIGTRAP path of steps 23-27 and not the dylib-version wall of
Control #6.

## 3. Repro

```sh
# 1. patch the launcher template (exact-slot; LCs verified unchanged)
cp tests/chrome-macho /tmp/chrome-macho.orig
python3 build-freebsd/chrome-template-patch.py tests/chrome-macho /tmp/cm.patched
mv /tmp/cm.patched tests/chrome-macho

# 2. clean run (recipe docs/SIGTRAP-RIP.md), DARLING_TEST_BINARY=chrome-macho
timeout -k 5 300 env DARLING_SRC_DIR=$DARLING_SRC_DIR DARLING_OVERLAY=$DARLING_OVERLAY \
  DARLING_BUILD_DIR=$DARLING_BUILD_DIR DARLING_TEST_BINARY=chrome-macho \
  DARLING_STAGING_TREES="$(cat /tmp/stub76-staging.txt)" DARLING_SMOKE_REFRESH=1 \
  DYLD_BIND_AT_LAUNCH=1 DYLD_PRINT_LIBRARIES=1 DYLD_PRINT_INITIALIZERS=1 DYLD_PRINT_TO_STDERR=1 \
  $DARLING_BUILD_DIR/launch-dynamic > diag/chrome-fw-run.log 2>&1
grep -c 'errno=22\|SIGTRAP-AUDIT-DELIVER' diag/chrome-fw-run.log   # 0
grep 'FATAL signal' diag/chrome-fw-run.log                        # signal 11 in libobjc+0x480DD

# 3. restore the launcher
cp /tmp/chrome-macho.orig tests/chrome-macho
```
