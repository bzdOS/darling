#!/usr/bin/env python3
"""patch-unix2003-imports.py — rewrite $UNIX2003 import names in a Mach-O to
their unsuffixed base, when the base is exported by a provider dylib.

Why: the rebuilt libsystem_malloc.dylib imports the legacy UNIX2003 aliases
(_kill$UNIX2003, _mprotect$UNIX2003, _sleep$UNIX2003, _write$UNIX2003) but the
guest system dylibs export only the unsuffixed names, so dyld fails with
"Symbol not found: _mprotect$UNIX2003 ... Expected in: flat namespace". The
suffix is purely a name decoration; the base symbol is the same ABI.

What it does: for every undefined $UNIX2003 symbol in the target, compute the
base name; if the base is exported by one of the provider dylibs, overwrite the
string-table entry with base + NUL (the new name is shorter, so nothing moves).
n_strx entries are not touched; the script refuses to rename a name that some
other n_strx points strictly inside of. The symbol table itself is untouched.

Usage: patch-unix2003-imports.py <target.dylib> <provider.dylib> [provider ...]
"""
import struct
import subprocess
import sys
import hashlib

SUFFIX = b"$UNIX2003"


def load_commands(data):
    magic = struct.unpack_from("<I", data, 0)[0]
    if magic != 0xFEEDFACF:
        raise SystemExit("not a thin 64-bit little-endian Mach-O")
    ncmds = struct.unpack_from("<I", data, 16)[0]
    o = 32
    cmds = []
    for _ in range(ncmds):
        cmd, cmdsize = struct.unpack_from("<II", data, o)
        cmds.append((cmd, o, cmdsize))
        o += cmdsize
    return cmds


def provider_exports(paths):
    exported = set()
    for p in paths:
        out = subprocess.run(["nm", "-gU", p], capture_output=True, text=True).stdout
        for line in out.splitlines():
            parts = line.split()
            if len(parts) >= 3:
                exported.add(parts[2])
    return exported


def read_uleb(data, o):
    result = 0
    shift = 0
    while True:
        b = data[o]
        o += 1
        result |= (b & 0x7F) << shift
        if not (b & 0x80):
            return result, o
        shift += 7


def lazy_bind_symbols(data, off, size):
    """Yield (string_offset, name) for SET_SYMBOL_TRAILING_FLAGS_IMM in lazy bind."""
    o = off
    end = off + size
    while o < end:
        op = data[o]
        imm = op & 0xF0
        if imm == 0x00:  # DONE
            o += 1
        elif imm == 0x10 or imm == 0x30 or imm == 0x50 or imm == 0xB0:
            o += 2
        elif imm == 0x20 or imm == 0x60 or imm == 0x80:
            _, o = read_uleb(data, o + 1)
        elif imm == 0x40:  # SET_SYMBOL_TRAILING_FLAGS_IMM, string follows
            s = o + 1
            e = data.find(b"\x00", s)
            yield s, bytes(data[s:e])
            o = e + 1
        elif imm == 0x70:
            _, o = read_uleb(data, o + 2)
        elif imm == 0x90:
            o += 1
        elif imm == 0xA0 or imm == 0xC0:
            _, o = read_uleb(data, o + 1)
            if imm == 0xC0:
                _, o = read_uleb(data, o)
        else:
            o += 1


def main():
    if len(sys.argv) < 3:
        sys.exit(__doc__)
    target = sys.argv[1]
    providers = sys.argv[2:]
    data = bytearray(open(target, "rb").read())
    symtab = None
    lazy = None
    for cmd, o, cmdsize in load_commands(data):
        if cmd == 0x2:  # LC_SYMTAB
            symtab = struct.unpack_from("<IIII", data, o + 8)
        if cmd in (0x22, 0x80000022):  # LC_DYLD_INFO(_ONLY)
            lazy_off, lazy_size = struct.unpack_from("<II", data, o + 8 + 24)
            lazy = (lazy_off, lazy_size)
    if symtab is None:
        sys.exit("no LC_SYMTAB")
    symoff, nsyms, stroff, strsize = symtab
    strtab = bytes(data[stroff:stroff + strsize])

    nstrx = [struct.unpack_from("<I", data, symoff + i * 16)[0] for i in range(nsyms)]
    used = set(nstrx)

    def read_cstr(idx):
        end = strtab.find(b"\x00", idx)
        return strtab[idx:end if end >= 0 else len(strtab)]

    exported = provider_exports(providers)
    renamed, skipped = [], []
    for i, idx in enumerate(nstrx):
        name = read_cstr(idx)
        if not name.endswith(SUFFIX):
            continue
        base = name[:-len(SUFFIX)]
        base_s = base.decode("ascii", "replace")
        if base_s not in exported:
            skipped.append((base_s, "base not exported by providers"))
            continue
        inside = [s for s in used if idx < s < idx + len(name)]
        if inside:
            skipped.append((base_s, "n_strx points inside the name: %r" % inside))
            continue
        abs_off = stroff + idx
        data[abs_off:abs_off + len(name)] = base + b"\x00" * (len(name) - len(base))
        renamed.append(base_s)

    renamed_lazy = []
    i = 0
    while True:
        j = data.find(SUFFIX, i)
        if j < 0:
            break
        s = j
        while s > 0 and (chr(data[s - 1]).isalnum() or data[s - 1] == 0x5F):
            s -= 1
        name = bytes(data[s:j + len(SUFFIX)])
        base = name[:-len(SUFFIX)]
        base_s = base.decode("ascii", "replace")
        if base_s in exported:
            data[s:s + len(name)] = base + b"\x00" * (len(name) - len(base))
            renamed_lazy.append(base_s)
        i = j + len(SUFFIX)

    open(target, "wb").write(data)
    print("renamed strtab (%d): %s" % (len(renamed), ", ".join(renamed)))
    print("renamed inline (%d): %s" % (len(renamed_lazy), ", ".join(renamed_lazy)))
    print("skipped (%d): %s" % (len(skipped), skipped))
    print("sha256 %s" % hashlib.sha256(data).hexdigest())


if __name__ == "__main__":
    main()
