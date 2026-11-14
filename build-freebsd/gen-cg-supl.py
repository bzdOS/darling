#!/usr/bin/env python3
"""gen-cg-supl.py — emit a stub .s for a provider supplement dylib.

Reads the missing-symbol list (one symbol per line, from
sym-wall-map.py / chrome-imports-by-ordinal.py --missing) and writes an
assembly file: kCG*/kCM* constants -> 8-byte zero data objects;
everything else -> a text stub (xor eax,eax; ret). Types follow the symbol
name, which matches the overlay stubs' own convention (their kCG* constants
are data).

Usage: gen-cg-supl.py <missing.txt> <out.s> [install_name] [overlay_root]
The optional install_name (default /usr/lib/supl.dylib) is echoed in the
header and in the link recipe printed after generation. The optional
overlay_root (default $DARLING_OVERLAY) locates the provider images the
PROVIDER_CODE bodies are copied from; it is read only when the input list
carries one of those names.
"""
import sys

DATA_PREFIXES = ("_kCG", "_kCM", "_kCA", "_kCV", "_kCI", "_kMT",
                 "_OBJC_CLASS_", "_OBJC_METACLASS_", "___")

# Symbols the supplement must NOT answer with a data slot or a no-op return:
# the real provider spells them differently, so the stub forwards. A `.quad 0`
# slot makes a caller bind to the slot's address and jump into __data; the
# generator's default `xorl %eax,%eax; ret` is worse for bzero (a caller that
# asked for zeroed memory gets it untouched and "succeeds").
#   ___bzero   — chrome imports this private alias of bzero (step 41)
#   _bzero     — libsystem_c re-exports it from libsystem_platform; without
#                this entry the local xor;ret stub would swallow the forward
FORWARD_NAMES = {
    "___bzero": "_bzero",
    "_bzero": "__platform_bzero",
}

# Symbols the supplement must not stub at all: the real provider exports the
# same name, so a forward would loop onto the local stub and a data slot would
# repeat the step-40 wall one class lower (bind puts the object's address in
# the GOT, the call jumps into __data). Omitting the name lets dyld bind it
# straight into the provider. Classified in step 41: the 24 FUNC names whose
# provider definition is in __TEXT,__text. The bzero pair is not here: it
# forwards (FORWARD_NAMES above).
#
# The 21 names below are the step-41 FUNC set minus ___atomic_load/___atomic_store.
# Step 43/2 measured that they resolve through the re-export chain: build-suppl-stubs.sh
# links suppl/cg-supl with -reexport_library for libsystem_c, libsystem_kernel,
# libsystem_m and libsystem_platform, so an omitted name is found in suppl by dyld
# following those four re-exports. Without that link they must stay stubs (step 42:
# dyld refused the load at the first omitted name, ___stack_chk_fail).
SKIP_NAMES = {
    "____mb_cur_max_l", "___darwin_check_fd_set_overflow", "___error",
    "___exp10", "___exp10f", "___fpclassifyf", "___mac_syscall", "___maskrune",
    "___math_errhandling", "___memcpy_chk", "___memmove_chk", "___memset_chk",
    "___sincos_stret", "___sincosf_stret", "___stack_chk_fail",
    "___strlcat_chk", "___strlcpy_chk", "___tolower", "___toupper",
    "___ulock_wait", "___ulock_wake",
}

# Always emitted, never skipped and never data (their names start with "___", which
# DATA_PREFIXES would turn into `.quad 0`): the framework calls them through the GOT,
# so a data slot puts the caller in __text jumping into __data.
#
# MEASURED: a text forward cannot carry them.
#   - their only definitions in the whole closure are in LSysX.dylib (llvm-nm over
#     every Mach-O of the overlay and of the app bundle: 2 hits, both there); none of
#     the four re-export providers, libSystem.B.dylib or libcompiler_rt exports a
#     plain ___atomic_load/___atomic_store (libcompiler_rt carries only $ld$hide$*
#     markers), and no other spelling of the function exists;
#   - the name the forward would jump to is the name just defined, so a probe link
#     (`___atomic_store: jmp ___atomic_store`, -undefined dynamic_lookup) emits
#     `eb fe` — an infinite self-loop, not a bind to the provider. _memcpy, the one
#     same-semantics target under another name, is itself in the supplement's own
#     export list, so it is local too.
# So each body is the provider's implementation carried over verbatim from LSysX:
# position-independent code (llvm-objdump --macho --reloc LSysX: no entries), copied
# from the __text section at the symbol's own address.
PROVIDER_CODE = {
    "___atomic_load": "usr/lib/LSysX.dylib",
    "___atomic_store": "usr/lib/LSysX.dylib",
    "__platform_bzero": "usr/lib/system/libsystem_platform.dylib",
}
DEF_INSTALL = "/usr/lib/suppl.dylib"


def provider_code(path, wanted):
    """__text bytes of each name in `wanted`, from `path` to the next __text symbol."""
    import struct

    d = open(path, "rb").read()
    (magic,) = struct.unpack_from("<I", d, 0)
    if magic != 0xFEEDFACF:
        sys.exit(f"{path}: not a little-endian 64-bit Mach-O (magic {magic:#x})")
    (ncmds,) = struct.unpack_from("<I", d, 16)
    symtab = text = None
    off = 32
    for _ in range(ncmds):
        cmd, cmdsize = struct.unpack_from("<II", d, off)
        if cmd == 0x2:                                   # LC_SYMTAB
            symtab = struct.unpack_from("<IIII", d, off + 8)
        elif cmd == 0x19:                                # LC_SEGMENT_64
            (nsects,) = struct.unpack_from("<I", d, off + 64)
            so = off + 72
            for _ in range(nsects):
                if d[so:so + 16].split(b"\0")[0] == b"__text" \
                        and d[so + 16:so + 32].split(b"\0")[0] == b"__TEXT":
                    addr, size = struct.unpack_from("<QQ", d, so + 32)
                    (foff,) = struct.unpack_from("<I", d, so + 48)
                    text = (addr, size, foff)
                so += 80
        off += cmdsize
    if not symtab or not text:
        sys.exit(f"{path}: no LC_SYMTAB or no __TEXT,__text")
    symoff, nsyms, stroff, strsize = symtab
    addr, size, foff = text
    values, addrs = {}, []
    for i in range(nsyms):
        e = symoff + i * 16
        n_strx, n_type, _n_sect, _n_desc, n_value = struct.unpack_from(
            "<IBBHQ", d, e)
        if n_type & 0x0E != 0x0E or not addr <= n_value < addr + size:
            continue
        name = d[stroff + n_strx:].split(b"\0")[0].decode()
        values[name] = n_value
        addrs.append(n_value)
    addrs = sorted(set(addrs))
    out = {}
    for w in wanted:
        if w not in values:
            sys.exit(f"{path}: {w} is not defined in __text")
        start = values[w]
        later = [a for a in addrs if a > start]
        end = min(later) if later else addr + size
        out[w] = d[foff + start - addr:foff + end - addr]
    return out


def byte_lines(b):
    return ["    .byte " + ",".join(f"0x{x:02x}" for x in b[i:i + 16])
            for i in range(0, len(b), 16)]


def main():
    src, dst = sys.argv[1], sys.argv[2]
    install = sys.argv[3] if len(sys.argv) > 3 else DEF_INSTALL
    root = sys.argv[4] if len(sys.argv) > 4 else None
    names = [l.strip() for l in open(src) if l.strip()]
    # always-emit: a provider-code name is emitted even when the installed export
    # list does not carry it — the forward that ends on it must find it here
    for extra in PROVIDER_CODE:
        if extra not in names:
            names.append(extra)
    out = [f"# generated by gen-cg-supl.py — supplement stubs for {install}",
           ".text"]
    n_text = n_data = n_fwd = n_skip = n_body = 0
    bodies = {}
    if any(n in PROVIDER_CODE for n in names):
        if not root:
            import os
            root = os.environ.get("DARLING_OVERLAY")
        if not root:
            sys.exit("provider-code bodies need the overlay root: "
                     "pass it as the 4th argument (or set DARLING_OVERLAY)")
        by_image = {}
        for n in names:
            if n in PROVIDER_CODE:
                by_image.setdefault(PROVIDER_CODE[n], []).append(n)
        for rel, group in by_image.items():
            bodies.update(provider_code(f"{root}/{rel}", group))
    for name in names:
        if name in PROVIDER_CODE:
            out += [f".text", f".globl {name}", f"{name}:"]
            out += byte_lines(bodies[name])
            n_body += 1
            continue
        if name in SKIP_NAMES:
            n_skip += 1
            continue
        if name in FORWARD_NAMES:
            out += [f".text", f".globl {name}", f"{name}:",
                    f"    jmp {FORWARD_NAMES[name]}"]
            n_fwd += 1
        elif name.startswith(DATA_PREFIXES):
            out += [f".data", f".globl {name}", f"{name}:", ".quad 0"]
            n_data += 1
        else:
            out += [f".text", f".globl {name}", f"{name}:",
                    "    xorl %eax, %eax", "    ret"]
            n_text += 1
    open(dst, "w").write("\n".join(out) + "\n")
    print(f"wrote {dst}: {n_text} text stubs, {n_data} data stubs, "
          f"{n_fwd} forwarding stubs, {n_body} provider-code bodies, "
          f"{n_skip} skipped, {len(names)} total")
    print("link recipe:")
    print(f"  clang -target x86_64-apple-macos10.12 -c {dst} -o {dst}.o")
    print(f"  ld64.lld -dylib -arch x86_64 -platform_version macos 10.12 10.12 \\")
    print(f"    -undefined dynamic_lookup \\")
    print(f"    -install_name {install} -current_version 1.0.0 \\")
    print(f"    -compatibility_version 1.0.0 -o {dst}.dylib {dst}.o")


if __name__ == "__main__":
    main()
