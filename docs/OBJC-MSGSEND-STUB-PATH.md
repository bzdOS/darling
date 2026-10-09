# Control #159 step 37 — why a guest msgSend on the empty stub class returns an IMP

**Date:** 2026-10-09
**Branch:** task/objc-msgsend-stub-path
**Base:** pr-arm64 = ac7364709 (step 36 merged)
**Method:** static analysis of `diag/chrome-fw-run.log` (step 33) and
disassembly of the staged `libobjc.A.dylib` / `Foundation`. No run.

Continues step 34 (`docs/OBJC-ISA-CALLER.md`) and step 36
(`docs/OBJC-DESC-IMP-BIND.md`): the object's class `NSConstantDictionary`
(CoreFoundationExtras) is an empty root class, yet `[obj description]` returned a
code pointer instead of raising an unrecognized selector.

---

## 1. The function under the object pointer (step 34)

Step 34's fatal registers (log `chrome-fw-run.log`, line 1336850):

```
rip=0x000005e4e141a0dd  rdi=0x000005e4f21870b0  r10=0x00000348e5894850
```

The receiver `rdi` and the first message's result `rax` are both
`0x5e4f21870b0`. Foundation's `__TEXT` base in the same log is
`0x5e4f210e000`, so (python, not by hand):

```
$ python3 -c "print(hex(0x5e4f21870b0 - 0x5e4f210e000))"
0x790b0
$ llvm-nm Foundation.framework/Versions/C/Foundation | grep 790b0
00000000000790b0 t __NSCFCopyDescription2
```

So the pointer under the isa-dereference (the "object" `rdi`) is the address of
**`__NSCFCopyDescription2`** (Foundation+0x790B0) — a **function**, not a
Foundation object.

## 2. The msgSend chain and every branch that returns an IMP without raising

`libobjc.A.dylib` (`$DARLING_OVERLAY/usr/lib/libobjc.A.dylib`):

**`_objc_msgSend` @0x480c0** — fast path; each `jmpq *%r11` is a return of a
cached IMP (no raise):

```
480c3: je   0x48138      ; rdi==nil -> returns 0 (no IMP)
480ca: jne  0x48143      ; tagged-pointer path
480d7: andq (%rdi), %r10 ; isa
480dd: andl 0x18(%r10), %r11d
480e5: addq 0x10(%r10), %r11
480e9: cmpq (%r11), %rsi
480ec: jne  0x480f8      ; probe loop
480f5: jmpq *%r11        ; CACHE HIT -> cached IMP (no raise)
4810e: jmpq *%r11        ; CACHE HIT (probe slot 2)
4812f: jmpq *%r11        ; CACHE HIT (probe slot 3)
48183: jmp  __objc_msgSend_uncached   ; cache miss
```

**`__objc_msgSend_uncached` @0x48c10** calls the slow path and jumps to the
returned IMP:

```
48c51: movl $0x3, %ecx          ; behavior = LOOKUP_INITIALIZE|LOOKUP_RESOLVE
48c56: callq _lookUpImpOrForward
48c5b: movq %rax, %r11          ; r11 = returned IMP
...     jmpq *%r11 (after restore)
```

**`_lookUpImpOrForward` @0x2d4a0** — every path that sets `r13` then falls into
the common return `0x2d6d7`/`0x2d6e8` returns an IMP **without raising**:

| # | set of `r13` | address | meaning |
|---|--------------|---------|---------|
| 1 | `movq 0x10(%rbx), %r13` | `0x2d6f1` | found method IMP (superclass walk) |
| 2 | `movq %rax, %r13` | `0x2d73c` | found method IMP (`method_t_remappedImp`) |
| 3 | `leaq 0x8(%rax,%rbx), %r13` / `cmoveq` | `0x2d74f`–`0x2d758` | found method IMP |
| 4 | `leaq __objc_msgForward_impcache(%rip), %r13` | `0x2d6fa` | **forwarding IMP** (`__objc_msgForward_impcache`) — returned when the selector is not found and the caller allows forwarding |
| 5 | `xorl %r13d, %r13d` | `0x2d7c5` | **NULL IMP** (not found, no forwarding) |
| — | `movq %r13, %rax … retq` | `0x2d6d7`/`0x2d6e8` | the single common return |

The only branches that **raise** (do not return an IMP) are:

```
2d7a2: callq _objc_fatal   ; "Attempt to use unknown class %p."
2d814: callq _objc_fatal   ; "Memory corruption in class list."
```

There is **no `doesNotRecognizeSelector` / `objc_msgForward` raise here** — an
unrecognized selector is handled by returning the forwarding IMP (`0x2d6fa`),
which the caller then jumps to (forwarding), not by raising inside
`lookUpImpOrForward`.

## 3. The minimal path to `&__NSCFCopyDescription2` as the IMP

For `[obj description]` to return `0x5e4f21870b0 = &__NSCFCopyDescription2`, the
`description` lookup must hit branch **#1/#2/#3** (a found method whose IMP is
`__NSCFCopyDescription2`), or the fast-path **cache hit** with that IMP cached.
Both require a method-list / cache entry whose IMP is exactly
`Foundation+0x790B0`. But the class that owns the receiver
(`NSConstantDictionary`, CoreFoundationExtras) is an **empty root class with no
method list** (step 36), so **no static file names such an entry** — the
`description`→`__NSCFCopyDescription2` binding is not present in any staged
image we can read.

**Honest negative:** the exact branch that produced the IMP is reachable only
dynamically (a runtime-populated method list / cache that no static image
declares). Static files give the complete set of candidate branches (§2) but not
which one fired. Confirming it needs a runtime probe.

### runtime probe (for step 38 — owner decision 04.10)

Two options, both within the existing recipe:

- **marker in the probe**: build a tiny Mach-O that calls
  `objc_msgSend(NSConstantDictionary_instance, "description")` and prints the
  returned pointer, so the IMP can be compared to `Foundation+0x790B0`.
- **breakpoint at the initializer**: attach lldb to the guest at
  `Chrome+0x2065300`, set a breakpoint on `lookUpImpOrForward` and print `r13` /
  the class at each return, to see which branch (§2) fires.

## 4. Repro

```sh
LOG=$DARLING_SRC_DIR/../diag/chrome-fw-run.log
OBJC=$DARLING_OVERLAY/usr/lib/libobjc.A.dylib
F=$DARLING_OVERLAY/System/Library/Frameworks/Foundation.framework/Versions/C/Foundation

grep -nE 'FATAL signal|rip=' $LOG | head
python3 -c "print(hex(0x5e4f21870b0 - 0x5e4f210e000))"          # 0x790b0
llvm-nm $F | grep 790b0                                          # __NSCFCopyDescription2
llvm-objdump -d $OBJC > /tmp/libobjc.asm
grep -nA60 '^_objc_msgSend:' /tmp/libobjc.asm                    # fast path + miss
grep -nA30 '__objc_msgSend_uncached:' /tmp/libobjc.asm
grep -nA230 '^_lookUpImpOrForward:' /tmp/libobjc.asm | grep -nE 'r13|retq|objc_fatal|msgForward'
```
