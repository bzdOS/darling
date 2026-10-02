# dyld-bindings: where _OBJC_METACLASS_$_NSObject resolves

Frontier of the plan: diagnose the binding of `_OBJC_METACLASS_$_NSObject`
with `DYLD_PRINT_BINDINGS=1`.

## Method

`sh build-freebsd/dyld-bindings-probe.sh [SYMBOL]` runs the
guest-wl-session-roundtrip probe with `DYLD_PRINT_BINDINGS=1` alongside
the current `LD_DEBUG=all`, and writes the log + a grep of the symbol to
`$DARLING_BUILD_DIR/wl-body-dyldbind.{log,txt}`. dyld supports the flag
(the string is in `overlay/usr/lib/dyld`); the probe exits 0 and reaches
`[step 12]`.

The dyld trace line format is:

```
dyld: bind: <image>:<slot> = <provider-image>:<symbol>, *<slot> = <value>
```

The trace interleaves byte-for-byte with the traplog stream; the
analysis strips the `[traplog] …` tokens to reconstruct the lines.

## Which tracer shows the resolve

- `DYLD_PRINT_BINDINGS=1` — the Mach-O dyld binding trace: **15885**
  `dyld: bind:` lines, including the target symbol.
- `LD_DEBUG=all` — the FreeBSD ELF rtld trace: it shows `reloc_jmpslot`
  lines for the host processes (timeout / launch-dynamic / mldr /
  darlingserver), and **zero** mentions of the Mach-O symbol.

So only `DYLD_PRINT_BINDINGS` shows the resolve; `LD_DEBUG=all` does not.

## The resolve of _OBJC_METACLASS_$_NSObject

Every site resolves from **`libobjc.A.dylib`** — the ObjC runtime — via
dyld's normal bind, e.g.:

```
dyld: bind: AppKit:0xE72A87A7730 = libobjc.A.dylib:_OBJC_METACLASS_$_NSObject, *0xE72A87A7730 = 0xE72A7AC1000
dyld: bind: Wayland:0xE72A9E76898 = libobjc.A.dylib:_OBJC_METACLASS_$_NSObject, *0xE72A9E76898 = 0xE72A7AC1000
```

1333 sites, all provided by `libobjc.A.dylib` (value `0xE72A7AC1000`,
non-zero — the class object lives next door at `_OBJC_CLASS_$_NSObject` =
`0xE72A7AC1050`). Referring images: AppKit, CoreData, CoreFoundation,
CoreGraphics, Foundation, Onyx2D, QuartzCore, Wayland, libdispatch,
libsystem_trace, libxpc. No NULL / missing-symbol site.

## Between DID-NOT-RETURN and the exit (code 0)

The reconstructed log order: `[step 09]` → `DID-NOT-RETURN` → `[step 10]`
→ `[step 11]` → `[step 12]` (`LANE FINDING`) → exit 0. There are **no
`dyld: bind:` lines after the DNR marker** — every binding happened
during image loading, before the park. So between the marker and the
exit only the three fresh-display lane variants run; no new symbol is
resolved.

## Verdict (one line)

`_OBJC_METACLASS_$_NSObject` resolves from **libobjc.A.dylib** via dyld's
normal bind (1333 sites across AppKit/CoreFoundation/Foundation/Wayland/
…, non-zero); between DNR and the exit there are **no new bindings** —
only steps 10–12 run, so the park is not a binding wall for this symbol.
