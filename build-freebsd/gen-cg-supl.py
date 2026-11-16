#!/usr/bin/env python3
"""gen-cg-supl.py — emit a stub .s for a provider supplement dylib.

Reads the missing-symbol list (one symbol per line, from
sym-wall-map.py / chrome-imports-by-ordinal.py --missing) and writes an
assembly file: kCG*/kCM* constants -> 8-byte zero data objects;
everything else -> a text stub (xor eax,eax; ret). Types follow the symbol
name, which matches the overlay stubs' own convention (their kCG* constants
are data).

Usage: gen-cg-supl.py <missing.txt> <out.s> [install_name] [overlay_root]
The bodies are written in assembly, not copied from a provider image. With an
overlay_root, the export tries of every dylib libSystem.B.dylib re-exports are
read and a name any of them already defines is not emitted at all.
`gen-cg-supl.py --providers <overlay_root>` prints that dylib list.
The optional install_name (default /usr/lib/supl.dylib) is echoed in the
header and in the link recipe printed after generation.
"""
import os
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import stub_data_rules as R  # noqa: E402

# Typing is shared with check-data-as-code.py through stub_data_rules.py: a name the
# gate calls data is emitted as a zero-filled object here (never as code), ObjC classes
# as real root classes in <out>.m, and names in the --data-list file (consumer
# evidence from `check-data-as-code.py --emit-data`) as data as well.
DATA_PREFIXES = ("_kCG", "_kCM", "_kCA", "_kCV", "_kCI", "_kMT", "___")

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
# A text forward cannot carry the two atomics (the only definitions in the closure
# are in LSysX.dylib, and a same-name forward assembles to a `jmp .` self-loop), and
# the earlier approach of copying the provider's machine code verbatim was wrong:
# the copied bodies carried rel32 calls whose displacement was computed for the
# provider's layout, so in the supplement they landed in the middle of unrelated
# functions (SIGILL in __platform_bzero). The bodies are therefore written here in
# assembly. They are position independent and self-contained: no call, jump or
# RIP-relative reference leaves the body (rep stosb / rep movsb instead of a call to
# memset/memmove). An import was tried first (a stub bound to __platform_memset
# through the -reexport_library link) and failed at run time: the linker attributes
# the import to one of the four provider dylibs and dyld then answered "Symbol not
# found: __platform_memset, Expected in: libsystem_kernel.dylib" at the first
# call, so the imported route is not usable with this overlay. check-suppl-bodies.py
# enforces the no-outward-reference rule on the linked image.
#   __atomic_load(size, src, dst, order), __atomic_store(size, dst, src, order)
ASM_BODIES = {
    "___atomic_load": """\
    pushq %rbp
    movq %rsp, %rbp
    cmpl $1, %edi
    jne 2f
    movzbl (%rsi), %eax
    movb %al, (%rdx)
    jmp 9f
2:  cmpl $2, %edi
    jne 3f
    movzwl (%rsi), %eax
    movw %ax, (%rdx)
    jmp 9f
3:  cmpl $4, %edi
    jne 4f
    movl (%rsi), %eax
    movl %eax, (%rdx)
    jmp 9f
4:  cmpl $8, %edi
    jne 5f
    movq (%rsi), %rax
    movq %rax, (%rdx)
    jmp 9f
5:  movslq %edi, %rcx
    movq %rdx, %rdi
    rep movsb
9:  mfence
    popq %rbp
    retq""",
    "___atomic_store": """\
    pushq %rbp
    movq %rsp, %rbp
    mfence
    cmpl $1, %edi
    jne 2f
    movzbl (%rdx), %eax
    movb %al, (%rsi)
    jmp 9f
2:  cmpl $2, %edi
    jne 3f
    movzwl (%rdx), %eax
    movw %ax, (%rsi)
    jmp 9f
3:  cmpl $4, %edi
    jne 4f
    movl (%rdx), %eax
    movl %eax, (%rsi)
    jmp 9f
4:  cmpl $8, %edi
    jne 5f
    movq (%rdx), %rax
    movq %rax, (%rsi)
    jmp 9f
5:  movslq %edi, %rcx
    movq %rsi, %rdi
    movq %rdx, %rsi
    rep movsb
9:  mfence
    popq %rbp
    retq""",
    # bzero(p, n) == memset(p, 0, n)
    "__platform_bzero": """\
    movq %rsi, %rcx
    xorl %eax, %eax
    rep stosb
    retq""",
}
# Data symbols whose value matters. The generic rules above type a name by its prefix,
# so these three (no kCG-style prefix) used to come out as `xorl %eax,%eax; ret`, i.e.
# three bytes of code read as data. The framework reads the 8-byte value at
# _vm_page_size through a two-level import that names this image, and its page-size
# check (`cmpq $0x1000` / `cmpq $0x4000`, trap otherwise) saw 0xc331 and hit int3.
# The real definitions live in libsystem_kernel (__DATA,__common); a constant here is
# independent of whether that image has run its initialiser.
DATA_VALUES = {
    "_vm_page_size": 4096,
    "_vm_page_mask": 4095,
    "_vm_page_shift": 12,
}
DEF_INSTALL = "/usr/lib/suppl.dylib"

# The dylibs build-suppl-stubs.sh re-exports from the supplement: every image that
# libSystem.B.dylib itself re-exports (see closure_providers). The framework binds
# many imports (mmap, munmap, vm_allocate, getpid, malloc, pthread_*, dispatch_*,
# fopen, abort, ...) two-level to the supplement image. dyld looks the name up in
# the supplement first and follows the re-exports only when the supplement does not
# define it, so a stub emitted under a name a provider already defines SHADOWS the
# real function: `_mmap` came back as `xorl %eax,%eax; ret`, i.e. NULL, and the
# allocator's address-space reservation "failed" without the guest ever issuing the
# system call; `_malloc_get_all_zones` reported success with zero zones. A stub is
# only right for a name no provider defines.


def closure_providers(overlay_root):
    """Paths of the dylibs libSystem.B.dylib re-exports, in load-command order."""
    libsys = f"{overlay_root}/usr/lib/libSystem.B.dylib"
    r = subprocess.run(["llvm-objdump", "--macho", "--dylibs-used", libsys],
                       capture_output=True, text=True)
    paths = []
    for l in r.stdout.splitlines():
        l = l.strip()
        if l.endswith("reexport)") and l.startswith("/"):
            paths.append(overlay_root + l.split(" (")[0])
    if r.returncode != 0 or not paths:
        sys.exit(f"gen-cg-supl: cannot read the re-exports of {libsys}")
    return paths


def provider_exports(overlay_root):
    """Names defined in the export trie of any closure provider."""
    defined = set()
    for path in closure_providers(overlay_root):
        r = subprocess.run(["llvm-objdump", "--macho", "--exports-trie", path],
                           capture_output=True, text=True)
        # Two line shapes define a name: "0x<addr> <name>" and, for an alias
        # the provider re-exports from another image, "[re-export] <name>
        # (<target> from <image>)". libsystem_c defines _memmove, _memcpy,
        # _memchr, _memset, _strlen and _strchr only in the second form. Taking
        # only the first form left a stub (xorl %eax,%eax; ret) shadowing them:
        # the framework's std::string appends copied nothing.
        for l in r.stdout.splitlines():
            f = l.split()
            if len(f) > 1 and (f[0].startswith("0x") or f[0] == "[re-export]"):
                defined.add(f[1])
    return defined


if __name__ == "__main__" and len(sys.argv) == 3 and sys.argv[1] == "--providers":
    # build-suppl-stubs.sh: print the -reexport_library list, one path per line
    print("\n".join(closure_providers(sys.argv[2])))
    sys.exit(0)


def explicit_names():
    """Names this generator treats specially; never dropped as shadowing."""
    keep = set(ASM_BODIES) | set(DATA_VALUES)
    for k, v in FORWARD_NAMES.items():
        keep.add(k)
        keep.add(v)
    return keep


def main():
    argv = sys.argv[1:]
    datalist = set()
    if "--data-list" in argv:
        i = argv.index("--data-list")
        if os.path.exists(argv[i + 1]):
            datalist = {l.strip() for l in open(argv[i + 1]) if l.strip()}
        del argv[i:i + 2]
    src, dst = argv[0], argv[1]
    install = argv[2] if len(argv) > 2 else DEF_INSTALL
    overlay = argv[3] if len(argv) > 3 else None
    classes = []
    names = [l.strip() for l in open(src) if l.strip()]
    shadow = set()
    if overlay:
        shadow = (provider_exports(overlay) & set(names)) - explicit_names()
    # always-emit: a body name is emitted even when the installed export
    # list does not carry it — the forward that ends on it must find it here
    for extra in ASM_BODIES:
        if extra not in names:
            names.append(extra)
    out = [f"# generated by gen-cg-supl.py — supplement stubs for {install}",
           ".text"]
    n_text = n_data = n_fwd = n_skip = n_body = n_shadow = 0
    for name in names:
        if name in ASM_BODIES:
            out += [f".text", f".globl {name}", f"{name}:", ASM_BODIES[name]]
            n_body += 1
            continue
        if name in DATA_VALUES:
            out += [f".data", f".p2align 3", f".globl {name}", f"{name}:",
                    f".quad {DATA_VALUES[name]}"]
            n_data += 1
            continue
        if name in SKIP_NAMES:
            n_skip += 1
            continue
        if name in shadow:
            n_shadow += 1
            continue
        if name in FORWARD_NAMES:
            out += [f".text", f".globl {name}", f"{name}:",
                    f"    jmp {FORWARD_NAMES[name]}"]
            n_fwd += 1
        elif name.startswith(("_OBJC_CLASS_$_", "_OBJC_METACLASS_$_")):
            classes.append(name)
        elif name.startswith(DATA_PREFIXES) or R.is_known_data(name) or name in datalist:
            out += R.data_asm(name, strings=True)
            n_data += 1
        else:
            out += [f".text", f".globl {name}", f"{name}:",
                    "    xorl %eax, %eax", "    ret"]
            n_text += 1
    if classes:
        out.append(R.EMPTY_CACHE_S)
        open(dst + ".m", "w").write(R.classes_m(R.class_names(classes)))
        n_data += len(classes)
    elif os.path.exists(dst + ".m"):
        os.remove(dst + ".m")
    open(dst, "w").write("\n".join(out) + "\n")
    print(f"wrote {dst}: {n_text} text stubs, {n_data} data stubs, "
          f"{n_fwd} forwarding stubs, {n_body} asm bodies, "
          f"{n_skip} skipped, {n_shadow} dropped (a provider defines them), "
          f"{len(names)} total")
    print("link recipe:")
    print(f"  clang -target x86_64-apple-macos10.12 -c {dst} -o {dst}.o")
    print(f"  ld64.lld -dylib -arch x86_64 -platform_version macos 10.12 10.12 \\")
    print(f"    -undefined dynamic_lookup \\")
    print(f"    -install_name {install} -current_version 1.0.0 \\")
    print(f"    -compatibility_version 1.0.0 -o {dst}.dylib {dst}.o")


if __name__ == "__main__":
    main()
