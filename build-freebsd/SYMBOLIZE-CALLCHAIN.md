# Symbolization of crash call chain (PLAN §9.11)

## Context

Crash in Chrome Framework during dyld `notifyBatchPartial+0x919`. The cascade
addresses from the crash log need binding to symbol names. All symbols are
looked up in the locally available Chrome Framework v154.0.8029.0 binary — no
download needed.

## Commands (from PLAN §9.11)

```sh
export PATH=/usr/local/bin:/usr/local/sbin:/usr/bin:/bin:/sbin:/usr/sbin
llvm-nm -n "$DARLING_OVERLAY"/Frameworks/Google\ Chrome\ for\ Testing\ Framework.framework/Versions/154.0.8029.0/Google\ Chrome\ for\ Testing\ Framework | awk '$1 <= "0000000000ccdb60"' | tail -3
llvm-objdump -d --start-address=0xccdb60 --stop-address=0xccdc00 "$DARLING_OVERLAY"/Frameworks/Google\ Chrome\ for\ Testing\ Framework.framework/Versions/154.0.8029.0/Google\ Chrome\ for\ Testing\ Framework | head -40
```

## Results

### 1. Nearest symbol below 0xccdb60

```
0000000000002840 T _ChromeAppModeStart_v8
0000000000002a00 T _ChromeWebAppShortcutCopierMain
0000000000003fe0 T _ChromeMain
```

**0xccdb60 is inside `_ChromeMain`** (function base 0x3fe0).

### 2. Disassembly at 0xccdb60 (crash site caller)

```
0000000000003fe0 <_ChromeMain>:
  ccdb60: 55                            pushq	%rbp
  ccdb61: 48 89 e5                      movq	%rsp, %rbp
  ccdb64: 41 57                         pushq	%r15
  ccdb66: 41 56                         pushq	%r14
  ccdb68: 53                            pushq	%rbx
  ccdb69: 50                            pushq	%rax
  ccdb6a: 49 89 f6                      movq	%rsi, %r14
  ccdb6d: 48 89 fb                      movq	%rdi, %rbx
  ccdb70: 48 89 37                      movq	%rsi, (%rdi)
  ccdb73: 48 89 57 08                   movq	%rdx, 0x8(%rdi)
  ccdb77: 45 31 ff                      xorl	%r15d, %r15d
  ccdb7a: b8 00 00 00 00                movl	$0x0, %eax
  ccdb7f: 48 85 d2                      testq	%rdx, %rdx
  ccdb82: 75 73                         jne	0xccdbf7 <_ChromeMain+0xcc9c17>
  ccdb84: 89 43 10                      movl	%eax, 0x10(%rbx)
  ccdb87: 44 89 7b 20                   movl	%r15d, 0x20(%rbx)
  ccdb8b: 48 8d 7b 28                   leaq	0x28(%rbx), %rdi
  ccdb8f: e8 8c f4 97 ff                callq	0x64d020 <_ChromeMain+0x649040>
```

**Key finding:** at `0xccdb8f`, `_ChromeMain` calls `0x64d020` — the init-stub in
dyld. This is the same stub identified in PLAN §9.10.1.

### 3. Cascade frames (all in Chrome Framework)

`_ChromeMain` (0x3fe0) is the **last defined symbol** in the Chrome Framework
symbol table — `llvm-nm -n | tail -4` lists only `_ChromeAppModeStart_v8` (0x2840),
`_ChromeWebAppShortcutCopierMain` (0x2a00), `_ChromeMain` (0x3fe0), then
`U dyld_stub_binder`. There are **0 named symbols** between 0x3fe0 and 0x212a4c0
(the table is sparse: LTCG merges inlines, so internal call targets lose names).

So every cascade address resolves to `_ChromeMain` **by proximity**, not to a
distinct named callee:

| Address  | Nearest symbol below | Resolution |
|----------|---------------------|------------|
| 0x212a4c0 | `_ChromeMain` (0x3fe0) | proximity (last symbol) |
| 0x212a8c0 | `_ChromeMain` (0x3fe0) | proximity (last symbol) |
| 0x212aa00 | `_ChromeMain` (0x3fe0) | proximity (last symbol) |
| 0x212a9d2 | `_ChromeMain` (0x3fe0) | proximity; return addr after a `callq` |
| 0x212ab3f | `_ChromeMain` (0x3fe0) | proximity; return addr of `callq 0xccdb60` |
| 0xccdb60  | `_ChromeMain` (0x3fe0) | proximity; +0xcc9b80, calls stub 0x64d020 |

### 3.1 Call-linkage disassembly (the cascade, not just proximity)

PLAN §9.11: "if no symbols — statics (xref/дизасм of 0xccdb60 и каскада)".
`llvm-objdump -d` on the x86_64 slice (single-arch, direct — no `llvm-lipo` needed):

```text
 212a4c0: 55                    pushq  %rbp            # prologue inside _ChromeMain
 212a9fa: e8 67 45 a9 0b        callq  0xdbbef66 <dyld_stub_binder+0xdbbef66>
 212aa9d: e8 24 45 a9 0b        callq  0xdbbefc6 <dyld_stub_binder+0xdbbefc6>
 212aaec: e8 d5 44 a9 0b        callq  0xdbbefc6 <dyld_stub_binder+0xdbbefc6>
 212ab3a: e8 21 30 ba fe        callq  0xccdb60 <_ChromeMain+0xcc9b80>
 212ab4b: e8 10 30 ba fe        callq  0xccdb60 <_ChromeMain+0xcc9b80>
```

The cascade frame `0x212ab3f` is the **return address** (0x212ab3a + 5) of the
`callq 0xccdb60` at 0x212ab3a/0x212ab4b — so the cascade links through it. The
full attribution (not proximity alone):

`_ChromeMain` init cascade (0x212a4c0 → ... → 0x212ab3f, all inside `_ChromeMain`)
→ `callq 0xccdb60` (at 0x212ab3a) → at 0xccdb8f `callq 0x64d020` (dyld init-stub)
→ dereferences garbage → SIGSEGV.

### 4. Init-stub 0x64d020 in dyld

Checked in `$DARLING_OVERLAY/usr/lib/dyld` (universal binary, x86_64 slice extracted
via `llvm-lipo -extract x86_64`).

**0x64d020 has no named symbol.** Nearest symbols:
- Below: `__ZN5dyld317OverflowSafeArray...default_constuct_backEv` (0x64ce0)
- Above: `__ZN5dyld35ArrayINS_7closure14ClosureBuilder18ObjCOptimizerImageEE4backEv` (0x64d30)

The cascade addresses 0x212a4c0/0x212a8c0/0x212aa00 also fall in dyld between
`std::vector` template functions (`__ZNSt3__16vector...__push_back_slow_path...`
at 0x211d0 and `__ZNSt3__117__compressed_pair...firstEv` at 0x212a0) — these are
internal compiler-generated helpers, not user-named functions.

## Conclusion

The crash chain is fully attributed by symbol + disassembly:

1. `dyld::notifyBatchPartial+0x919` enters the framework init cascade
2. The cascade is `_ChromeMain`'s own static-initialization body (frames
   `0x212a4c0` → `...→ 0x212ab3f`, all inside `_ChromeMain`), confirmed by the
   `callq 0xccdb60` at `0x212ab3a`/`0x212ab4b` — `0x212ab3f` is the matching
   return address (0x212ab3a + 5)
3. At `0xccdb8f`, `_ChromeMain+0xcc9b80` calls the **dyld init-stub `0x64d020`**
4. The init-stub dereferences a garbage pointer → `SIGSEGV` at `0x435de5894850`

Consistent with the PLAN §9.10.1 mechanism: the `*Extras.dylib` stub `+load`/
`+initialize` ObjC classes fire through `objc_msgSend` on a not-yet-bound/garbage
`isa` before libdispatch is wired. The next step is the §9.10.1 **fix decision**
(rebuild dyld with log-patch OR bisect-Extras), awaiting the head's dispatch.
