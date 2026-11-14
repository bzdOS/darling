#!/usr/bin/env python3
"""check-suppl-bodies.py -- fail if any function body in a supplement dylib has an
outward pc-relative reference.

A body is the byte range of one __TEXT,__text symbol (its start to the next symbol
start). Every direct call/jmp/jcc target and every RIP-relative memory operand is
resolved to an address. The reference is accepted only if it stays inside the body,
or lands in __stubs/__stub_helper/__got/__la_symbol_ptr (the linker's own import
indirection), or is a listed forwarding stub (FORWARDS: a one-instruction
`jmp <local symbol>` emitted by the generator on purpose). Anything else is what
a verbatim-copied provider body looks like after the move: the displacement is
still the provider's, the target is not.

Usage: check-suppl-bodies.py <dylib> [<dylib> ...]   (exit 1 on any violation)
"""
import re
import subprocess
import sys

# Forwarding stubs the generator emits: source symbol -> the only allowed local target.
FORWARDS = {"___bzero": "_bzero", "_bzero": "__platform_bzero"}
IMPORT_SECTS = ("__stubs", "__stub_helper", "__got", "__la_symbol_ptr")

LABEL = re.compile(r"^(\S+):$")
INSN = re.compile(r"^\s*([0-9a-f]+):\s+([0-9a-f]{2}(?: [0-9a-f]{2})*)\s+(\S.*)$")
BRANCH = re.compile(r"^(call|jmp|j[a-z]+|loop[a-z]*|jrcxz|jecxz)q?\s+0x([0-9a-f]+)\s*$")
RIPREL = re.compile(r"(-?0x[0-9a-f]+)\(%rip\)")


def run(*a):
    return subprocess.run(a, check=True, capture_output=True, text=True).stdout


def sections(path):
    out = {}
    for l in run("llvm-objdump", "--macho", "--section-headers", path).splitlines():
        m = re.match(r"\s*\d+\s+(\S+)\s+([0-9a-f]+)\s+([0-9a-f]+)", l)
        if m:
            out[m.group(1)] = (int(m.group(3), 16), int(m.group(3), 16) + int(m.group(2), 16))
    return out


def check(path):
    sects = sections(path)
    imports = [sects[s] for s in IMPORT_SECTS if s in sects]
    text = sects["__text"]
    # symbol ranges
    syms, cur = [], None
    insns = []
    for l in run("llvm-objdump", "--macho", "-d", "--print-imm-hex", path).splitlines():
        m = LABEL.match(l)
        if m:
            cur = [m.group(1), None, None]
            syms.append(cur)
            continue
        m = INSN.match(l)
        if m and cur is not None:
            addr = int(m.group(1), 16)
            ln = len(m.group(2).split())
            if cur[1] is None:
                cur[1] = addr
            cur[2] = addr + ln
            insns.append((cur[0], addr, ln, m.group(3)))
    # a body runs up to the next symbol, so trailing padding belongs to it
    ends = {}
    for i, (n, st, en) in enumerate(syms):
        ends[n] = (st, syms[i + 1][1] if i + 1 < len(syms) and syms[i + 1][1] else text[1])
    name_at = {st: n for n, st, _ in syms}
    bad, nref = [], 0
    for name, addr, ln, txt in insns:
        lo, hi = ends[name]
        tgt = None
        m = BRANCH.match(txt)
        if m:
            tgt = int(m.group(2), 16)
        else:
            m = RIPREL.search(txt)
            if m and not txt.startswith("nop"):
                tgt = addr + ln + int(m.group(1), 16)
        if tgt is None:
            continue
        nref += 1
        if lo <= tgt < hi or any(a <= tgt < b for a, b in imports):
            continue
        if FORWARDS.get(name) and name_at.get(tgt) == FORWARDS[name]:
            continue
        bad.append(f"  OUTWARD {name}+{addr - lo:#x}: {txt.strip()!r} -> {tgt:#x}"
                   f" (body {lo:#x}..{hi:#x}, lands in {name_at.get(tgt, 'mid-symbol/unknown')})")
    print(f"{path}: {len(syms)} bodies, {len(insns)} insns, {nref} pc-relative refs, "
          f"{len(bad)} outward")
    for b in bad:
        print(b)
    return not bad


if __name__ == "__main__":
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    ok = all([check(p) for p in sys.argv[1:]])
    print("check-suppl-bodies: " + ("OK" if ok else "FAIL"))
    sys.exit(0 if ok else 1)
