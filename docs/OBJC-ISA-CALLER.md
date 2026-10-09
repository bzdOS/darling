# Control #156 step 34 — the caller of the broken object and the class of the garbage isa

**Date:** 2026-10-09
**Branch:** task/objc-isa-caller
**Base:** pr-arm64 = e045b01ad5 (step 33 merged)
**Method:** analysis of the step-33 log `diag/chrome-fw-run.log` plus
disassembly of the staged framework/dylibs (read-only). No new run was needed.

---

## 0. Result

- **The last initializer entered before the FATAL** was
  `dyld: calling initializer function 0x5e4e3675300` (log line 1336728). The
  framework `__TEXT` base is `0x5E4E1610000`, so this is
  **`Chrome+0x2065300`**.
- **The caller of the broken object** is the return address at the top of the
  guest stack at the crash (`gstack+0 = 0x5e4f21870e1`). That address is in
  **Foundation.framework** (`__TEXT` base `0x5e4f210e000`) at
  **`Foundation+0x790E1`**, inside **`__NSCFCopyDescription2`**
  (`Foundation+0x790B0`) — the instruction right after its second
  `objc_msgSend` call.
- **The receiver of the crashing message** (`rdi = 0x5e4f21870b0`) is
  `Foundation+0x790B0` — i.e. **the address of `__NSCFCopyDescription2` itself**,
  a pointer to **code**.
- **Class of the garbage isa: (i) a pointer to code.** `_objc_msgSend` read the
  object's first 8 bytes as the isa; those bytes are the function prologue
  `55 48 89 e5 48 83 ec 10` (`push rbp; mov rsp,rbp; sub $0x10,rsp`), masked to
  `r10 = 0x348e5894850`, and the class dereference `0x18(%r10)` faulted at
  `0x348e5894868`. It is **not** a stub/lazy binding and **not** an unslid
  chained-fixup record.

## 1. Log analysis (`diag/chrome-fw-run.log`)

```
1336724: dyld: calling initializer function 0x5e4f20e99b0 in …/CoreGraphics
1336727: dyld: calling initializer function 0x5e4e373a4c0 in //Frameworks/…Google Chrome for Testing Framework
1336728: dyld: calling initializer function 0x5e4e3675300 in //Frameworks/…Google Chrome for Testing Framework   <- last entered
…
1336848: [darling-mldr] FATAL signal 11 (code=1) at addr=0x348e5894868
  rip=0x000005e4e141a0dd  rdi=0x000005e4f21870b0  r10=0x00000348e5894850 …
  [gstack+  0] 0x000005e4f21870e1        <- return address (caller)
```

Framework `__TEXT` base `0x5E4E1610000` (log line 31756) →
`0x5e4e3675300 = Chrome+0x2065300`, `0x5e4e373a4c0 = Chrome+0x212A4C0`.
`libobjc.A.dylib` is mapped later (`__TEXT 0x5E4E13D2000`, log line 610/613);
the crash `rip 0x5e4e141a0dd = libobjc+0x480DD = _objc_msgSend`.

`rdi 0x5e4f21870b0` and `gstack+0 0x5e4f21870e1` are both in
**Foundation.framework** (`__TEXT 0x5e4f210e000`, log line mapping
`/System/Library/Frameworks/Foundation.framework/Versions/C/Foundation`) —
offsets `0x790B0` and `0x790E1`.

## 2. The call site — `__NSCFCopyDescription2`

```
790b0 <__NSCFCopyDescription2>:
790b0: 55                   pushq %rbp
790b1: 48 89 e5             movq  %rsp, %rbp
790b4: 48 83 ec 10          subq  $0x10, %rsp
790b8: 48 89 7d f8          movq  %rdi, -0x8(%rbp)     ; save arg1
790bc: 48 89 75 f0          movq  %rsi, -0x10(%rbp)    ; save arg2
790c0: 48 8b 7d f8          movq  -0x8(%rbp), %rdi     ; rdi = arg1
790c4: 48 8b 35 15 7c 0c 00 movq  0xc7c15(%rip), %rsi  ; sel7
790cb: ff 15 37 df 0b 00    callq *0xbdf37(%rip)       ; objc_msgSend #1 -> rax
790d1: 48 89 c7             movq  %rax, %rdi           ; rdi = rax
790d4: 48 8b 35 0d 7c 0c 00 movq  0xc7c0d(%rip), %rsi  ; sel9
790db: ff 15 27 df 0b 00    callq *0xbdf27(%rip)       ; objc_msgSend #2  <-- crash
790e1: 48 83 c4 10          addq  $0x10, %rsp          ; <-- return address (gstack+0)
790e5: 5d                   popq  %rbp
790e6: c3                   retq
```

The crash return address `0x790E1` is exactly the instruction after the
**second** `callq *…` (`0x790DB`). That call's receiver (`rdi`) is `rax` from the
**first** message (`movq %rax,%rdi` at `0x790D1`), which the run shows as
`0x5e4f21870b0 = Foundation+0x790B0 = &__NSCFCopyDescription2`.

So the first message (selector `.7` on the input object) returned a **function
address**, and the second message (selector `.9`) was sent to that code pointer.
`_objc_msgSend` then dereferenced the function's first 8 bytes as the isa:

```
480d7: andq (%rdi), %r10    ; r10 = mask & *(rdi);  *(0x5e4f21870b0) = 55 48 89 e5 48 83 ec 10
480dd: andl 0x18(%r10), %r11d   ; <-- fault at 0x348e5894868
```

`55 48 89 e5 48 83 ec 10` is the `__NSCFCopyDescription2` prologue — the same
"prologues look like `55`/`48 89 e5`" bytes the step-33 report flagged.

## 3. Class of the isa — verdict

**A pointer to code.** The receiver is a function address
(`Foundation+0x790B0`), so the "isa" field is the function's machine code, not a
class object. Evidence: the receiver equals the exact address of
`__NSCFCopyDescription2` (image base + 0x790B0), and its first 8 bytes are that
function's prologue (a plausible class pointer would point into Foundation's
`__DATA`/`__objc_data`, not into `__TEXT`). Ruled out: a stub/lazy binding (the
value is a real in-image code address, not a `__stubs`/GOT slot) and an unslid
chained fixup (an unslid pointer would be a small file-relative value, not a
fully-slid `0x5e4f…` address).

## 4. Repro

```sh
# analysis only — no new run
LOG=$DARLING_SRC_DIR/../diag/chrome-fw-run.log
grep -n 'calling initializer' $LOG | tail -1                 # 0x5e4e3675300 = Chrome+0x2065300
grep -n 'FATAL signal'       $LOG                            # rip, rdi, r10, gstack+0
# framework __TEXT base:
grep -n '__TEXT at 0x5E4E1610000' $LOG
# caller: Foundation+0x790E1 (__NSCFCopyDescription2), object = Foundation+0x790B0
FOUND=$DARLING_OVERLAY/System/Library/Frameworks/Foundation.framework/Versions/C/Foundation
llvm-objdump -d --start-address=0x790b0 --stop-address=0x79100 $FOUND
OBJC=$DARLING_OVERLAY/usr/lib/libobjc.A.dylib
llvm-objdump -d $OBJC | grep -A8 '^_objc_msgSend:'             # 480d7 / 480dd
```
