# Control #164 step 42 — the forward chain's last link is an empty function; the "return" is RAX residue

**Date:** 2026-10-13
**Branch:** task/fwd-prep-empty (base: pr-arm64 = a2c97e95c, step 41 merged)
**Method:** offline reads on the staged CoreFoundation. No build, no run.

Closes Control #163's open question: what `_CF_forwarding_prep_0` is and what it
returns. The forwarding chain measured across steps 39–41 is now complete, and
its last link is the answer to the whole "cache/resolution artifact" line.

## 0. Result — measured: the handler is a 16-byte no-op that returns uninitialized RAX

`_CF_forwarding_prep_0` at CoreFoundation+0x19d310 (symbol table: `T
__CF_forwarding_prep_0`, 16 bytes to the next symbol):

```
000000000019d310 <__CF_forwarding_prep_0>:
  19d310: 55            pushq %rbp
  19d311: 48 89 e5      movq  %rsp, %rbp
  19d314: 5d            popq  %rbp
  19d315: c3            retq
```

Empty. It touches no argument, no memory, no lock, and sets no return value.
Its siblings are the same (`__CF_forwarding_prep_1` @0x19d320 and
`__CF_forwarding_prep_0`'s neighbours `__CF_forwarding_prep_b` @0x19d330:
identical prologue+ret). CoreFoundation imports `_objc_setForwardHandler`
(`U` in its symbol table, no call sites in `__text`, a `__mod_init_func` entry
exists at 0x1e1e98 with three initializer pointers) — one of those
initializers installs this handler at process start, which is what Control
#163 measured at runtime (`__objc_forward_handler` = this address).

## 1. What this explains, end to end

For an empty root class (the CoreFoundationExtras `NSConstantDictionary`
stub), every send now has a complete, measured path:

```
objc_msgSend fast path → empty bucket miss
__objc_msgSend_uncached → _lookUpImpOrForward(obj, sel, class, 3)
  → STOCK sentinel _objc_msgForward_impcache (Control #163)
jmp impcache gadget → jmp __objc_msgForward
  → movq __objc_forward_handler,%r11 → _CF_forwarding_prep_0
  → pushq %rbp; movq; popq %rbp; retq — returns RAX UNTOUCHED
```

So the value every probe measured as the send's return is **RAX residue from
the guest's own code path upstream of the jump** — never a computed result:

| observation | value | now explained as |
|---|---|---|
| Control #162, description | 0x32 | RAX residue on that path (2 runs, same binary layout) |
| Control #162, hash/class/length/isEqual:/retainCount | 0x1f/0x20/0x21/0x23/0x26 | residue differs per selector because the executed code differs |
| Control #163, description | 0xa | residue varies run to run — never assumed stable |
| step 38, description | libsystem_trace+0x5eda | residue held a classname-string pointer in that run |
| chrome, `[arg1 description]` | Foundation+0x790B0 = `&__NSCFCopyDescription2` | residue held the code pointer, which the next send then dereferenced as a receiver |

The chrome death is therefore not "a method resolved to __NSCFCopyDescription2"
— no method ever resolved. `[description]` returned an uninitialized register,
and the crash is `[<code pointer> copy]` on the *next* send (rip =
libobjc+0x480DD, `_objc_msgSend+0x1d`, Control #160).

**Why no fatal ever fires** (the question Control #161 opened): libobjc's own
`objc_defaultForwardHandler` fatals, but CoreFoundation's initializer replaced
it with this no-op before any user code runs. The guest never raises
"unrecognized selector" for a stub-root-class send — it silently returns
register garbage.

## 2. What stays open — and it is no longer the forward chain

The forwarding chain is measured end to end and its behaviour is
deterministic-but-meaningless. What is NOT yet answered is why chrome's
receiver took this path at all: chrome's object's isa binds through its
chained-fixup ordinal 0x33d (Control #156) to `_OBJC_CLASS_$_NSConstantDictionary`
— which image provides that class in chrome's link closure: the real
`CoreFoundation.framework`'s class (a toll-free-bridged class with real
methods) or my `CoreFoundationExtras` stub (empty root class). That is a
one-measurement offline question (resolve the ordinal → symbol → library in
the framework's import table with the tooling from Control #162), and its
answer decides which of the two very different fixes applies. It is a
measurement, not a guess; it is also the last question of this series, and
what follows it is a fix, not a control.

## 3. Repro

```sh
export PATH=/usr/local/bin:/usr/local/sbin:/usr/bin:/bin:/sbin:/usr/sbin
CF=$DARLING_OVERLAY/System/Library/Frameworks/CoreFoundation.framework/Versions/A/CoreFoundation

llvm-nm $CF | grep -A1 'forwarding_prep'      # 0x19d310 T __CF_forwarding_prep_0
llvm-objdump -d --disassemble-symbols=__CF_forwarding_prep_0 $CF
#   pushq %rbp; movq %rsp,%rbp; popq %rbp; retq     (16 bytes, empty)
llvm-nm $CF | grep _objc_setForwardHandler    # U — imported, installed by __mod_init_func
llvm-objdump -h $CF | grep mod_init_func      # 0x1e1e98, 3 initializers
```

## 4. Artifacts

- This doc only — no code change, no build, no run.
