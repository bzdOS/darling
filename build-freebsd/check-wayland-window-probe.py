#!/usr/bin/env python3
# check-wayland-window-probe.py — verify tests/src/wayland-window-create.m
# against the built Wayland.backend dylib BEFORE anything is built or run.
#
# WHY: the backend's sources were never committed and cannot be rebuilt (see
# tests/vendor/wayland-backend/README.md), so the probe declares the backend's
# private methods itself. Running a guest process needs root, and the first
# thing an ABI mistake does is abort -- or worse, quietly write through a
# garbage pointer -- in a way that looks like a backend bug. So the probe's
# declarations are read out of its own source and compared against the
# encodings actually present in the dylib's ObjC metadata.
#
# USAGE: python3 build-freebsd/check-wayland-window-probe.py <Wayland-dylib> [probe.m]
#   probe.m defaults to tests/src/wayland-window-create.m next to this script.
#
# EXIT: 0 if every declaration matches, 1 otherwise.
#
# WHAT IS COMPARED, AND WHY NOT THE WHOLE ENCODING
#
# The numeric parts of an encoding are compiler bookkeeping: argument offsets
# are packed, so -[WaylandWindow _acquireBackBufferForWidth:height:] is
# "24@0:8i16i20" -- the two ints sit at 16 and 20, not 16 and 24. Synthesising
# those offsets to match would be guesswork about the compiler. What actually
# breaks a call is the KIND of the return value and of each argument, so those
# are what gets compared, with struct types compared by shape.
#
# That still catches the mistakes that were really made here, twice:
#   - returning `id` where the method returns a pointer to a struct;
#   - "fixing" that to a by-value struct return, which is a different wrong
#     answer: a by-value struct is encoded {..}, the real return is ^{..}, and
#     a caller declaring it by value passes a temporary's address in RDI where
#     the callee reads self.
# and also a plain void/int or object/pointer slip.
#
# WHAT IS NOT COMPARED, AND CANNOT BE
#
# The encoding fixes the field TYPES of the buffer struct (ptr, ptr, uint64,
# int x4, char) but not which int is width and which is height. Only the
# disassembly settles that, and it says offset 28 is width, 32 is height:
#     movl 0x1c(%rax),%eax ; cmpl -0x1c(%rbp),%eax
#     movl 0x20(%rax),%eax ; cmpl -0x20(%rbp),%eax
# The probe carries the two unidentified ints as field_24/field_36, prints
# them raw, and never addresses pixels with them.

import re
import struct
import sys

LC_SEGMENT_64 = 0x19
MH_MAGIC_64 = 0xFEEDFACF

# Methods the probe declares that the backend does NOT implement, because
# AppKit provides them. These cannot be checked against the backend at all and
# must not fail the build:
#   currentDisplay      +[NSDisplay currentDisplay], in NSDisplay.m
#   backingScaleFactor  -[NSScreen backingScaleFactor], in AppKit
#   windowNumber        -[CGWindow windowNumber], CoreGraphics/CGWindow.m:124
#                       returns `(NSInteger) self`
# They are declared in the probe only to satisfy the compiler; at run time
# they resolve against AppKit.framework.
APPKIT_PROVIDED = {
    "currentDisplay": "+[NSDisplay currentDisplay] (NSDisplay.m)",
    "backingScaleFactor": "-[NSScreen backingScaleFactor] (AppKit)",
    "windowNumber": "-[CGWindow windowNumber] (CoreGraphics/CGWindow.m:124)",
}

# Declared in the probe and overridden by the backend. Used as a completeness
# check: if one of these stops being parsed out of the probe, the gate is no
# longer looking at what it thinks it is.
BACKEND_METHODS = {"shm", "compositor", "wmBase", "newWindowWithDelegate:",
                   "screens", "setFrame:", "frame", "styleMask",
                   "_acquireBackBufferForWidth:height:", "flushBuffer"}


def kinds_compatible(declared, actual):
    """Are a declared kind and the backend's kind ABI-compatible?

    Three deliberate equivalences, each of which bit during development:
      * `void *` against a typed pointer (^ vs ^{wl_shm=}). The probe only
        needs the pointer back, both come back in RAX, and at the ABI level
        pointer types are interchangeable. Reported, not failed.
      * a pointer to a struct against a pointer to that same struct spelled
        out: the probe says `WLBackBuffer *`, the encoding says
        `^{?=^{wl_buffer}^vQiiiic}`. Same thing.
      * the pointee struct itself: `^{?}` is a pointer to an unspelled struct.
    Everything else has to match exactly.
    """
    if declared == actual:
        return True, ""
    if declared == "^" and actual.startswith("^{"):
        return True, "declared void *, backend returns a typed pointer"
    if declared == "^{?}" and actual.startswith("^{"):
        return True, "pointer to the same struct, pointee spelled out in the encoding"
    if declared == "!struct-by-value!":
        return False, ("declared as a by-value struct return, but the method "
                       "returns a POINTER to it: a by-value caller passes a "
                       "temporary's address in RDI where the callee reads self")
    if actual == "!struct-by-value!":
        return False, "the backend returns a struct by value, the probe does not"
    return False, ""


# ---------------------------------------------------------------- Mach-O ---

class MachO:
    def __init__(self, path):
        self.d = open(path, "rb").read()
        magic, _, _, _, ncmds, _, _, _ = struct.unpack_from("<IiiIIIII", self.d, 0)
        if magic != MH_MAGIC_64:
            raise SystemExit("not a 64-bit Mach-O: %08x" % magic)
        self.sections = {}
        off = 32
        for _ in range(ncmds):
            cmd, cmdsize = struct.unpack_from("<II", self.d, off)
            if cmd == LC_SEGMENT_64:
                nsects = struct.unpack_from("<I", self.d, off + 64)[0]
                so = off + 72
                for _i in range(nsects):
                    sectname = self.d[so:so + 16].rstrip(b"\0").decode()
                    segname = self.d[so + 16:so + 32].rstrip(b"\0").decode()
                    addr, size, foff = struct.unpack_from("<QQI", self.d, so + 32)
                    self.sections.setdefault(sectname, (addr, size, foff))
                    so += 80
            off += cmdsize

    def to_fo(self, vmaddr):
        for _n, (addr, size, foff) in self.sections.items():
            if addr <= vmaddr < addr + size:
                return foff + (vmaddr - addr)
        return None

    def cstr(self, vmaddr):
        fo = self.to_fo(vmaddr)
        if fo is None:
            return "<?>%x>" % vmaddr
        end = self.d.index(b"\0", fo)
        return self.d[fo:end].decode("utf-8", "replace")


def dylib_methods(path):
    """selector -> list of (class, raw encoding)"""
    m = MachO(path)
    out = {}
    _addr, size, foff = m.sections["__objc_classlist"]
    for i in range(size // 8):
        cls = struct.unpack_from("<Q", m.d, foff + i * 8)[0]
        cfo = m.to_fo(cls)
        if cfo is None:
            continue
        ro = m.to_fo(struct.unpack_from("<Q", m.d, cfo + 32)[0] & ~1)
        if ro is None:
            continue
        name_ptr, base_methods = struct.unpack_from("<QQ", m.d, ro + 24)
        cname = m.cstr(name_ptr)
        if not base_methods:
            continue
        mfo = m.to_fo(base_methods)
        if mfo is None:
            continue
        _entsize, cnt = struct.unpack_from("<II", m.d, mfo)
        for k in range(cnt):
            sel, types, _imp = struct.unpack_from("<QQQ", m.d, mfo + 8 + k * 24)
            out.setdefault(m.cstr(sel), []).append((cname, m.cstr(types)))
    return out


# ------------------------------------------------------------- encodings ---

def _one_type(rest):
    """Consume one type token from `rest`; return (kind, remainder)."""
    if rest[0] == "{":
        depth, i = 0, 0
        while i < len(rest):
            if rest[i] == "{":
                depth += 1
            elif rest[i] == "}":
                depth -= 1
                if depth == 0:
                    break
            i += 1
        return rest[:i + 1], rest[i + 1:]
    if rest[0] == "^":
        if len(rest) > 1 and rest[1] == "{":
            depth, i = 0, 1
            while i < len(rest):
                if rest[i] == "{":
                    depth += 1
                elif rest[i] == "}":
                    depth -= 1
                    if depth == 0:
                        break
                i += 1
            return "^" + rest[1:i + 1], rest[i + 1:]
        return "^", rest[1:]
    return rest[0], rest[1:]


def split_encoding(enc):
    """raw encoding -> (return kind, [arg kinds]) with struct shapes kept.

    The layout is <return><framesize>@0:8<args...>, so the return type comes
    FIRST and the frame size is in the middle -- not at the front.
    """
    ret, rest = _one_type(enc)
    m = re.match(r"^[0-9]+@0:8", rest)
    if not m:
        return None, None
    rest = rest[m.end():]
    args = []
    while rest:
        if rest[0].isdigit():          # argument offset
            rest = re.sub(r"^[0-9]+", "", rest)
            continue
        kind, rest = _one_type(rest)
        args.append(kind)
    return ret, args


# -------------------------------------------------------- probe's source ---

# C type (as written in the probe) -> encoding kind. Longest match wins.
CTYPE_KIND = [
    ("unsigned long long", "Q"),
    ("unsigned long", "Q"),
    ("struct wl_buffer *", "^"),
    ("WLBackBuffer *", "^{?}"),
    # A bare WLBackBuffer is the struct BY VALUE, which is a different and
    # wrong ABI: the caller would pass a temporary's address in RDI where the
    # callee reads self. Recognised explicitly so it is reported as that,
    # rather than falling through as an untypable declaration.
    ("WLBackBuffer", "!struct-by-value!"),
    ("void *", "^"),
    ("const char *", "^"),
    ("CGRect", "{CGRect={CGPoint=dd}{CGSize=dd}}"),
    ("CGPoint", "{CGPoint=dd}"),
    ("CGSize", "{CGSize=dd}"),
    ("id", "@"),
    ("Class", "#"),
    ("SEL", ":"),
    ("BOOL", "c"),
    ("bool", "B"),
    ("int", "i"),
    ("short", "s"),
    ("long", "q"),
    ("double", "d"),
    ("float", "f"),
    ("void", "v"),
]


def ctype_kind(t):
    t = t.strip()
    # an object pointer: anything ending in " *" that is not handled above
    for name, kind in CTYPE_KIND:
        if t == name:
            return kind
    if t.endswith("*"):
        return "@"
    return None


def parse_probe(path):
    """Pull method declarations out of the probe's protocols and local
    @interface blocks. Returns [(selector, ret kind, [arg kinds], text)]."""
    src = open(path).read()
    # drop comments so commented-out declarations cannot be picked up
    src = re.sub(r"/\*.*?\*/", " ", src, flags=re.S)
    src = re.sub(r"//[^\n]*", " ", src)

    blocks = []
    for pat in (r"@protocol\s+\w+\s*<[^>]*>(.*?)@end",
                r"@interface\s+(\w+)\s*:\s*(\w+)(.*?)@end"):
        for m in re.finditer(pat, src, flags=re.S):
            blocks.append(m.group(m.lastindex))

    found = []
    decl = re.compile(r"^\s*([-+])\s*\(([^)]*)\)\s*([^;{]+);", re.M)
    for body in blocks:
        for m in decl.finditer(body):
            ret = m.group(2)
            rest = m.group(3)
            # selector pieces: "name:(TYPE)arg" possibly repeated
            parts = re.findall(r"(\w+)?\s*:\s*\(([^)]*)\)\s*\w*|(\w+)\s*$", rest)
            sel = ""
            args = []
            ok = True
            for named, atype, bare in parts:
                if bare:
                    sel += bare
                    continue
                sel += named + ":"
                k = ctype_kind(atype)
                if k is None:
                    ok = False
                args.append(k)
            if not ok or not sel:
                print("SKIP  could not type a declaration: %s" % m.group(0).strip())
                continue
            rk = ctype_kind(ret)
            if rk is None:
                print("SKIP  unknown return type in: %s" % m.group(0).strip())
                continue
            found.append((sel, rk, args, m.group(0).strip()))
    return found


def main():
    if len(sys.argv) < 2:
        raise SystemExit(__doc__)
    dylib = sys.argv[1]
    here = __file__.rsplit("/", 1)[0]
    probe = sys.argv[2] if len(sys.argv) > 2 else here + "/../tests/src/wayland-window-create.m"

    found = dylib_methods(dylib)
    print("dylib: %s" % dylib)
    print("probe: %s" % probe)
    print("classes: %d, distinct selectors: %d\n"
          % (len(set(c for v in found.values() for c, _t in v)), len(found)))

    decls = parse_probe(probe)
    print("parsed %d method declaration(s) from the probe\n" % len(decls))

    bad = 0
    seen = set()
    for sel, rk, args, text in sorted(decls):
        if sel in seen:
            continue          # declared in more than one local block
        seen.add(sel)
        if sel in APPKIT_PROVIDED:
            print("ok    %-32s provided by AppKit, not the backend: %s"
                  % (sel, APPKIT_PROVIDED[sel]))
            continue
        hits = found.get(sel)
        if not hits:
            print("MISSING  %-32s probe declares it, the backend does not implement it"
                  % sel)
            print("         declared as: %s" % text)
            bad += 1
            continue
        cname, enc = hits[0]
        drk, dargs = split_encoding(enc)
        if drk is None:
            print("UNPARSED %-31s could not parse the encoding %s" % (sel, enc))
            bad += 1
            continue
        okr, note_r = kinds_compatible(rk, drk)
        okargs = len(args) == len(dargs) and all(
            kinds_compatible(a, b)[0] if a and b else a == b
            for a, b in zip(args, dargs))
        notes = [note_r] if note_r else []
        if okr and okargs:
            print("ok    %-32s %-14s %s(%s)%s"
                  % (sel, cname, drk, ",".join(dargs),
                     "   [%s]" % "; ".join(notes) if notes else ""))
        else:
            print("DIFF    %-32s %-14s probe says %s(%s), backend is %s(%s)"
                  % (sel, cname, rk, ",".join(a or "?" for a in args), drk,
                     ",".join(dargs)))
            print("        declared as: %s" % text)
            bad += 1

    missing_from_probe = sorted(set(BACKEND_METHODS) - seen)
    if missing_from_probe:
        print("\nnote: expected in the probe but not parsed from it: %s"
              % ", ".join(missing_from_probe))
        bad += 1

    print("\n%s: %d problem(s)" % ("FAIL" if bad else "PASS", bad))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
