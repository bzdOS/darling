#!/usr/bin/env python3
"""attribution-from-log.py -- say, from a run log alone, which image's trie was
being walked when dyld2's trieWalk faulted, and at which source line.

    python3 build-freebsd/attribution-from-log.py <log> [--dyld <dyld>]
    python3 build-freebsd/attribution-from-log.py --self-test
    python3 build-freebsd/attribution-from-log.py --recompute [--dyld <dyld>]

Reads only. The log is the input; nothing here writes to it or launches
anything.

WHAT IT EXTRACTS, AND WHY EACH PIECE CARRIES WEIGHT

(a) Whether the loader printed its own bounds complaint,
    "trieWalk() malformed trie node, terminalSize=... extends past end of trie".
    That line is NOT commented out in dyld2's source (unlike the dyld::log
    calls around it), so it would appear if the fault were at the guard on
    ImageLoader.cpp:1848. Its ABSENCE is therefore evidence, not absence of
    evidence: the fault was at one of the three reads dyld2 does not bounds
    check -- 1837 terminalSize = *p++, 1862 the edge scan c = *p, or 1876 the
    child uleb128 skip. When it IS present, the fault is at or after 1848 and
    the number in the message is the bogus terminalSize.

(b) The fault address, from the mldr handler's output. Any of the shapes it
    uses are accepted; if the address lands inside trieWalk, the offset from the
    function's entry gives the line.

(c) The last "dyld: loaded:" line before the fault, which names the image whose
    load was in flight. It is a proxy for the image whose exports were being
    searched, not a proof: a lookup can start in one image while another
    finishes loading. It is reported as the best available signal, and as a
    proxy, not as a conclusion.

THE ADDRESS MAP IS RECOMPUTED, NOT ASSUMED

The table below is baked, and every entry says where it came from. But it is
falsifiable: --recompute re-derives the table from the dyld that is actually on
this machine -- FAT header, x86_64 slice, the trieWalk symbol, and a
disassembly of it -- and diffs it against the baked one, exiting non-zero on
any disagreement. The derivation is the slice-1 method: the source lines have
distinct instruction shapes, and the anchors are found by those shapes rather
than by their offsets, so a different build shifting the addresses is detected
instead of silently mis-mapped. Without --recompute the baked table is used and
the tool says plainly that it was not re-verified on this binary.
"""

import hashlib
import os
import re
import struct
import subprocess
import sys

# Baked map: (offset from the entry of ImageLoader::trieWalk, source line, what).
#
# Provenance: derived in slice 1 from the x86_64 slice of the June-backup dyld,
# entry 0x376a0, by disassembling it and identifying each line by its
# instruction shape. The shapes are recorded here so the derivation can be
# re-checked, and --recompute does exactly that.
TRIE_WALK_ENTRY = 0x376A0
ANCHORS = [
    # (offset, line, instruction shape that identifies it, meaning)
    (0x45, 1837, "movzbl (%rax),%eax  after p is bumped",
     "terminalSize = *p++  -- NO bounds check on p"),
    (0x6B, 1841, "callq read_uleb128",
     "terminalSize = read_uleb128(p,end) -- taken when the first byte is >127"),
    (0xA4, 1847, "movq %rax,-0x38(%rbp)  then cmpq -0x18(%rbp),%rax",
     "children = p + terminalSize  -- the instruction at trieWalk+0xa4; a store, cannot fault"),
    (0xAC, 1848, "cmpq -0x18(%rbp),%rax / jbe",
     "if (children > end) -- the guard; its failure logs the terminalSize message"),
    (0xBA, 1849, "leaq of the complaint string (found by locating the string, not by offset)",
     "dyld::log(\"trieWalk() malformed trie node, terminalSize=...\") -- the log call's "
     "argument setup starts at +0xb6, this is the leaq that references the string"),
    (0xE4, 1853, "movb (%rax),%al  after children is bumped",
     "childrenRemaining = *children++ -- first real dereference of children"),
]

UNCHECKED_READS = {
    0x45: "1837 terminalSize = *p++",
    0xE4: "1853 *children++ (first dereference of a children pointer built from terminalSize)",
}
EDGE_SCAN = (0xC3, 0xE4)      # the edge-scan loop and the uleb skip live between these


def die(msg, code=2):
    print("attribution-from-log: %s" % msg, file=sys.stderr)
    sys.exit(code)


def find_dyld(explicit=None):
    """The dyld to map addresses in, and how we found it."""
    cands = []
    if explicit:
        cands.append(explicit)
    od = os.environ.get("DARLING_OVERLAY")
    if od:
        cands.append(os.path.join(od, "usr/lib/dyld"))
        cands.append(os.path.join(od, "usr/lib/dyld.June-backup"))
    cands += ["/usr/lib/dyld.June-backup", "/usr/lib/dyld"]
    for c in cands:
        if c and os.path.exists(c):
            return c
    return None


def x86_64_slice(path):
    """(bytes, provenance) of the x86_64 slice of a possibly-fat Mach-O."""
    d = open(path, "rb").read()
    magic = struct.unpack_from(">I", d, 0)[0]
    if magic == 0xCAFEBABE:
        n = struct.unpack_from(">I", d, 4)[0]
        for i in range(n):
            ct, _cs, off, size, _al = struct.unpack_from(">IIIII", d, 8 + i * 20)
            if ct == 0x01000007:                    # CPU_TYPE_X86_64
                return d[off:off + size], "fat slice at offset 0x%x size %d" % (off, size)
        die("%s is universal but has no x86_64 slice" % path)
    magic = struct.unpack_from("<I", d, 0)[0]
    if magic != 0xFEEDFACF:
        die("%s is not a 64-bit Mach-O (magic %08x)" % (path, magic))
    return d, "thin 64-bit Mach-O, no fat header"


def trie_walk_entry(slice_path):
    """The address of ImageLoader::trieWalk, from the symbol table."""
    out = subprocess.run(["llvm-nm", "-n", slice_path], capture_output=True, text=True).stdout
    for line in out.splitlines():
        m = re.match(r"^([0-9a-f]+)\s+\S+\s+(__ZN11ImageLoader8trieWalk\S*)$", line.strip())
        if m:
            return int(m.group(1), 16)
    return None


def vm_of_fileoff(slice_path, foff):
    """The vmaddr of a file offset, via the section headers."""
    d = open(slice_path, "rb").read()
    _m, _t, _s, _f, ncmds, _, _, _ = struct.unpack_from("<IiiIIIII", d, 0)
    off = 32
    for _ in range(ncmds):
        cmd, cs = struct.unpack_from("<II", d, off)
        if cmd == 0x19:                                  # LC_SEGMENT_64
            nsects = struct.unpack_from("<I", d, off + 64)[0]
            so = off + 72
            for _i in range(nsects):
                _n, _s2 = d[so:so + 16], d[so + 16:so + 32]
                addr, size, foff2 = struct.unpack_from("<QQI", d, so + 32)
                if foff2 <= foff < foff2 + size:
                    return addr + (foff - foff2)
                so += 80
        off += cs
    return None


def log_string_vmaddr(slice_path):
    """vmaddr of dyld2's terminalSize complaint string, in this slice.

    Found from the text itself rather than hardcoded, so the 1849 anchor can be
    looked up by what it references instead of by where it happens to sit.
    """
    d = open(slice_path, "rb").read()
    i = d.find(b"trieWalk() malformed trie node, terminalSize=")
    if i < 0:
        return None
    return vm_of_fileoff(slice_path, i)


def recompute(dyld_path):
    """Re-derive the anchor offsets from the binary, by instruction shape."""
    blob, prov = x86_64_slice(dyld_path)
    tmp = "/tmp/attribution-slice-%d.dylib" % os.getpid()
    with open(tmp, "wb") as f:
        f.write(blob)
    try:
        entry = trie_walk_entry(tmp)
        if entry is None:
            die("ImageLoader::trieWalk not found in %s" % dyld_path)
        complaint = log_string_vmaddr(tmp)
        dis = subprocess.run(
            ["llvm-objdump", "-d", "--no-show-raw-insn",
             "--start-address=0x%x" % entry, "--stop-address=0x%x" % (entry + 0x200), tmp],
            capture_output=True, text=True).stdout
        found = {}
        for line in dis.splitlines():
            m = re.match(r"^\s*([0-9a-f]+):\s+(.*)$", line)
            if not m:
                continue
            addr, raw = int(m.group(1), 16), m.group(2)
            off = addr - entry
            target = None
            tm = re.search(r"##\s*(0x[0-9a-fA-F]+)", raw)
            if tm:
                target = int(tm.group(1), 16)
            t = re.sub(r"\s+", " ", re.sub(r"##.*$", "", raw)).strip()
            # 1837: movzbl (%rax), %eax
            if re.match(r"^movzbl \(%rax\), %eax$", t):
                found.setdefault(0x45, off)
            # 1841: a call to read_uleb128
            if t.startswith("callq") and "read_uleb128" in t:
                found.setdefault(0x6B, off)
            # 1847: the store of children, movq %rax, -0x38(%rbp)
            if re.match(r"^movq %rax, -0x38\(%rbp\)$", t):
                found.setdefault(0xA4, off)
            # 1848: the bounds check itself, at its OWN offset
            if re.match(r"^cmpq -0x18\(%rbp\), %rax$", t):
                found.setdefault(0xAC, off)
            # 1849: the leaq of the complaint string
            if complaint is not None and target == complaint:
                found.setdefault(0xBA, off)
            # 1853: movb (%rax), %al
            if re.match(r"^movb \(%rax\), %al$", t):
                found.setdefault(0xE4, off)
        return entry, found, prov, hashlib.sha256(blob).hexdigest()
    finally:
        if os.path.exists(tmp):
            os.unlink(tmp)


def line_for_offset(off):
    """Nearest anchor at or below `off`, and the distance from it."""
    best = None
    for aoff, line, _shape, _what in ANCHORS:
        if aoff <= off and (best is None or aoff > best[0]):
            best = (aoff, line, _what)
    return best


# ---------------------------------------------------------------- the log ---

COMPLAINT = "trieWalk() malformed trie node"
LOADED = re.compile(r"dyld:\s+loaded:\s+.*?((?:/[^\s:]+\.(?:dylib|so)|[^\s:]+\.framework)[^\s:]*)")
# A backtrace from a real run is symbolised, and "trieWalk+0x..." is the most
# trustworthy thing in the log: it is already an offset from the entry, so it
# needs no slide and no assumption.
LABELLED = re.compile(r"trieWalk\s*\+\s*0x([0-9a-fA-F]+)")
RIP = re.compile(r"\brip\b[= ]\s*0x([0-9a-fA-F]+)")
FATAL = re.compile(r"(?:FATAL signal|signal \d+|fatal|abort|SIGSEGV)[^\n]*?0x([0-9a-fA-F]+)")
ADDR = re.compile(r"\b(?:at|addr=)\s*0x([0-9a-fA-F]+)")
# If the log carries dyld's load address, a runtime address can be turned back
# into a static one; without it, a runtime address alone is ambiguous.
DYLD_BASE = re.compile(r"dyld[^\n]*?\b(?:base|loaded at|at)\s*=?\s*0x([0-9a-fA-F]+)", re.I)


def analyse(text, entry=TRIE_WALK_ENTRY, span=0x400):
    """Everything the log has to say, and one-line verdict."""
    lines = text.splitlines()
    complaint_at = [i for i, l in enumerate(lines) if COMPLAINT in l]
    size_in_complaint = None
    if complaint_at:
        m = re.search(r"terminalSize=0x([0-9a-fA-F]+)", lines[complaint_at[0]])
        if m:
            size_in_complaint = int(m.group(1), 16)

    # Fault offset, in order of trust:
    #   1. a symbolised "trieWalk+0xNN" -- already relative to the entry
    #   2. a runtime address, if the log also gives dyld's load address
    #   3. a runtime address that is already inside trieWalk's static range
    offset = None
    how = None
    runtime = None
    for rx, tag in ((RIP, "rip"), (FATAL, "fatal"), (ADDR, "at")):
        m = rx.search(text)
        if m:
            runtime = int(m.group(1), 16)
            how = tag
            break

    # The handler prints two different numbers and they are not
    # interchangeable. `rip=` is where the CPU was executing; `addr=` is the
    # address the faulting access named, and for a data fault that is the one
    # that locates the bug. Taking rip first and then reporting "fault address
    # not found" on a log that printed addr= in plain sight was this tool
    # misreading its own input, so both are read and both are reported.
    si_addr = None
    m = re.search(r"\bat addr=(0x[0-9a-fA-F]+)", text)
    if m:
        si_addr = int(m.group(1), 16)
    rip_value = None
    m = RIP.search(text)
    if m:
        rip_value = int(m.group(1), 16)

    m = LABELLED.search(text)
    if m:
        offset, how = int(m.group(1), 16), "symbolised label"
    elif runtime is not None:
        base = None
        mb = DYLD_BASE.search(text)
        if mb:
            base = int(mb.group(1), 16)
        if base is not None:
            # runtime - image base is a STATIC ADDRESS inside dyld; the offset
            # into trieWalk is that minus the function's entry. Conflating the
            # two is easy and silently produces an offset that is out by 0x376A0.
            static = runtime - base
            off = static - entry
            if 0 <= off < span:
                offset, how = off, ("static address 0x%x (runtime 0x%x - logged dyld "
                                    "base 0x%x) minus trieWalk entry 0x%x"
                                    % (static, runtime, base, entry))
            else:
                how = ("runtime 0x%x - logged base 0x%x = static 0x%x, which is %+d "
                       "from trieWalk's entry 0x%x and so not inside the function "
                       "(0x0-0x%x) -- the logged base is not trieWalk's image base"
                       % (runtime, base, static, off, entry, span))
        elif entry <= runtime < entry + span:
            offset, how = runtime - entry, "static address in range"
    fault = (offset + entry) if offset is not None else None

    # si_addr is the better candidate when rip could not be placed: for a data
    # fault it is the address that actually faulted. Tried second so a log that
    # DOES place its rip keeps the answer it had.
    if offset is None and si_addr is not None:
        if entry <= si_addr < entry + span:
            offset, how, runtime = si_addr - entry, "faulting address (addr=) in range", si_addr
        elif DYLD_BASE.search(text):
            base = int(DYLD_BASE.search(text).group(1), 16)
            static = si_addr - base
            if 0 <= static < span:
                offset, how = static, ("faulting address 0x%x - dyld base 0x%x"
                                       % (si_addr, base))
    if offset is None and si_addr is not None:
        how = (how or "") + "; addr=0x%x does not fall in trieWalk either" % si_addr

    # The last image loaded before the complaint (or the end of the log), which
    # is the best available signal for whose exports were being searched. It is
    # a proxy, not a proof, and is reported as such.
    stop = complaint_at[0] if complaint_at else len(lines)
    image = None
    for l in lines[:stop]:
        m = LOADED.search(l)
        if m:
            image = m.group(1)

    anchor = line_for_offset(offset) if offset is not None else None
    return {
        "complaint": bool(complaint_at),
        "terminal_size": size_in_complaint,
        "fault": fault,
        "fault_from": how,
        "offset": offset,
        "image": image,
        "si_addr": si_addr,
        "rip": rip_value,
        "entry": entry,
        "span": span,
        "line": anchor[1] if anchor else None,
        "meaning": anchor[2] if anchor else None,
    }


def verdict(r):
    img = r["image"] or "<image not determinable from this log>"
    if r["complaint"]:
        where = "ImageLoader.cpp:%s" % r["line"] if r["line"] else "ImageLoader.cpp:1848/1849"
        return ("%s | %s | the loader's own guard fired, terminalSize=0x%x is bogus; "
                "dyld2 logged and returned NULL rather than faulting" %
                (img, where, r["terminal_size"] or 0))
    if r["fault"] is None:
        # Say what the log does NOT support. The image below is only the last
        # thing dyld announced, and leading with it reads as an accusation --
        # which is how a crash with nothing to do with dyld2's trie walk ended
        # up attributed to whichever dylib happened to load last.
        return ("not a trieWalk fault: the loader printed no bounds complaint, "
                "and neither rip (%s) nor the faulting address (%s) falls in "
                "trieWalk's range 0x%x-0x%x. The last image the loader "
                "announced is %s, which is NOT evidence that it owns the fault."
                % ("0x%x" % r["rip"] if r["rip"] is not None else "absent",
                   "0x%x" % r["si_addr"] if r["si_addr"] is not None else "absent",
                   r["entry"], r["entry"] + r["span"], img))
    if r["line"]:
        return ("%s | ImageLoader.cpp:%s (+0x%x, %s) | no bounds complaint was printed, "
                "so this is one of the reads dyld2 does not check: 1837, 1862 or 1876"
                % (img, r["line"], r["offset"], r["meaning"]))
    return ("%s | trieWalk+0x%x | outside the mapped anchors; no complaint printed"
            % (img, r["offset"]))


# ------------------------------------------------------------- self-test ---

FIXTURE_COMPLAINT = """\
dyld: loaded: /System/Library/Frameworks/Foundation.framework/Versions/C/Foundation
dyld: loaded: /System/Library/Frameworks/AppKit.framework/Versions/C/AppKit
trieWalk() malformed trie node, terminalSize=0x2840 extends past end of trie
FATAL signal 11 at addr=0x0
"""

# A real backtrace is symbolised and the address is a RUNTIME one, so the tool
# has to get the offset from the label rather than by assuming a static address.
FIXTURE_NO_COMPLAINT = """\
dyld: loaded: /System/Library/Frameworks/CoreGraphics.framework/Versions/A/CoreGraphics
dyld: loaded: /usr/lib/libSystem.B.dylib
FATAL signal 11 at 0x100037744
  thread 0 rip 0x100037744 ImageLoader::trieWalk+0xa4
stack dump:
  frame 0: 0x100037744 ImageLoader::trieWalk+0xa4
  frame 1: 0x1003775a1 ImageLoaderMachOCompressed::findShallowExportedSymbol
"""

# No symbolised label, only a runtime address plus dyld's load address: the
# slide has to come from the log or the offset is ambiguous.
FIXTURE_NO_COMPLAINT_BASE = """\
dyld: loaded: /System/Library/Frameworks/AppKit.framework/Versions/C/AppKit
dyld image loaded at 0x100000000
FATAL signal 11 at 0x100037744
  thread 0 rip 0x100037744
"""

SELF_TEST_CASES = [
    # name, fixture, expect_complaint, expect_offset, expect_line, expect_image_tail
    ("complaint present -> guard, not a fault",
     FIXTURE_COMPLAINT, True, None, None, "AppKit.framework/Versions/C/AppKit"),
    ("no complaint, symbolised trieWalk+0xa4",
     FIXTURE_NO_COMPLAINT, False, 0xA4, 1847, "libSystem.B.dylib"),
    ("no complaint, runtime address + dyld base",
     FIXTURE_NO_COMPLAINT_BASE, False, 0xA4, 1847, "AppKit.framework/Versions/C/AppKit"),
]


def self_test():
    ok = True
    for name, text, want_complaint, want_off, want_line, want_img in SELF_TEST_CASES:
        r = analyse(text)
        print("case: %s" % name)
        problems = []
        if r["complaint"] != want_complaint:
            problems.append("complaint %s, expected %s" % (r["complaint"], want_complaint))
        if want_off is not None and r["offset"] != want_off:
            problems.append("offset %s, expected 0x%x" % (
                "0x%x" % r["offset"] if r["offset"] is not None else "None", want_off))
        if want_line is not None and r["line"] != want_line:
            problems.append("line %s, expected %s" % (r["line"], want_line))
        if not (r["image"] or "").endswith(want_img):
            problems.append("image %r, expected it to end with %r" % (r["image"], want_img))
        print("  complaint=%s offset=%s line=%s via=%s" % (
            r["complaint"],
            "0x%x" % r["offset"] if r["offset"] is not None else "none",
            r["line"], r["fault_from"]))
        print("  image   : %s" % r["image"])
        print("  verdict : %s" % verdict(r))
        if problems:
            print("  SELF-TEST FAIL: %s" % "; ".join(problems))
            ok = False
        print()
    print("self-test: %s (%d case(s))" % ("PASS" if ok else "FAIL", len(SELF_TEST_CASES)))
    return 0 if ok else 1


def main(argv):
    if "--self-test" in argv:
        return self_test()
    dyld_explicit = None
    if "--dyld" in argv:
        dyld_explicit = argv[argv.index("--dyld") + 1]
    if "--recompute" in argv:
        dyld = find_dyld(dyld_explicit)
        if not dyld:
            die("no dyld found; pass --dyld <path>")
        entry, found, prov, digest = recompute(dyld)
        print("dyld            : %s" % dyld)
        print("slice           : %s" % prov)
        print("slice sha256    : %s" % digest)
        print("trieWalk entry  : 0x%x (baked table says 0x%x)" % (entry, TRIE_WALK_ENTRY))
        bad = 0
        if entry != TRIE_WALK_ENTRY:
            print("MISMATCH entry  : recomputed 0x%x != baked 0x%x" % (entry, TRIE_WALK_ENTRY))
            bad += 1
        for aoff, line, shape, _what in ANCHORS:
            got = found.get(aoff)
            if got is None:
                print("MISSING         : line %d (+0x%x) not found by shape %r" % (line, aoff, shape))
                bad += 1
            elif got != aoff:
                print("MISMATCH        : line %d recomputed at +0x%x, baked says +0x%x"
                      % (line, got, aoff))
                bad += 1
            else:
                print("ok              : line %-4d +0x%-5x %s" % (line, aoff, shape))
        print("\n%s: %d disagreement(s) between the baked table and this binary"
              % ("FAIL" if bad else "PASS", bad))
        return 1 if bad else 0
    logs = [a for a in argv if not a.startswith("--") and a != dyld_explicit]
    if not logs:
        die("usage: attribution-from-log.py <log> [--dyld <dyld>] | --self-test | --recompute")
    text = open(logs[0], errors="replace").read()
    r = analyse(text)
    print("log: %s" % logs[0])
    print("address map: baked table, NOT re-verified on this binary "
          "(run --recompute to check it against the dyld here)")
    print("complaint printed : %s" % r["complaint"])
    if r["terminal_size"] is not None:
        print("  terminalSize    : 0x%x" % r["terminal_size"])
    print("fault address     : %s" % ("0x%x (%s)" % (r["fault"], r["fault_from"])
                                       if r["fault"] else "not inside trieWalk"))
    print("  handler si_addr  : %s" % ("0x%x" % r["si_addr"] if r["si_addr"] is not None
                                       else "not printed"))
    print("  handler rip      : %s" % ("0x%x" % r["rip"] if r["rip"] is not None
                                       else "not printed"))
    if r["offset"] is not None:
        print("  offset in trieWalk: +0x%x" % r["offset"])
        print("  mapped           : ImageLoader.cpp:%s -- %s" % (r["line"], r["meaning"]))
    print("last image loaded : %s" % r["image"])
    print("\nVERDICT: %s" % verdict(r))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
