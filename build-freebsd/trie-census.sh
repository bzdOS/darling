#!/bin/sh
# trie-census.sh: ULEB census of LC_DYLD_EXPORTS_TRIE over every Mach-O
# image in the staged overlay (Frameworks/** and usr/lib/**).
#
# For each image: name, filetype, dataoff/datasize, node count, export
# count, special flags (ABSOLUTE kind=2 / WEAK 0x04 / REEXPORT 0x08) and
# out-of-bounds child offsets. Method per build-freebsd/DYLD-REBUILD.md.
#
# Read-only: the live overlay is never written.
# Reproduce: DARLING_OVERLAY=... DARLING_BUILD_DIR=... sh build-freebsd/trie-census.sh
SRC=$(cd "$(dirname "$0")/.." && pwd)
OVL=${DARLING_OVERLAY:-${SRC}/../overlay}
OUT="${DARLING_BUILD_DIR:-/tmp/darling-build}/wl-body-trie-census.txt"
export OVL OUT
python3 - <<'PY'
import os, struct

OVL = os.environ["OVL"]
OUT = os.environ["OUT"]
ROOTS = [OVL]  # every Mach-O under the staged overlay

MH_MAGIC_64 = 0xfeedfacf
LC_DYLD_EXPORTS_TRIE = 0x80000033
FAT_MAGIC = 0xcafebabe
FAT_MAGIC_64 = 0xcafebabf

def uleb(b, i):
    r = 0; s = 0
    while i < len(b):
        x = b[i]; i += 1
        r |= (x & 0x7f) << s
        if not (x & 0x80):
            break
        s += 7
    return r, i

def walk(trie):
    nodes = 0; exports = 0; specials = []; oob = 0
    def rec(i, path, depth):
        nonlocal nodes, exports, oob
        if depth > 4096 or i >= len(trie):
            return
        nodes += 1
        tsize, i2 = uleb(trie, i)
        if tsize:
            term = trie[i2:i2+tsize]
            if len(term) >= 1:
                flags, j = uleb(term, 0)
                exports += 1
                kind = flags & 0x03
                tags = []
                if kind == 2: tags.append("ABSOLUTE")
                if flags & 0x04: tags.append("WEAK")
                if flags & 0x08: tags.append("REEXPORT")
                if tags:
                    specials.append((path, "+".join(tags), flags))
        i3 = i2 + tsize
        if i3 >= len(trie):
            return
        nchild = trie[i3]; i3 += 1
        for _ in range(nchild):
            j = i3
            while j < len(trie) and trie[j] != 0:
                j += 1
            edge = trie[i3:j].decode("latin-1"); i3 = j + 1
            child, i3 = uleb(trie, i3)
            if child >= len(trie):
                oob += 1
            else:
                rec(child, path + edge, depth + 1)
    if trie:
        rec(0, "", 0)
    return nodes, exports, specials, oob

def analyze_slice(f, base):
    magic, = struct.unpack_from("<I", f, base)
    if magic != MH_MAGIC_64:
        return None
    cputype, cpusubtype, filetype, ncmds, sizeofcmds, flags, res = \
        struct.unpack_from("<iiIIIII", f, base + 4)
    off = base + 32
    exports = None
    for _ in range(ncmds):
        if off + 8 > len(f):
            break
        cmd, cmdsize = struct.unpack_from("<II", f, off)
        if cmd == LC_DYLD_EXPORTS_TRIE:
            dataoff, datasize = struct.unpack_from("<II", f, off + 8)
            exports = (dataoff, datasize)
        if cmdsize < 8:
            break
        off += cmdsize
    return filetype, exports

def analyze_file(path):
    try:
        with open(path, "rb") as fh:
            head = fh.read(8)
            if len(head) < 8:
                return None
            magic, = struct.unpack_from("<I", head, 0)
            slices = []
            if magic in (FAT_MAGIC, FAT_MAGIC_64):
                nfat, = struct.unpack_from(">I", head, 4)
                fh.seek(0); fat = fh.read(8 + nfat * 20)
                for k in range(nfat):
                    _, _, off, size, _ = struct.unpack_from(">IIIII", fat, 8 + k * 20)
                    slices.append(off)
            elif magic == MH_MAGIC_64:
                slices = [0]
            else:
                return None
            results = []
            for base in slices:
                fh.seek(0); data = fh.read()
                r = analyze_slice(data, base)
                if not r:
                    continue
                filetype, exports = r
                if not exports:
                    results.append((filetype, None))
                    continue
                dataoff, datasize = exports
                fh.seek(dataoff); trie = fh.read(datasize)
                nodes, nexp, specials, oob = walk(trie)
                results.append((filetype, (dataoff, datasize, nodes, nexp, specials, oob)))
            return results
    except OSError:
        return None

lines = []
total = 0; with_trie = 0; total_specials = []; total_oob = 0
for root in ROOTS:
    for dirpath, _, files in os.walk(root):
        for name in files:
            p = os.path.join(dirpath, name)
            if os.path.islink(p):
                continue
            r = analyze_file(p)
            if not r:
                continue
            for filetype, ex in r:
                total += 1
                rel = os.path.relpath(p, OVL)
                if ex is None:
                    continue
                with_trie += 1
                dataoff, datasize, nodes, nexp, specials, oob = ex
                total_oob += oob
                for s in specials:
                    total_specials.append((rel, s[0], s[1], s[2]))
                lines.append("%s\tft=%d\tdataoff=%d\tsize=%d\tnodes=%d\texports=%d\tspecial=%d\toob=%d"
                             % (rel, filetype, dataoff, datasize, nodes, nexp, len(specials), oob))

with open(OUT, "w") as w:
    w.write("images scanned: %d (with LC_DYLD_EXPORTS_TRIE: %d)\n" % (total, with_trie))
    w.write("images with a non-empty trie:\n")
    for l in lines:
        w.write("  " + l + "\n")
    w.write("special nodes (ABSOLUTE/WEAK/REEXPORT): %d\n" % len(total_specials))
    for s in total_specials:
        w.write("  %s : %s : %s : flags=0x%x\n" % s)
    w.write("out-of-bounds child offsets: %d\n" % total_oob)
print("images scanned: %d (with trie: %d); special: %d; oob: %d"
      % (total, with_trie, len(total_specials), total_oob))
PY
echo "written: ${OUT}"
