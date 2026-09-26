#!/usr/bin/env python3
"""decode-crash.py LOG OVERLAY_DIR TESTS_DIR

Decodes a darling mldr crash dump: pairs the dyld segment mappings
("dyld: Mapping <path>" followed by "__TEXT at 0xLO->0xHI") from the log with
the mldr register/stack dump, resolves rip and every stack slot to
image + offset + nearest symbol (via nm), and prints a readable trace.

Handles fat/thin Mach-O vaddr skew by trying both V and V-0x1000 symbol
lookups and keeping the closer match.
"""
import bisect
import re
import struct
import subprocess
import sys
import os

FAT_SKEW = 0x1000  # fat-file slice offset: nm thin vaddrs vs runtime offsets


def load_segments(log):
    """Return [(lo, hi, path)] from dyld segment lines."""
    segs = []
    path = None
    for line in open(log, errors="replace"):
        m = re.search(r"dyld: Mapping (\S+)", line)
        if m:
            path = m.group(1)
            continue
        m = re.search(r"__TEXT at 0x([0-9A-Fa-f]+)->0x([0-9A-Fa-f]+)", line)
        if m and path:
            segs.append((int(m.group(1), 16), int(m.group(2), 16), path))
    return segs


def add_pseudo_segments(segs, log, tests, overlay):
    """dyld and the main executable are mapped by mldr BEFORE
    DYLD_PRINT_SEGMENTS logging starts, so they never appear in the segment
    list and crashes inside them show as 'unmapped'. mldr's DEBUG lines
    (DARLING_FREEBSD build) print their base addresses — turn those into
    pseudo-segments, appended so real mappings keep priority."""
    text = open(log, errors="replace").read()
    m = re.search(r"DEBUG dyld mh=0x([0-9a-fA-F]+)", text)
    if m:
        lo = int(m.group(1), 16)
        # mldr logs the dyld_all_image_infos size, not the image size —
        # derive the real image size from the dyld file's segment vmsizes
        dyld_size = 0
        try:
            dyld_file = os.path.join(overlay, "usr/lib/dyld")
            with open(dyld_file, "rb") as f:
                data = f.read(8192 + 65536)
            base_off = 0
            if data[:4] == b"\xca\xfe\xba\xbe":       # fat, big-endian header
                narch = struct.unpack_from(">I", data, 4)[0]
                for i in range(narch):
                    cputype, _, off_a = struct.unpack_from(">III", data, 8 + i * 20)
                    if cputype == 0x01000007:          # CPU_TYPE_X86_64
                        base_off = off_a
                        break
            elif data[:4] == b"\xbe\xba\xfe\xca":     # fat, little-endian header
                narch = struct.unpack_from("<I", data, 4)[0]
                for i in range(narch):
                    cputype, _, off_a = struct.unpack_from("<III", data, 8 + i * 20)
                    if cputype == 0x01000007:
                        base_off = off_a
                        break
            if base_off:
                with open(dyld_file, "rb") as f:
                    f.seek(base_off)
                    data = f.read(65536)
                base_off = 0
            base_magic = struct.unpack_from("<I", data, base_off)[0]
            endian = "<" if base_magic in (0xFEEDFACF, 0xFEEDFACE) else ">"
            is64 = base_magic in (0xFEEDFACF, 0xCFFAEDFE)
            ncmds = struct.unpack_from(endian + "I", data, base_off + 16)[0]
            off = base_off + (32 if is64 else 28)
            for _ in range(ncmds):
                cmd, cmdsize = struct.unpack_from(endian + "II", data, off)
                if cmd == 0x19:  # LC_SEGMENT_64
                    vaddr, vmsize = struct.unpack_from(endian + "QQ", data, off + 24)
                    dyld_size = max(dyld_size, vaddr + vmsize)
                elif cmd == 0x1:  # LC_SEGMENT
                    vaddr, vmsize = struct.unpack_from(endian + "II", data, off + 24)
                    dyld_size = max(dyld_size, vaddr + vmsize)
                off += cmdsize
        except Exception:
            pass
        if lo and dyld_size:
            segs.append((lo, lo + dyld_size - 1, "/usr/lib/dyld"))
    m = re.search(r"DEBUG pre-start: mh=0x([0-9a-fA-F]+)", text)
    if m:
        lo = int(m.group(1), 16)
        # the launched binary's name appears in the log ("binary : .../tests/X"
        # or "Running: ... mldr /tmp/darling-local-overlay/X")
        main = None
        m2 = (re.search(r"binary\s*:\s*\S*/tests/(\S+)", text)
              or re.search(r"mldr (\S+/(\S+))\s*$", text, re.M))
        if m2:
            name = m2.group(2) if m2.lastindex and m2.lastindex >= 2 else m2.group(1)
            cand = os.path.join(tests, os.path.basename(name))
            if os.path.exists(cand):
                main = cand
        if main:
            segs.append((lo, lo + 0x1000000, main))
    return segs


_nm_cache = {}


def nm_symbols(file):
    """Sorted [(vaddr, symbol)] for a Mach-O, cached."""
    if file in _nm_cache:
        return _nm_cache[file]
    syms = []
    try:
        out = subprocess.run(["nm", "-n", file], capture_output=True,
                             text=True, timeout=60).stdout
        for line in out.splitlines():
            parts = line.split()
            if len(parts) >= 3 and re.fullmatch(r"[0-9a-fA-F]+", parts[0]):
                syms.append((int(parts[0], 16), " ".join(parts[2:])))
    except Exception:
        pass
    syms.sort()
    _nm_cache[file] = syms
    return syms


def nearest(file, vaddr):
    syms = nm_symbols(file)
    if not syms:
        return None
    addrs = [a for a, _ in syms]
    i = bisect.bisect_right(addrs, vaddr) - 1
    if i < 0:
        return None
    a, name = syms[i]
    return f"{name}+{vaddr - a:#x}"


def host_path(guest, overlay, tests):
    if guest.startswith("/System/") or guest.startswith("/usr/"):
        return overlay + guest
    if guest.startswith("/tmp/darling-local-overlay/"):
        return guest  # host-visible copy
    if os.path.isabs(guest) and os.path.exists(guest):
        return guest  # absolute host path (pseudo-segments)
    base = os.path.basename(guest)
    if os.path.exists(os.path.join(tests, base)):
        return os.path.join(tests, base)
    return os.path.join(overlay, guest.lstrip("/"))


def resolve(addr, segs, overlay, tests):
    for lo, hi, path in segs:
        if lo <= addr <= hi:
            off = addr - lo
            hp = host_path(path, overlay, tests)
            sym = nearest(hp, off) if os.path.exists(hp) else None
            if not sym:
                sym = nearest(hp, off - FAT_SKEW) if os.path.exists(hp) else None
            loc = f"{path}+{off:#x}"
            return f"{loc:<58} {sym or '?'}"
    return None


def main():
    log, overlay, tests = sys.argv[1], sys.argv[2], sys.argv[3]
    text = open(log, errors="replace").read()
    segs = load_segments(log)
    n_real = len(segs)
    segs = add_pseudo_segments(segs, log, tests, overlay)

    fatal = re.search(r"FATAL signal (\d+).*?addr=(0x[0-9a-f]+)", text)
    rip = re.search(r"rip=(0x[0-9a-fA-F]+)", text)
    if not fatal or not rip:
        print("no crash found in log")
        return
    print(f"crash: signal {fatal.group(1)} at {fatal.group(2)}")
    if segs:
        print(f"images mapped: {n_real} (+{len(segs)-n_real} pseudo from mldr DEBUG)")

    r = resolve(int(rip.group(1), 16), segs, overlay, tests)
    print(f"  rip  {rip.group(1)}  {r or 'unmapped'}")

    for m in re.finditer(r"\[rsp\s*(\d+)\] (0x[0-9a-fA-F]+)", text):
        off, val = m.group(1), int(m.group(2), 16)
        r = resolve(val, segs, overlay, tests)
        if r:
            print(f"  rsp+{off:<3} {m.group(2)}  {r}")

    # mldr's deep stack dump ("stack dump at rsp=..." or "guest stack dump
    # at rsp=..." followed by raw hex lines) — scan every hex word as a
    # candidate return address
    dump = re.search(r"(?:guest )?stack dump at rsp=0x[0-9a-fA-F]+:\n(.*)", text, re.S)
    shown = 0
    if dump:
        for m in re.finditer(r"0x([0-9a-fA-F]{9,16})", dump.group(1)):
            val = int(m.group(1), 16)
            r = resolve(val, segs, overlay, tests)
            if r:
                print(f"  stack {m.group(0)}  {r}")
                shown += 1
                if shown >= 40:
                    break

    if not segs:
        print("\n(no DYLD_PRINT_SEGMENTS mappings in log; rerun with "
              "DYLD_DEBUG='DYLD_PRINT_SEGMENTS=1' for symbolized traces)")


if __name__ == "__main__":
    main()
