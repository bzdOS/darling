#!/bin/sh
# fixup-dylinker.sh — turn a freshly linked system_loader into something mldr
# will accept as a dynamic linker.
#
# WHY THIS EXISTS
#
#   ld64.lld does not implement -dylinker:
#
#       ld64.lld: warning: Option `-dylinker' is not yet implemented. Stay tuned...
#
#   It accepts the flag, links the image, and then produces the two things an
#   EXECUTABLE gets instead of the two things a DYNAMIC LINKER gets:
#
#       filetype   MH_EXECUTE (2)      instead of MH_DYLINKER (7)
#       entry      LC_MAIN             instead of LC_UNIXTHREAD
#
#   It also always emits LC_LOAD_DYLINKER, and mldr refuses exactly that:
#
#       src/startup/mldr/loader.c:346:
#           "Dynamic linker can't reference another dynamic linker"
#
#   None of the three linkers installed here implements the option (19.1.7,
#   20.1.8, 21.1.8 all warn), so this post-link pass is the way to get a
#   loadable image out of them, not a stopgap for an old toolchain.
#
# WHAT IT CHANGES, in this order
#
#   1. filetype 2 -> 7. The kernel switches on it: MH_EXECUTE is only accepted
#      at load depth 1 or 3, MH_DYLINKER only at depth 2, and a dylinker is
#      loaded at depth 2 (bsd/kern/mach_loader.c: the filetype switch, and the
#      load_dylinker() call site). Wrong filetype fails header validation before
#      any of the image's own code runs.
#
#   2. LC_LOAD_DYLINKER removed, load commands compacted, ncmds and sizeofcmds
#      fixed. Segment file offsets do not move.
#
#   3. LC_MAIN replaced by LC_UNIXTHREAD carrying the same entry address, as an
#      x86_64 thread state: flavour 4 (x86_THREAD_STATE64), count 42, 21
#      64-bit slots, rip in slot 16. LC_MAIN is ignored by the kernel unless the
#      load depth is 1, so without this the image is mapped and then started at
#      nothing. Apple's own dyld, and the one in the overlay, carry
#      LC_UNIXTHREAD.
#
#   LC_UNIXTHREAD is 184 bytes where LC_MAIN is 24, so step 3 also drops
#   LC_UUID, LC_VERSION_MIN_MACOSX, LC_SYMTAB, LC_DYSYMTAB and
#   LC_FUNCTION_STARTS to pay for the larger thread command, and the space those
#   commands gave back becomes padding at the end of the header area. The file
#   size does not change at all, so no segment or section offset moves.
#   Losing the symbol table costs symbolised crash addresses, and costs nothing
#   else here.
#
# USAGE
#
#   sh build-freebsd/fixup-dylinker.sh <system_loader> <out>
#
# The entry address is read out of LC_MAIN (entryoff + the __TEXT vmaddr). If
# the image has no LC_MAIN the script stops rather than guessing.
#
# NOT VERIFIED BY RUNNING. The transformation follows the kernel's own rules and
# the reference dyld in the overlay, and an earlier hand-run of the same three
# steps produced an image the kernel accepted, but nobody has loaded the output
# of this script. The first run is the check.
set -eu

in=${1:?usage: fixup-dylinker.sh <in> <out>}
out=${2:?usage: fixup-dylinker.sh <in> <out>}

[ -f "$in" ] || { echo "FATAL: $in missing" >&2; exit 1; }

echo "=== fixup-dylinker: $in -> $out"
python3 - "$in" "$out" <<'PYEOF'
import struct
import sys

MH_EXECUTE, MH_DYLINKER = 2, 7
LC_REQ_DYLD = 0x80000000
LC_SYMTAB, LC_DYSYMTAB, LC_UUID = 0x2, 0xB, 0x1B
LC_UNIXTHREAD, LC_LOAD_DYLINKER = 0x5, 0xE
LC_SEGMENT_64, LC_VERSION_MIN_MACOSX = 0x19, 0x24
LC_FUNCTION_STARTS = 0x26
LC_MAIN = 0x28 | LC_REQ_DYLD
DROP = {LC_UUID, LC_VERSION_MIN_MACOSX, LC_SYMTAB, LC_DYSYMTAB,
        LC_FUNCTION_STARTS}

src, dst = sys.argv[1], sys.argv[2]
data = open(src, "rb").read()
size_in = len(data)

magic, cputype, cpusubtype, ftype, ncmds, sizeofcmds, flags, _ = \
    struct.unpack_from("<8I", data, 0)
assert magic == 0xFEEDFACF, f"not a 64-bit Mach-O: {magic:#x}"
assert cputype == 0x01000007, f"not x86_64 (CPU_TYPE_X86_64): {cputype:#x}"
print(f"  in: filetype={ftype} ncmds={ncmds} sizeofcmds={sizeofcmds} "
      f"size={size_in}")

# --- where does the image start, and where does it want to begin executing ---
text_vmaddr = None
main_entryoff = None
off = 32
for _ in range(ncmds):
    cmd, cmdsize = struct.unpack_from("<2I", data, off)
    if cmd == LC_SEGMENT_64:
        segname = data[off + 8:off + 24].rstrip(b"\0").decode("ascii", "replace")
        if segname == "__TEXT":
            text_vmaddr = struct.unpack_from("<Q", data, off + 24)[0]
    elif cmd == LC_MAIN:
        main_entryoff = struct.unpack_from("<Q", data, off + 8)[0]
    off += cmdsize
assert text_vmaddr is not None, "no __TEXT segment"
assert main_entryoff is not None, "no LC_MAIN: nothing to convert"
rip = text_vmaddr + main_entryoff
print(f"  __TEXT vmaddr={text_vmaddr:#x} LC_MAIN entryoff={main_entryoff:#x} "
      f"-> entry {rip:#x}")

out = bytearray(data)

# --- 1. filetype ---
if ftype == MH_EXECUTE:
    struct.pack_into("<I", out, 12, MH_DYLINKER)
    print("  filetype 2 -> 7 (MH_DYLINKER)")
elif ftype == MH_DYLINKER:
    print("  filetype already 7, left alone")
else:
    raise SystemExit(f"unexpected filetype {ftype}, refusing to touch it")

# --- 2 and 3, one pass over the load commands ---
kept, dropped_bytes, main_seen, dylinker_seen = [], 0, False, False
off = 32
for _ in range(ncmds):
    cmd, cmdsize = struct.unpack_from("<2I", data, off)
    blob = bytes(data[off:off + cmdsize])
    if cmd == LC_MAIN:
        # thread_command: cmd, cmdsize, flavor=x86_THREAD_STATE64, count=42,
        # then 21 64-bit slots; rip is slot 16.
        regs = [0] * 21
        regs[16] = rip
        blob = (struct.pack("<4I", LC_UNIXTHREAD, 184, 4, 42)
                + struct.pack("<21Q", *regs))
        main_seen = True
    elif cmd == LC_LOAD_DYLINKER:
        dropped_bytes += cmdsize
        dylinker_seen = True
        off += cmdsize
        continue
    elif cmd in DROP:
        dropped_bytes += cmdsize
        off += cmdsize
        continue
    kept.append(blob)
    off += cmdsize

assert main_seen, "no LC_MAIN found"
print(f"  LC_MAIN -> LC_UNIXTHREAD (x86_THREAD_STATE64, 42, rip={rip:#x})")
print(f"  LC_LOAD_DYLINKER: "
      f"{'removed' if dylinker_seen else 'not present'}")
print(f"  dropped {dropped_bytes} bytes of "
      f"LC_UUID/LC_VERSION_MIN_MACOSX/LC_SYMTAB/LC_DYSYMTAB/LC_FUNCTION_STARTS"
      f" to pay for the larger thread command")

newcmds = b"".join(kept)
old_area = 32 + sizeofcmds
new_area = 32 + len(newcmds)
print(f"  load commands: {ncmds} -> {len(kept)}, sizeofcmds {sizeofcmds} -> "
      f"{len(newcmds)} ({len(newcmds) - sizeofcmds:+d})")
assert new_area <= old_area, (
    f"the new load commands need {new_area} bytes but the header area is "
    f"{old_area}; the file would have to grow and every segment file offset "
    f"with it -- drop the symbol table instead of using --keep-symbols")

# The freed space becomes padding at the end of the header area, so the file
# keeps its length and no segment offset moves.
body = bytes(out[:32]) + newcmds
body += b"\0" * (old_area - len(body))
body += bytes(out[old_area:])
hdr = bytearray(body[:32])
struct.pack_into("<I", hdr, 16, len(kept))        # ncmds
struct.pack_into("<I", hdr, 20, len(newcmds))     # sizeofcmds
final = bytes(hdr) + body[32:]

# --- the invariants that matter ---
assert len(final) == size_in, f"file size changed: {size_in} -> {len(final)}"
m2, _, _, ft2, nc2, sc2, _, _ = struct.unpack_from("<8I", final, 0)
assert ft2 == MH_DYLINKER, ft2
assert 32 + sc2 <= len(final), "sizeofcmds runs past the end of the file"
assert m2 == 0xFEEDFACF
codes, o = [], 32
for _ in range(nc2):
    c, cs = struct.unpack_from("<2I", final, o)
    assert cs >= 8 and o + cs <= 32 + sc2, f"bad cmdsize {cs} at {o}"
    codes.append(c)
    o += cs
assert o == 32 + sc2, f"load commands end at {o}, sizeofcmds says {32 + sc2}"
assert LC_UNIXTHREAD in codes, "no LC_UNIXTHREAD in the output"
assert LC_MAIN not in codes, "LC_MAIN survived"
assert LC_LOAD_DYLINKER not in codes, "LC_LOAD_DYLINKER survived"

open(dst, "wb").write(final)
print(f"  wrote {dst} ({len(final)} bytes, size unchanged)")
print(f"  entry point for the record: {rip:#x}")
PYEOF

echo "=== what the result looks like"
file "$out"
echo "--- load commands:"
if command -v llvm-otool >/dev/null 2>&1; then
	llvm-otool -l "$out" | awk '/^ *cmd /{print "   " $2}'
elif command -v otool >/dev/null 2>&1; then
	otool -l "$out" | awk '/^ *cmd /{print "   " $2}'
else
	python3 - "$out" <<'PYEOF'
import struct, sys
d = open(sys.argv[1], "rb").read()
N = {0x1:"LC_SEGMENT",0x2:"LC_SYMTAB",0x5:"LC_UNIXTHREAD",0xb:"LC_DYSYMTAB",
     0xc:"LC_LOAD_DYLIB",0xe:"LC_LOAD_DYLINKER",0xf:"LC_ID_DYLINKER",
     0x19:"LC_SEGMENT_64",0x1b:"LC_UUID",0x26:"LC_FUNCTION_STARTS",
     0x32:"LC_BUILD_VERSION",0x80000022:"LC_DYLD_INFO_ONLY",
     0x80000028:"LC_MAIN",0x80000033:"LC_DYLD_CHAINED_FIXUPS",
     0x80000034:"LC_DYLD_EXPORTS_TRIE"}
_,_,_,_,nc,_,_,_ = struct.unpack_from("<8I", d, 0)
o = 32
for _ in range(nc):
    c, cs = struct.unpack_from("<2I", d, o)
    print("   " + N.get(c, hex(c)))
    o += cs
PYEOF
fi
echo "=== done"
