# Control #158 step 36 — the description IMP: NSConstantDictionary is an empty stub class

**Date:** 2026-10-09
**Branch:** task/objc-desc-imp-bind
**Base:** pr-arm64 = f6e4f36af (step 35 merged)
**Method:** static analysis of the staged dylibs and `diag/chrome-fw-run.log`
(step 33). No build, no new run.

Follows step 35 (`docs/OBJC-COPYDESC-PATH.md`), which named the crash as
`[[arg1 description] copy]` with `arg1` an `NSConstantDictionary`.

---

## 1. FILE truth — the class has no `description`/`copy` IMP at all

The symbol `_OBJC_CLASS_$_NSConstantDictionary` is **not in Foundation**: it is
defined in **`$DARLING_OVERLAY/usr/lib/CoreFoundationExtras.dylib`**:

```
$ llvm-nm $DARLING_OVERLAY/usr/lib/CoreFoundationExtras.dylib | grep NSConstantDictionary
0000000000002050 S _OBJC_CLASS_$_NSConstantDictionary
0000000000002078 S _OBJC_METACLASS_$_NSConstantDictionary
0000000000002178 s __OBJC_CLASS_RO_$_NSConstantDictionary
```

Reading the class object (0x2050) and its `class_ro_t` (0x2178) out of the file:

```
class@0x2050 isa=0x2078 super=0x0 data=0x2178 flags=0x2 name=NSConstantDictionary baseMethods=0x0
meta @0x2078 isa=0x2078 super=0x2050 data=0x2130 flags=0x3 name=NSConstantDictionary baseMethods=0x0
class_ro_t bytes @0x2178: 02 00 00 00 ... 36 06 00 00 ...
$ grep -c description CoreFoundationExtras.dylib    # 0 occurrences of the selector string
```

- `flags = 0x2` = `RO_ROOT` → **NSConstantDictionary is a root class** (no
  superclass: `super = 0x0` in the file).
- `baseMethods = 0x0` → the class has **no method list** (neither the class nor
  the metaclass).
- the file contains **no `"description"` and no `"copy"` selector strings** at all.

So there is **no IMP for `description` or `copy` in this class's file** — the
premise that the staged Foundation holds a `description` method for
`NSConstantDictionary` does not hold. The class is an **empty stub**: it exists
only so the symbol resolves for a bind.

## 2. Classification of the mismatch

Of the three options:

- **(a) a wrong fixup of the Foundation/CoreFoundationExtras image — ruled
  out.** There is no IMP in this class to be a wrong fixup; the class has no
  method list at all. The image's own fixups (the class object's `isa`/`data`
  and the object's isa bind) resolve to what the file says.
- **(c) a CF copy-description-family function that returns code — ruled out
  here** for the same reason (no such IMP is present in this class).
- **(b) the file is consistent, but the guest resolved/executed something else
  — this is what remains.** An **empty root class** should make
  `[dict description]` an *unrecognized selector*, yet the run shows it
  returning a code pointer (`&__NSCFCopyDescription2`). The class's methods and
  superclass are established at runtime by the objc runtime; with a stub root
  class, whatever `description` resolved to — and the code pointer it returned —
  is produced by the guest-side method resolution/cache in the mldr/dyld path,
  not by an IMP the file names. Static files alone cannot name the exact
  resolution; the evidence is that the file offers nothing to resolve *to*.

So the mismatch is a **guest-resolution artifact around an empty stub class**
(b), not an image defect (a).

## 3. The path tail — deeper frames

`gstack+0x20` (`+32`) is `___CFStringAppendFormatCore` (step 35). Deeper frames
in the same raw stack window:

```
[gstack+ 32] 0x000005e4f1954264   CoreFoundation.framework+0xED264  (___CFStringAppendFormatCore)
[gstack+ 80] 0x000005e4f1a0793a   CoreFoundation.framework+0x1A093A
```

The gstack dump is a raw window from `rsp` (frames at `+40`, `+48`, `+56` are
stack addresses, not code), **not a full unwind**, and it contains **no Chrome
code frame** — so the `Chrome+0x2065300` caller of the `%@` formatting is not
recoverable from the log. Honest negative: the caller cannot be named from the
existing data; a run with a full backtrace (or a breakpoint at the initializer)
would be needed.

## 4. Repro

```sh
CFE=$DARLING_OVERLAY/usr/lib/CoreFoundationExtras.dylib
llvm-nm $CFE | grep NSConstantDictionary
python3 - "$CFE" <<'PY'
import struct,os,sys
d=open(sys.argv[1],"rb").read()
u32=lambda o:struct.unpack_from('<I',d,o)[0]; u64=lambda o:struct.unpack_from('<Q',d,o)[0]
def cstr(o):
    e=d.index(b'\0',o); return d[o:e].decode()
for lbl,cls in (('class',0x2050),('meta',0x2078)):
    data=u64(cls+32)&~7
    print(lbl,hex(cls),'super',hex(u64(cls+8)),'flags',hex(u32(data)),'name',cstr(u64(data+24)),'baseMethods',hex(u64(data+32)))
PY
# gstack tail
grep -nE 'gstack\+ *(32|80)\]' $DARLING_SRC_DIR/../diag/chrome-fw-run.log
```
