#!/usr/bin/env python3
"""overlay-qualify.py — enumerate an overlay's Mach-Os and decide which of them
dyld2 would ever route through ImageLoader::trieWalk.

The point is EXHAUSTIVENESS. Slices 3 and 5 checked 24 hand-picked candidates,
which proved those 24 were clean and said nothing about the ~380 images the
loader actually walks past on the way to the fault. This walks every file under
the given roots, counts them, and puts each one in exactly one bucket:

Every line is TAB delimited with the bucket as field 1:

    OK<TAB><sweep-path><TAB><original-path><TAB><detail>   sweep this
    SKIP<TAB><original-path><TAB><reason>                 filtered, and here is why

A TALLY line on stderr carries the counters.

Three filters, each of which is a way for the trie walk to be unreachable
rather than a way for it to be interesting:

  * not a Mach-O we can walk -- no x86_64 slice, or not 64-bit. dyld2 walks
    the image matching the running architecture; a 32-bit-only or arm64-only
    file is not on this code path at all. (On this overlay the fat files are
    x86_64+i386, so the x86_64 slice is extracted and the i386 one ignored.)

  * no file-backed export trie -- neither LC_DYLD_INFO[_ONLY] nor
    LC_DYLD_EXPORTS_TRIE. An image with no such command is not one of the
    ImageLoaderMachOCompressed images that calls findShallowExportedSymbol.

  * empty export blob -- the command is there but export_off/export_size is
    zero, which makes dyld2 return NULL and NOT fall back to any other trie
    (ImageLoaderMachOCompressed::findShallowExportedSymbol). A lookup in such
    an image never enters trieWalk, so there is nothing to sweep.

The export-blob decision is NOT reimplemented here. It calls the walker's own
MachO.export_blob(), so "qualifies" cannot drift from "the walker found a trie
to walk" -- the two criteria are the same code, which is the only way an
inventory like this stays honest as the emulator changes.

The overlay is opened read-only. FAT slices are written to --slice-dir, which
the caller owns and removes; the original files are never touched.

  overlay-qualify.py --slice-dir DIR [--root DIR]... [--root DIR]
"""

import argparse
import hashlib
import importlib.util
import os
import struct
import sys

FAT_MAGIC = 0xCAFEBABE
FAT_MAGIC_64 = 0xCAFEBABF
CPU_TYPE_X86_64 = 0x01000007

# Imported by path: the emulator's filename is not a module name.
_EM = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                   "chrome-trie-emulator.py")
_spec = importlib.util.spec_from_file_location("chrome_trie_emulator", _EM)
if _spec is None or _spec.loader is None:
    sys.exit("FATAL: cannot load %s" % _EM)
_mod = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(_mod)
MachO = _mod.MachO


def fat_x86_64(blob):
    """The x86_64 slice of a universal file, or None.

    Returns (slice_bytes, arch_description). Handles FAT_MAGIC and FAT_MAGIC_64
    and reads the headers big-endian, because the fat header is stored
    big-endian even in a little-endian file -- reading it little-endian makes a
    fat header look like the byte-reversed thin magic, which is the mistake
    that made an early version of this report zero thin files out of 384.
    """
    if len(blob) < 8:
        return None, "file too short for a fat header"
    magic, nfat = struct.unpack_from(">II", blob, 0)
    if magic not in (FAT_MAGIC, FAT_MAGIC_64):
        return None, "not a fat file (magic %08x)" % magic
    if nfat == 0 or nfat > 64:
        return None, "implausible nfat_arch %d" % nfat
    step, fmt = (20, ">IIIII") if magic == FAT_MAGIC else (32, ">IIQQII")
    found = []
    for i in range(nfat):
        f = struct.unpack_from(fmt, blob, 8 + i * step)
        cputype, _sub, offset, size, _align = f[:5]
        found.append(cputype)
        if cputype == CPU_TYPE_X86_64:
            if offset + size > len(blob):
                return None, ("x86_64 slice runs past EOF "
                              "(off 0x%x size 0x%x, file 0x%x)"
                              % (offset, size, len(blob)))
            if struct.unpack_from("<I", blob, offset)[0] != _mod.MH_MAGIC_64:
                return None, ("x86_64 slice at 0x%x is not MH_MAGIC_64 (magic %08x)"
                              % (offset, struct.unpack_from("<I", blob, offset)[0]))
            return blob[offset:offset + size], "x86_64 slice of a %d-arch fat file" % nfat
    pretty = ", ".join("0x%x" % c for c in found)
    return None, "no x86_64 slice (archs: %s)" % pretty


def classify(path, slice_dir):
    """-> (bucket, sweep_path, detail). bucket is 'OK' or 'SKIP'."""
    try:
        blob = open(path, "rb").read()
    except OSError as e:
        return "SKIP", None, "unreadable: %s" % e
    if len(blob) < 4:
        return "SKIP", None, "file shorter than a magic"

    little = struct.unpack_from("<I", blob, 0)[0]
    sweep = path
    if little != _mod.MH_MAGIC_64:
        sliced, why = fat_x86_64(blob)
        if sliced is None:
            return "SKIP", None, why
        # Name the slice after the file it came from plus a digest of the
        # source path, so two files with the same basename cannot collide and
        # the report can still name the original.
        stem = os.path.basename(path).replace("/", "_")
        out = os.path.join(slice_dir, "%s.%s.slice"
                           % (stem, hashlib.sha1(path.encode()).hexdigest()[:8]))
        with open(out, "wb") as f:
            f.write(sliced)
        sweep = out
        shape = why
    else:
        shape = "thin MH_MAGIC_64"

    try:
        m = MachO(sweep)
    except SystemExit as e:
        return "SKIP", None, "load commands: %s" % e

    blob_desc = m.export_blob()
    if blob_desc is None:
        return ("SKIP", None,
                "no file-backed export trie (no LC_DYLD_INFO[_ONLY], "
                "no LC_DYLD_EXPORTS_TRIE) -- not an image that calls "
                "findShallowExportedSymbol")
    off, size, src = blob_desc
    if off == 0 or size == 0:
        return "SKIP", None, "export blob empty (%s): dyld2 returns NULL and does not fall back" % src

    nlibs = len(m.libraries)
    detail = "%s, %s off=0x%x size=0x%x, %d dependent librar%s%s" % (
        shape, src.split()[0], off, size, nlibs,
        "y" if nlibs == 1 else "ies",
        ", libSystem synthesised" if m.libsystem_added else "")
    return "OK", sweep, detail


def main():
    ap = argparse.ArgumentParser(add_help=True)
    ap.add_argument("--slice-dir", required=True,
                    help="where x86_64 slices of fat files are written (caller removes)")
    ap.add_argument("--root", action="append", default=[],
                    help="overlay subdirectory to walk recursively (repeatable)")
    a = ap.parse_args()

    os.makedirs(a.slice_dir, exist_ok=True)
    seen_files = 0
    non_macho = 0
    ok = skip = 0
    for root in a.root:
        if not os.path.isdir(root):
            print("SKIP\t%s\troot does not exist" % root, file=sys.stderr)
            continue
        for dirpath, dirnames, filenames in os.walk(root):
            dirnames.sort()
            for name in sorted(filenames):
                p = os.path.join(dirpath, name)
                if not os.path.isfile(p):
                    continue
                seen_files += 1
                try:
                    with open(p, "rb") as f:
                        head = f.read(4)
                except OSError:
                    non_macho += 1
                    continue
                if len(head) < 4:
                    non_macho += 1
                    continue
                if (struct.unpack_from(">I", head, 0)[0] not in (FAT_MAGIC, FAT_MAGIC_64)
                        and struct.unpack_from("<I", head, 0)[0] != _mod.MH_MAGIC_64):
                    non_macho += 1
                    continue
                bucket, sweep, detail = classify(p, a.slice_dir)
                if bucket == "OK":
                    ok += 1
                    print("OK\t%s\t%s\t%s" % (sweep, p, detail))
                else:
                    skip += 1
                    print("SKIP\t%s\t%s" % (p, detail))
    print("TALLY\tfiles_seen=%d\tmacho=%d\tqualified=%d\tfiltered=%d\tnot_macho=%d"
          % (seen_files, seen_files - non_macho, ok, skip, non_macho),
          file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())
