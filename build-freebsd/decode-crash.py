#!/usr/bin/env python3
"""decode-crash.py LOG OVERLAY_DIR TESTS_DIR

Decodes a darling mldr crash dump: pairs the dyld segment mappings
("dyld: Mapping <path>" or "dyld: Main executable mapped <path>", each followed
by "__TEXT at 0xLO->0xHI") from the log with the mldr register/stack dump,
resolves rip and every stack slot to image + offset + nearest symbol (via nm),
and prints a readable trace.

An address no known image covers is reported as `unmapped (no image covers this
address)` and is NOT attributed to anything. That is a result, not a gap: a
run of build-freebsd/SIGSEGV-SECOND-DLOPEN.md had its rip reported as
`tests/wayland-window-create-macho+0x86a759` against a probe binary of 19,048
bytes — an offset far past the end of the file — because the main executable's
range used to be invented as a flat 16 MB starting at its load address. The
span is now derived from the image itself, and when it cannot be derived, the
image is left out rather than guessed at.

Handles fat/thin Mach-O vaddr skew by falling back to a V-0x1000 symbol lookup
when the V lookup finds nothing.
"""
import bisect
import re
import struct
import subprocess
import sys
import os

FAT_SKEW = 0x1000  # fat-file slice offset: nm thin vaddrs vs runtime offsets


def load_segments(log):
    """Return ([(lo, hi, path)], n_dylib) from dyld segment lines.

    Both prefixes matter and dropping either one costs a real image: dyld
    announces a dylib as `dyld: Mapping <path>` and the main executable as
    `dyld: Main executable mapped <path>`. Counting only the first is how this
    tool reported "58 images mapped" for a log that maps 59: the main
    executable's own `__TEXT at` line was there all along, carrying a prefix
    the segment parser did not recognise, and it was silently dropped."""
    segs = []
    path = None
    n_dylib = 0
    for line in open(log, errors="replace"):
        m = re.search(r"dyld: Mapping (\S+)", line)
        if m:
            path = m.group(1)
            n_dylib += 1
            continue
        m = re.search(r"dyld: Main executable mapped (\S+)", line)
        if m:
            path = m.group(1)
            continue
        m = re.search(r"__TEXT at 0x([0-9A-Fa-f]+)->0x([0-9A-Fa-f]+)", line)
        if m and path:
            segs.append((int(m.group(1), 16), int(m.group(2), 16), path))
    return segs, n_dylib


def mach_o_span(path):
    """Highest (vaddr + vmsize) over a Mach-O's MAPPED segments, i.e. how far
    the image reaches above its mach header. 0 if it cannot be read.

    __PAGEZERO is excluded, and it is the whole ballgame. It is a reservation,
    never a mapping, and it carries vmsize 0x100000000 — taking the max over it
    produces a span of "base + 4 GB + image", which swallows every address for
    4 GB above the image. That is the exact number behind the bogus
    `wayland-window-create-macho+0x86a759` in
    build-freebsd/SIGSEGV-SECOND-DLOPEN.md: the old flat 0x1000000 fudge
    reproduced the same lie over a smaller radius, and a plain max over the
    segment table reproduces it over a much larger one. The span returned is
    measured from the lowest MAPPED segment (which is where the mach header
    sits), not from zero, because these images start at 0x100000000.

    Fat files are sliced the way dyld slices them: the x86_64 arch is picked
    out and the segment table inside that slice is what gets walked."""
    try:
        with open(path, "rb") as f:
            data = f.read(8192 + 65536)
        if len(data) < 32:
            return 0
        base_off = 0
        if data[:4] in (b"\xca\xfe\xba\xbe", b"\xbe\xba\xfe\xca"):  # fat
            be = data[:4] == b"\xca\xfe\xba\xbe"
            e = ">" if be else "<"
            narch = struct.unpack_from(e + "I", data, 4)[0]
            for i in range(narch):
                cputype, _, off_a = struct.unpack_from(e + "III", data, 8 + i * 20)
                if cputype == 0x01000007:          # CPU_TYPE_X86_64
                    base_off = off_a
                    break
            if base_off:
                with open(path, "rb") as f:
                    f.seek(base_off)
                    data = f.read(65536)
                base_off = 0
        base_magic = struct.unpack_from("<I", data, base_off)[0]
        endian = "<" if base_magic in (0xFEEDFACF, 0xFEEDFACE) else ">"
        is64 = base_magic in (0xFEEDFACF, 0xCFFAEDFE)
        ncmds = struct.unpack_from(endian + "I", data, base_off + 16)[0]
        off = base_off + (32 if is64 else 28)
        lo = None
        hi = 0
        for _ in range(ncmds):
            cmd, cmdsize = struct.unpack_from(endian + "II", data, off)
            if cmdsize == 0:
                break
            if cmd in (0x1, 0x19):  # LC_SEGMENT / LC_SEGMENT_64
                segname = data[off + 8:off + 24].split(b"\0")[0]
                if cmd == 0x19:
                    vaddr, vmsize = struct.unpack_from(endian + "QQ", data, off + 24)
                else:
                    vaddr, vmsize = struct.unpack_from(endian + "II", data, off + 24)
                if segname != b"__PAGEZERO" and vmsize:
                    lo = vaddr if lo is None else min(lo, vaddr)
                    hi = max(hi, vaddr + vmsize)
            off += cmdsize
        return (hi - lo) if lo is not None else 0
    except Exception:
        return 0


def add_pseudo_segments(segs, log, tests, overlay):
    """dyld and (in logs without a `Main executable mapped` line) the main
    executable are mapped by mldr BEFORE dyld's segment logging starts, so
    they never appear in the segment list. mldr's DEBUG lines print their base
    addresses — turn those into pseudo-segments, appended so real mappings keep
    priority.

    A pseudo-segment's length comes from the image's own segment table and from
    nothing else. It used to be a flat 0x1000000 for the main executable,
    which made every address within 16 MB above the probe's load address look
    like it belonged to the probe — the source of a confident, wrong
    attribution in the log this tool is meant to read. If the span cannot be
    derived, the pseudo-segment is dropped and those addresses stay unmapped,
    which is the honest answer."""
    text = open(log, errors="replace").read()
    m = re.search(r"DEBUG dyld mh=0x([0-9a-fA-F]+)", text)
    if m:
        lo = int(m.group(1), 16)
        # mldr logs the dyld_all_image_infos size, not the image size —
        # derive the real image size from the dyld file's segment vmsizes
        dyld_size = mach_o_span(os.path.join(overlay, "usr/lib/dyld"))
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
        if main and not any(host_path(p, overlay, tests) == main
                            for _, _, p in segs):
            # Compare by HOST path: the log names the guest path
            # ("/wayland-window-create-macho") while `main` is the file this
            # tool reads, so a literal string comparison never matches and a
            # duplicate of a range the log already gave us gets appended —
            # which is how a second, longer, made-up range for the same image
            # ended up overriding the real one.
            span = mach_o_span(main)
            if span:
                segs.append((lo, lo + span - 1, main))
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
    segs, n_dylib = load_segments(log)
    n_real = len(segs)
    segs = add_pseudo_segments(segs, log, tests, overlay)
    n_pseudo = len(segs) - n_real

    fatal = re.search(r"FATAL signal (\d+).*?addr=(0x[0-9a-f]+)", text)
    rip = re.search(r"rip=(0x[0-9a-fA-F]+)", text)
    if not fatal or not rip:
        print("no crash found in log")
        return
    print(f"crash: signal {fatal.group(1)} at {fatal.group(2)}")
    if n_real:
        print(f"images mapped: {n_real} address range(s) named by the log "
              f"({n_dylib} dylib mapping(s) + the main executable)"
              + (f", +{n_pseudo} derived from mldr DEBUG lines" if n_pseudo else ""))
    else:
        print("images mapped: none named by the log — it carries no "
              "DYLD_PRINT_SEGMENTS lines, so only what mldr's own DEBUG lines "
              "give can be attributed")
        if n_pseudo:
            print(f"           {n_pseudo} range(s) derived from mldr DEBUG lines")

    r = resolve(int(rip.group(1), 16), segs, overlay, tests)
    if r:
        print(f"  rip  {rip.group(1)}  {r}")
    else:
        # Say so, and say what it means. This tool only knows the images the
        # loader announced; an address no range covers is not a failure to
        # resolve, it is the finding — the faulting code is not in any image
        # the run announced, which is exactly what the run in
        # build-freebsd/SIGSEGV-SECOND-DLOPEN.md needed said out loud.
        print(f"  rip  {rip.group(1)}  unmapped (no image covers this address)")
        print(f"       not attributed: none of the {len(segs)} known range(s) "
              f"contains it, so it is host")
        print(f"       code or an image the loader never announced — this tool "
              f"will not guess.")

    unresolved = 0
    for m in re.finditer(r"\[rsp\s*(\d+)\] (0x[0-9a-fA-F]+)", text):
        off, val = m.group(1), int(m.group(2), 16)
        r = resolve(val, segs, overlay, tests)
        if r:
            print(f"  rsp+{off:<3} {m.group(2)}  {r}")
        else:
            unresolved += 1

    # mldr's deep stack dump ("stack dump at rsp=..." or "guest stack dump
    # at rsp=..." followed by raw hex lines) — scan every hex word as a
    # candidate return address. The guarded dump in src/startup/mldr/
    # crash_dump.c marks a slot it could not read as "(unreadable)", which
    # carries no hex word and so is naturally skipped here; the count of
    # slots that resolved to nothing is reported rather than hidden.
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
            else:
                unresolved += 1

    if unresolved:
        print(f"\n{unresolved} of the stack words named no known image "
              f"(not listed above)")

    if not segs:
        print("\n(no DYLD_PRINT_SEGMENTS mappings in log; rerun with "
              "DYLD_DEBUG='DYLD_PRINT_SEGMENTS=1' for symbolized traces)")


if __name__ == "__main__":
    main()
