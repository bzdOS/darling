# Control #157 step 35 — the copy-description path: selectors, caller, described object

**Date:** 2026-10-09
**Branch:** task/objc-copydesc-path
**Base:** pr-arm64 = 5389680ff (step 34 merged)
**Method:** static analysis of `diag/chrome-fw-run.log` (step 33) and
disassembly of the staged Foundation / CoreFoundation / Chrome framework
(read-only). No new run.

Extends step 34 (`docs/OBJC-ISA-CALLER.md`), which located the crash in
`__NSCFCopyDescription2` (Foundation+0x790B0) on its second `objc_msgSend`.

---

## 1. The two selectors — `"description"` and `"copy"`

In `__NSCFCopyDescription2` (Foundation+0x790B0):

```
790c0: 48 8b 7d f8          movq  -0x8(%rbp), %rdi
790c4: 48 8b 35 15 7c 0c 00 movq  0xc7c15(%rip), %rsi   ; sel7
790cb: ff 15 37 df 0b 00    callq *0xbdf37(%rip)        ; objc_msgSend #1
790d1: 48 89 c7             movq  %rax, %rdi
790d4: 48 8b 35 0d 7c 0c 00 movq  0xc7c0d(%rip), %rsi   ; sel9
790db: ff 15 27 df 0b 00    callq *0xbdf27(%rip)        ; objc_msgSend #2
```

Rip-relative targets (next-instruction base + displacement):

```
sel7 slot = 0x790cb + 0xc7c15 = 0x140ce0
sel9 slot = 0x790db + 0xc7c0d = 0x140ce8
```

Foundation's `__objc_selrefs` at those slots hold chained-fixup rebases; the
low bits give the selector-string offsets (file bytes at the slots:
`24 d6 10 00…` = 0x10d624, `a6 d3 10 00…` = 0x10d3a6):

```
$ python3 -c "d=open('…/Foundation','rb').read(); print(repr(d[0x10d624:d.index(b'\0',0x10d624)]))"
b'description'
$ python3 -c "d=open('…/Foundation','rb').read(); print(repr(d[0x10d3a6:d.index(b'\0',0x10d3a6)]))"
b'copy'
```

So the crash is **`[[arg1 description] copy]`**: the first message is
`description` (sel7), the second is `copy` (sel9) sent to the first message's
return value.

## 2. Who called `__NSCFCopyDescription2`

`__NSCFCopyDescription2` saves its caller's rbp at `[rbp]`; its rbp at the crash
was `0x7fffffdfda90`, so the caller's return address is `[rbp+8] =
0x7fffffdfda98 = gstack+0x20` (gstack = rsp = `0x7fffffdfda78`). In the log:

```
[gstack+ 32] 0x000005e4f1954264
```

`0x5e4f1954264` is in **CoreFoundation.framework** (`__TEXT` base
`0x5e4f1867000`) at **`CoreFoundation+0xED264`**:

```
00000000000e9350 <___CFStringAppendFormatCore>:
   ed257: 48 8b 79 10          movq  0x10(%rcx), %rdi
   ed25b: 48 8b b5 d0 e9 ff ff movq  -0x1630(%rbp), %rsi
   ed262: ff d0                callq *%rax                  ; <-- call to __NSCFCopyDescription2
   ed264: 48 89 85 20 e8 ff ff movq  %rax, -0x17e0(%rbp)   ; <-- caller return (gstack+32)
```

So the caller is **`___CFStringAppendFormatCore`** (CoreFoundation+0xE9350) — the
`NSString`/`CFString` format engine — via an **indirect** `callq *%rax` at
`0xED262` (the format handler for a `%@`-style specifier dispatches through a
table). The object is a format argument being rendered.

## 3. The described object — `NSConstantDictionary`

The object (`arg1`) is saved at `[rbp-8] = gstack+0x10`; in the log:

```
[gstack+ 16] 0x000005e4f14812e8
```

`0x5e4f14812e8` is in the **Chrome framework's `__DATA`** (`__DATA` base
`0x5E4F1360000`, vmoff `0x1212E8`; file offset `0xfe712e8`). The first 8 bytes
there (its isa) are a chained-fixup **bind**:

```
file 0xfe712e8: 3d 03 00 00 00 00 30 80   -> 0x803000000000033d  (bind, ordinal 0x33d)
```

Resolving ordinal `0x33d` (829) in the framework's `LC_DYLD_CHAINED_FIXUPS`
imports table:

```
import[829]: lib_ordinal=1 weak=0 name_off=0x4e6f
SYMBOL = _OBJC_CLASS_$_NSConstantDictionary
```

So the described object is an **`NSConstantDictionary`** instance (a static one
living in the framework's `__DATA`, isa bound to `NSConstantDictionary`). The
first message, `[dict description]`, returned a **code pointer**
(`0x5e4f21870b0 = &__NSCFCopyDescription2`, step 34); the second, `[that copy]`,
then crashed inside `_objc_msgSend` (step 34). So `-[NSConstantDictionary
description]` — or the path that produced its return value — is what yields a
function address instead of a description object; that is the next thread.

## 4. Repro

```sh
LOG=$DARLING_SRC_DIR/../diag/chrome-fw-run.log
FR=$DARLING_OVERLAY/Frameworks/Google\ Chrome\ for\ Testing\ Framework.framework/Versions/154.0.8029.0/Google\ Chrome\ for\ Testing\ Framework
FOUND=$DARLING_OVERLAY/System/Library/Frameworks/Foundation.framework/Versions/C/Foundation
CF=$DARLING_OVERLAY/System/Library/Frameworks/CoreFoundation.framework/Versions/A/CoreFoundation

# 1. selectors
llvm-objdump -d --start-address=0x790b0 --stop-address=0x790e7 $FOUND
python3 -c "d=open('$FOUND','rb').read(); print(repr(d[0x10d624:d.index(b'\0',0x10d624)]), repr(d[0x10d3a6:d.index(b'\0',0x10d3a6)]))"

# 2. caller
llvm-objdump -d --start-address=0xed250 --stop-address=0xed270 $CF
grep -n 'gstack+ *32' $LOG

# 3. described object
grep -n 'gstack+ *16' $LOG
# isa bytes at framework file offset 0xfe712e8 -> chained-fixup bind ordinal 0x33d -> _OBJC_CLASS_$_NSConstantDictionary
```
