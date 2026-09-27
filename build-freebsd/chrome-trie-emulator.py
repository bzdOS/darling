#!/usr/bin/env python3
"""chrome-trie-emulator.py — walk a Mach-O's LC_DYLD_EXPORTS_TRIE exactly the
way the June dyld does, offline, and say whether it survives.

    python3 build-freebsd/chrome-trie-emulator.py <thin-x86_64-macho> [symbol]

Reads only. Writes nothing. The Mach-O path is argv[1], as are the optional
symbol to trace and --brief.

WHY A WALKER AND NOT A DISASSEMBLER

The June dyld dies at trieWalk+0xa4 on this framework. Mapping that address
onto source (build-freebsd/dyld-rebuild.md, ImageLoader::trieWalk) lands on
the instruction that stores the child pointer, which is not where a SIGSEGV
can come from, so the address alone does not say which field is at fault. This
walks the real trie of the real file instead, and reports which node the walk
stands on when it would step outside the trie -- which is the question the
disassembly cannot answer.

THE ALGORITHM, AND WHY IT IS THIS ONE

It follows dyld2's ImageLoader::trieWalk, src/ImageLoader.cpp:1831-1905, not
dyld3's, because that is the code the June dyld actually runs. The two differ
in a way that matters: dyld2 reads the terminal size as a single byte and only
falls back to a uleb128 when that byte is > 127, and it walks the node payload
through a bare pointer. Its bounds checks are:

    1837  terminalSize = *p++;                    NO CHECK on p
    1841  terminalSize = read_uleb128(p, end);    throws at p == end (safe)
    1848  if ( children > end ) -> log, return NULL
    1853  childrenRemaining = *children++;        children <= end, so a read
                                                   AT end is a 1-byte overread
    1862  c = *p;   (edge scan)                   NO CHECK on p
    1876  while ( (*p & 0x80) != 0 ) ++p;         NO CHECK on p
    1889  if ( (nodeOffset == 0) || (&start[nodeOffset] > end) ) -> log, NULL

The three unchecked reads are what this emulator looks for: dyld2 can walk off
the end of the trie on lines 1837, 1862 and 1876, and where it does, the fault
is a read of whatever follows the trie in the file.

FLAGS

The terminal payload layout and the flag bits are Apple's, from
mach-o/loader.h:1457-1463 -- KIND_ABSOLUTE 0x02, WEAK_DEFINITION 0x04,
REEXPORT 0x08, STUB_AND_RESOLVER 0x10 -- with the payload read as dyld3's
MachOAnalyzer::recurseTrie (dyld3/MachOAnalyzer.cpp:3560) does it: flags, then
image offset, then the reexport ordinal or the stub-and-resolver address, then
the weak-definition address.
"""

import hashlib
import os
import struct
import sys
import threading

# The overlay's export tries are large and deeply nested -- Foundation is
# 35 KB of trie and AppKit 85 KB -- and the walk recurses once per node, so the
# default 1000-frame limit is not enough. Raising the limit alone is not safe
# either: CPython recurses on the C stack too, and a deep walk can run it out
# and die with no message. So the walk runs on a thread with a large stack and
# a matching recursion limit.
RECURSION_LIMIT = 200000
STACK_BYTES = 64 * 1024 * 1024
# Hard ceiling on nodes visited, so a malformed trie cannot make the walk run
# away and eat the machine. 85 KB is the largest blob seen (AppKit), which is
# a few tens of thousands of nodes, so this is far above anything real.
MAX_NODES = 5000000

MH_MAGIC_64 = 0xFEEDFACF
LC_SEGMENT_64 = 0x19
LC_DYLD_INFO = 0x22
# LC_REQ_DYLD | 0x33 -- the 0x80000000 marks it as a dyld-only load command.
LC_DYLD_EXPORTS_TRIE = 0x80000033
LC_DYLD_INFO_ONLY = 0x80000022

# The commands that make up an image's dependent-library list, in the order
# ImageLoaderMachO::sniffLoadCommands counts them (src/ImageLoaderMachO.cpp:302):
# that count IS libraryCount(), and libImage(ordinal-1) indexes it, so the
# order and the membership decide what a re-export ordinal means.
LC_LOAD_DYLIB = 0xC
LC_LOAD_WEAK_DYLIB = 0x80000018
LC_REEXPORT_DYLIB = 0x8000001F
LC_LOAD_UPWARD_DYLIB = 0x80000023
LIBRARY_COMMANDS = (LC_LOAD_DYLIB, LC_LOAD_WEAK_DYLIB, LC_REEXPORT_DYLIB,
                    LC_LOAD_UPWARD_DYLIB)

KIND_MASK = 0x03
KIND_REGULAR = 0x00
KIND_THREAD_LOCAL = 0x01
KIND_ABSOLUTE = 0x02
WEAK_DEFINITION = 0x04
REEXPORT = 0x08
STUB_AND_RESOLVER = 0x10

# Source lines quoted above, used in the report.
L_TERMINAL_SIZE = 1837
L_CHILDREN = 1847
L_CHILDREN_CHECK = 1848
L_CHILDREN_REMAINING = 1853
L_EDGE_SCAN = 1862
L_CHILD_ULEB_SKIP = 1876
L_NODE_OFFSET_CHECK = 1889


class MachO:
    def __init__(self, path):
        self.d = open(path, "rb").read()
        magic, _, _, _, ncmds, _, _, _ = struct.unpack_from("<IiiIIIII", self.d, 0)
        if magic != MH_MAGIC_64:
            raise SystemExit("%s: not a 64-bit thin Mach-O (magic %08x)" % (path, magic))
        self.path = path
        self.exports_trie = None
        self.dyld_info = None
        self.libraries = []          # (path, is_reexport) in load-command order
        off = 32
        for _ in range(ncmds):
            cmd, cmdsize = struct.unpack_from("<II", self.d, off)
            if cmd == LC_DYLD_EXPORTS_TRIE:
                dataoff, datasize = struct.unpack_from("<II", self.d, off + 8)
                self.exports_trie = (dataoff, datasize)
            elif cmd in (LC_DYLD_INFO, LC_DYLD_INFO_ONLY):
                # dyld_info_command: rebase, bind, weak_bind, lazy_bind, export.
                # export_off/export_size is the same export trie as
                # LC_DYLD_EXPORTS_TRIE describes.
                f = struct.unpack_from("<10I", self.d, off + 8)
                self.dyld_info = (f[8], f[9])
            elif cmd in LIBRARY_COMMANDS:
                name_off = struct.unpack_from("<I", self.d, off + 8)[0]
                end = self.d.index(b"\0", off + name_off)
                name = self.d[off + name_off:end].decode("utf-8", "replace")
                self.libraries.append((name, cmd == LC_REEXPORT_DYLIB))
            off += cmdsize
        # needsAddedLibSystemDepency: an image with no library load commands
        # gets libSystem as its library 0, so ordinal 1 means libSystem
        # (ImageLoaderMachO.cpp:295 and :555).
        if not self.libraries:
            self.libraries.append(("/usr/lib/libSystem.B.dylib", False))
            self.libsystem_added = True
        else:
            self.libsystem_added = False

    def export_blob(self):
        """Where dyld2 gets the export trie from, in dyld2's own order of
        preference (ImageLoaderMachOCompressed::findShallowExportedSymbol):

            uint32_t trieFileOffset = fDyldInfo ? fDyldInfo->export_off
                                                : fExportsTrie->dataoff;
            uint32_t trieFileSize   = fDyldInfo ? fDyldInfo->export_size
                                                : fExportsTrie->datasize;

        Two things follow, and both are load-time behaviour rather than
        cosmetics:

          * an image with LC_DYLD_INFO_ONLY is walked through ITS export_off,
            not through any LC_DYLD_EXPORTS_TRIE it may also carry;
          * if fDyldInfo is present, dyld2 does NOT fall back to the trie
            command when export_size is 0 -- findShallowExportedSymbol returns
            NULL instead. A file with both commands and a zero export_size
            therefore exports nothing as far as dyld2 is concerned.

        Returns (offset, size, source-description).
        """
        if self.dyld_info is not None:
            off, size = self.dyld_info
            src = "LC_DYLD_INFO_ONLY export_off/export_size"
            if off == 0 or size == 0:
                src += " (zero: dyld2 returns NULL and does NOT fall back)"
            return off, size, src
        if self.exports_trie is not None:
            off, size = self.exports_trie
            return off, size, "LC_DYLD_EXPORTS_TRIE dataoff/datasize"
        return None

    def trie(self):
        blob = self.export_blob()
        if blob is None:
            raise SystemExit("no export blob: neither LC_DYLD_INFO_ONLY nor "
                             "LC_DYLD_EXPORTS_TRIE in this file")
        off, size, src = blob
        if size == 0:
            raise SystemExit("export blob is empty (%s)" % src)
        return self.d[off:off + size], off, size, src


class UlebError(Exception):
    pass


def read_uleb(buf, p, end, line):
    """dyld2's read_uleb128: throws when p == end, so it is bounds-safe."""
    result = 0
    bit = 0
    while True:
        if p == end:
            raise UlebError("malformed uleb128 at ImageLoader.cpp:%d "
                            "(p == end at trie offset 0x%x)" % (line, p))
        slice_ = buf[p] & 0x7F
        if bit > 63:
            raise UlebError("uleb128 too big at ImageLoader.cpp:%d" % line)
        result |= slice_ << bit
        bit += 7
        if not (buf[p] & 0x80):
            p += 1
            return result, p
        p += 1


def kind_str(k):
    return {KIND_REGULAR: "regular", KIND_THREAD_LOCAL: "thread-local",
            KIND_ABSOLUTE: "absolute"}.get(k, "?%d" % k)


class Walker:
    """dyld2's trieWalk, instrumented. Never reads out of bounds."""

    def __init__(self, buf, brief=False, enumerate=False):
        self.buf = buf
        self.end = len(buf)
        self.brief = brief
        # Structural mode: visit every child instead of only the matching one.
        # dyld2 breaks out of the child loop on the first match, so this is an
        # enumeration of the trie, not a symbol lookup.
        self.enumerate = enumerate
        self.edges = []
        self.cycles = []   # (node, depth, path) -- structural mode only
        self.notes = []    # (node, text) -- observations, not dyld2 faults
        # --deep: follow re-export chains. self.reexport is the MachO this walk
        # belongs to (so the ordinal can be resolved against its dependent-library
        # list), self.root is the overlay to resolve provider paths against, and
        # chain/chain_seen accumulate the hops and stop a cycle.
        self.reexport = None
        self.root = None
        self.deep = False
        self.chain = []
        self.chain_seen = set()
        self.reexport_result = None
        self.names = []    # exported names, harvested while enumerating
        self.violations = []   # (offset, line, what)
        self.nodes = 0
        self.max_depth = 0
        self.feature_nodes = []   # (offset, flags, detail)
        self.candidate = None     # the node reported for the +0xa4 question
        self.terminal_sizes = []

    def read(self, p, line, what):
        """A read dyld2 performs without checking p. Records instead of faulting."""
        if p >= self.end:
            self.violations.append((p, line,
                                    "%s at trie offset 0x%x, trie ends at 0x%x "
                                    "(%d byte(s) past the end)"
                                    % (what, p, self.end, p - self.end + 1)))
            return None
        return self.buf[p]

    def walk(self, start_offset=0, depth=0, s=b"", path=(), on_path=frozenset()):
        buf, end = self.buf, self.end
        p = start_offset
        while p is not None:
            self.nodes += 1
            if self.nodes > MAX_NODES:
                raise SystemExit("node budget of %d exceeded -- the trie walk is "
                                 "not converging, refusing to keep going" % MAX_NODES)
            self.max_depth = max(self.max_depth, depth)
            node_off = p

            # Structural mode visits edges dyld2 would never follow -- dyld2
            # breaks out of the child loop on the FIRST match -- so it can
            # reach a node twice, or loop, on a path no symbol lookup takes.
            # That is worth reporting, but it is not a dyld2 fault, so it is
            # kept apart from the bounds violations that are.
            if self.enumerate and node_off in on_path:
                self.cycles.append((node_off, depth, path))
                return None   # this branch is done; the caller keeps siblings
            here = on_path | {node_off}

            # 1837: terminalSize = *p++;   NO CHECK ON p
            byte = self.read(p, L_TERMINAL_SIZE, "terminalSize byte")
            if byte is None:
                if self.enumerate:
                    return None   # this branch ends here; siblings continue
                return None
            p += 1
            terminal_size = byte
            via = "byte"
            if byte > 127:
                # 1840-1841: --p; terminalSize = read_uleb128(p, end);
                p -= 1
                try:
                    terminal_size, p = read_uleb(buf, p, end, L_TERMINAL_SIZE)
                except UlebError as exc:
                    self.violations.append((node_off, L_TERMINAL_SIZE, str(exc)))
                    return None
                via = "uleb128 (first byte was 0x%02x > 127)" % byte
            self.terminal_sizes.append(terminal_size)

            # 1843: if ( (*s == '\0') && (terminalSize != 0) ) return p;
            if s == b"" and terminal_size != 0:
                # This is the node findShallowExportedSymbol then reads the
                # terminal payload from. If the node is a re-export, the caller
                # of trieWalk follows the chain -- resolve_chain does that.
                if self.reexport is not None:
                    node = self._read_terminal(terminal_size, p, end, node_off)
                    if node is not None:
                        return self._resolve_chain(node, s_path=path, depth=depth)
                return p

            # 1847: const uint8_t* children = p + terminalSize;
            #        ^ this is the instruction at trieWalk+0xa4
            children = p + terminal_size
            # The node the head cares about is the DEEPEST one at which +0xa4
            # is evaluated, not the one with the biggest terminalSize: a lookup
            # that terminates never reaches +0xa4 on its leaf at all, because
            # line 1843 returns first.
            if self.candidate is None or depth >= self.candidate[5]:
                self.candidate = (node_off, terminal_size, p, children, via,
                                  depth, path)

            # 1848: if ( children > end ) -> log, return NULL
            children_past_end = children > end
            children_at_end = children == end

            # 1853: uint8_t childrenRemaining = *children++;
            if children_past_end:
                self.violations.append(
                    (node_off, L_CHILDREN_CHECK,
                     "children = p + terminalSize = 0x%x is past the trie end "
                     "0x%x -- dyld2 logs 'terminalSize=0x%lx extends past end "
                     "of trie' and returns NULL" % (children, end, terminal_size)))
                return None

            # The terminal payload sits BETWEEN p and children, so it has to be
            # consumed while p is still at the start of it -- the order dyld3's
            # recurseTrie uses, and the order that matters here. Parsing it
            # after p has been moved to children+1 reads the child-count byte
            # and the first edge as if they were flags and an image offset, and
            # then every child count, edge and node offset downstream is noise.
            payload = None
            if terminal_size != 0:
                try:
                    payload, p = self._payload(p, end, node_off)
                except UlebError as exc:
                    if self.enumerate:
                        self.notes.append((node_off, str(exc)))
                        return None
                    self.violations.append((node_off, L_TERMINAL_SIZE, str(exc)))
                    return None

            children_remaining = self.read(children, L_CHILDREN_REMAINING,
                                           "childrenRemaining")
            if children_remaining is None:
                return None
            p = children + 1

            node_offset = 0
            matched = None
            # 1856: for (; childrenRemaining > 0; --childrenRemaining)
            for _ in range(children_remaining):
                ss = 0
                wrong_edge = False
                edge_start = p
                # 1862-1871: scan the edge, comparing against s as a C string.
                # dyld2 has no length: once ss runs off the end of s it reads
                # s's NUL, and any non-NUL edge char mismatches. Reproducing
                # that matters -- treating "ran out of s" as "matches" walks
                # the wrong path entirely and invents failures.
                while True:
                    c = self.read(p, L_EDGE_SCAN, "edge character")
                    if c is None:
                        if self.enumerate:
                            break
                        return None
                    if c == 0:
                        break
                    if not wrong_edge:
                        want = s[ss] if ss < len(s) else 0
                        if c != want:
                            wrong_edge = True
                        ss += 1
                    p += 1
                edge = bytes(buf[edge_start:p]).decode("utf-8", "replace")
                p += 1                      # 1874/1887: skip the terminator
                if wrong_edge:
                    # 1876-1878:
                    #     ++p;                            skip the terminator
                    #     while ( (*p & 0x80) != 0 ) ++p; skip continuation bytes
                    #     ++p;                            skip the last byte
                    # The while advances only over bytes that actually have the
                    # high bit set; the trailing ++p is what consumes the byte
                    # that ends the uleb128. Adding an increment inside the loop
                    # as well walks one byte too far and lands on a bogus child
                    # node -- which looks exactly like a corrupt trie.
                    #
                    # dyld2 throws the decoded value away (a non-matching edge
                    # has to be skipped, not followed), but structural mode has
                    # to follow it, so decode it here and bounds-check it the
                    # way line 1889 would have.
                    while True:
                        b = self.read(p, L_CHILD_ULEB_SKIP, "child uleb128 byte")
                        if b is None:
                            return None
                        p += 1
                        if not (b & 0x80):
                            break
                    if p > end:
                        self.violations.append(
                            (node_off, L_CHILD_ULEB_SKIP,
                             "after skipping a non-matching edge %r, p = 0x%x is "
                             "past the trie end 0x%x -- dyld2 logs 'child node "
                             "extends past end of trie'" % (edge, p, end)))
                        return None
                    if self.enumerate:
                        try:
                            skipped, _q = read_uleb(buf, p - 1, end, L_CHILD_ULEB_SKIP)
                        except UlebError as exc:
                            self.violations.append((node_off, L_CHILD_ULEB_SKIP, str(exc)))
                            return None
                        if skipped == 0 or skipped > end:
                            # dyld2 does NOT range-check a non-matching edge:
                            # line 1889 is inside the matching branch only, and
                            # a skipped edge is stepped over, never followed.
                            # So this is an observation about the trie, not a
                            # fault dyld2 can hit.
                            self.notes.append(
                                (node_off, "edge %r points at nodeOffset=0x%x, "
                                 "outside the trie (end 0x%x); dyld2 skips this "
                                 "edge and never follows it" % (edge, skipped, end)))
                            continue
                        self._descend(node_off, skipped, end,
                                      depth, path, edge, here)
                else:
                    # 1888: nodeOffset = read_uleb128(p, end);
                    try:
                        node_offset, p = read_uleb(buf, p, end, L_TERMINAL_SIZE)
                    except UlebError as exc:
                        self.violations.append((node_off, L_TERMINAL_SIZE, str(exc)))
                        return None
                    # 1889: if ( (nodeOffset == 0) || (&start[nodeOffset] > end) )
                    # 1889 compares &start[nodeOffset], where `start` is
                    # trieWalk's start parameter -- the TRIE BASE, always 0,
                    # never the current node. Adding the current node's offset
                    # here silently shifts every child and invents failures.
                    if node_offset == 0 or node_offset > end:
                        self.violations.append(
                            (node_off, L_NODE_OFFSET_CHECK,
                             "matching edge %r has nodeOffset=0x%x; dyld2 checks "
                             "(&start[0x%x] > end 0x%x) and logs 'malformed trie "
                             "child, nodeOffset out of range'"
                             % (edge, node_offset, node_offset, end)))
                        return None
                    matched = edge
                    break

            if not self.brief:
                self._print_node(node_off, depth, terminal_size, via, payload,
                                 children_remaining, children_at_end, path)
            if self.enumerate and terminal_size != 0 and children_remaining == 0:
                self.names.append("".join(x for x in path if x))
            if node_offset != 0:
                rest = s[ss:] if matched else b""
                return self.walk(node_offset, depth + 1, rest,
                                 path + (matched or "",), here)
            p = None
        return None

    def _read_terminal(self, terminal_size, p, end, node_off):
        """(flags, ordinal, imported_name) for a terminal node."""
        if terminal_size == 0:
            return None
        try:
            flags, q = read_uleb(self.buf, p, end, L_TERMINAL_SIZE)
        except UlebError as exc:
            self.violations.append((node_off, L_TERMINAL_SIZE, str(exc)))
            return None
        ordinal = None
        imported = ""
        if flags & REEXPORT:
            try:
                ordinal, q = read_uleb(self.buf, q, end, L_TERMINAL_SIZE)
            except UlebError as exc:
                self.violations.append((node_off, L_TERMINAL_SIZE, str(exc)))
                return None
            e = self.buf.index(b"\0", q)
            imported = self.buf[q:e].decode("utf-8", "replace")
        return (flags, ordinal, imported)

    def _resolve_chain(self, node, s_path, depth):
        """Follow a re-export, the way findShallowExportedSymbol does.

        ImageLoaderMachOCompressed.cpp:493-506:
            ordinal = read_uleb128(p, end);
            importedName = (char*)p;
            if ( importedName[0] == '\\0' ) importedName = symbol;
            if ( (ordinal > 0) && (ordinal <= libraryCount()) ) {
                reexportedFrom = libImage(ordinal-1);
                if ( reexportedFrom == NULL ) return NULL;
                return reexportedFrom->findExportedSymbol(importedName, true, ...);
            }
        so the ordinal indexes the dependent-library list, an empty imported
        name means "same name", and a weak library that failed to load ends the
        chain rather than faulting.
        """
        flags, ordinal, imported = node
        if not (flags & REEXPORT):
            return None
        if imported == "":
            # importedName[0] == '\0' -> reuse the name we were asked for, and
            # that is the WHOLE name, so the edges along the path are
            # concatenated. Taking only the last one is wrong for any symbol
            # split over more than one edge, which is most of them.
            imported = "".join(s_path)
        image = self.reexport
        if ordinal is None or ordinal <= 0 or ordinal > len(image.libraries):
            self.notes.append(
                (0, "re-export ordinal %s out of range (libraryCount=%d) for %r; "
                     "dyld2 falls off the end of the if and returns nothing"
                     % (ordinal, len(image.libraries), imported)))
            self.chain.append((image.path, imported, "(ordinal out of range)", None))
            return None
        provider_path, is_reexport = image.libraries[ordinal - 1]
        if not is_reexport:
            # libImage() would be a plain load, not a re-export chain hop; dyld2
            # still follows it here, because this is a direct re-export node.
            pass
        chain_key = (image.path, imported)
        if chain_key in self.chain_seen:
            self.cycles.append((0, depth, "%s re-exports %r (already in this chain)"
                                % (image.path, imported)))
            self.chain.append((image.path, imported, provider_path, "CYCLE"))
            return None
        self.chain_seen.add(chain_key)
        self.chain.append((image.path, imported, provider_path, None))

        target = os.path.join(self.root, provider_path.lstrip("/"))
        if not os.path.exists(target):
            # "Missing weak-dylib" -> libImage() is NULL -> return NULL.
            self.notes.append((0, "re-export target %s is not present under %s; "
                                  "dyld2 treats it as a missing weak dylib and "
                                  "the chain ends here" % (provider_path, self.root)))
            self.chain[-1] = (image.path, imported, provider_path, "ABSENT")
            return None
        try:
            prov = MachO(target)
        except SystemExit as exc:
            self.violations.append((0, L_TERMINAL_SIZE,
                                    "provider %s: %s" % (provider_path, exc)))
            return None
        # Recurse: the same lookup, in the provider, with the provider as the
        # re-export source for any further hop.
        sub = Walker(prov.trie()[0], brief=True, enumerate=False)
        sub.root = self.root
        sub.reexport = prov
        sub.chain = self.chain
        sub.chain_seen = self.chain_seen
        sub.notes = self.notes
        sub.cycles = self.cycles
        sub.deep = self.deep
        found = sub.walk(0, 0, imported.encode())
        self.violations.extend(sub.violations)
        if sub.reexport_result is not None:
            self.reexport_result = sub.reexport_result
        return found

    def _descend(self, node_off, child_off, end, depth, path, edge, on_path=()):
        """Structural mode only: visit every child, not just the matching one.

        dyld2 never does this -- it breaks out of the child loop on the first
        match -- so this is an enumeration of the trie, not a symbol lookup,
        and it is labelled as such in the output.
        """
        self.edges.append((node_off, edge, child_off, depth))
        deeper = path + (edge,)
        # A node with no children and a non-zero terminal size is a leaf, so
        # the edge path is a complete exported name.
        self._leaves = getattr(self, "_leaves", set())
        self._leaves.add(child_off)
        return self.walk(child_off, depth + 1, b"", deeper, frozenset(on_path))

    def _payload(self, p, end, node_off):
        """Read the terminal payload the way dyld3's recurseTrie does."""
        buf = self.buf
        flags, p = read_uleb(buf, p, end, L_TERMINAL_SIZE)
        detail = []
        if flags & REEXPORT:
            ordinal, p = read_uleb(buf, p, end, L_TERMINAL_SIZE)
            e = buf.index(b"\0", p)
            ordinal_name = buf[p:e].decode("utf-8", "replace")
            p = e + 1
            detail.append("reexport ordinal=%d import=%s" % (ordinal, ordinal_name))
        else:
            image_offset, p = read_uleb(buf, p, end, L_TERMINAL_SIZE)
            detail.append("imageOffset=0x%x" % image_offset)
            if flags & STUB_AND_RESOLVER:
                other, p = read_uleb(buf, p, end, L_TERMINAL_SIZE)
                detail.append("resolver=0x%x" % other)
        if flags & WEAK_DEFINITION:
            other, p = read_uleb(buf, p, end, L_TERMINAL_SIZE)
            detail.append("weakDef=0x%x" % other)
        if flags & KIND_MASK == KIND_ABSOLUTE:
            detail.append("ABSOLUTE")
        if flags & KIND_MASK == KIND_THREAD_LOCAL:
            detail.append("thread-local")
        if detail:
            self.feature_nodes.append((node_off, flags, ", ".join(detail)))
        return (flags, detail), p

    def _print_node(self, node_off, depth, tsz, via, payload, remaining,
                    children_at_end, path):
        buf = self.buf
        print("  node 0x%06x  depth=%-2d terminalSize=%-4d (%s)"
              % (node_off, depth, tsz, via))
        if payload:
            flags, detail = payload
            print("           flags=0x%02x kind=%s %s"
                  % (flags, kind_str(flags & KIND_MASK), "; ".join(detail)))
        if remaining is not None:
            print("           childrenRemaining=%d%s"
                  % (remaining, "   (children == end: 1-byte overread)"
                     if children_at_end else ""))
        if path:
            print("           path: %s" % "".join(x for x in path if x))


class Auditor:
    """--audit: bounds-audit EVERY node of a trie, with no name semantics.

    The name-driven walk above can only ever visit the nodes one lookup reaches,
    and its --enumerate mode is a documented open defect (it mis-parses the
    large overlay tries). Neither is good enough to answer "can this trie put
    trieWalk's foot anywhere it must not?", so this walks the child-offset
    graph itself: from the root, for every node, every child edge, depth first,
    with each node visited once.

    The point of the audit is not which nodes exist but which READS can leave the
    trie, so each node is checked against the reads dyld2 performs there, split
    by whether dyld2 checks them:

      UNCHECKED -- a read dyld2 performs with no guard. Any of these is a fault.
        1837  terminalSize = *p++                      one byte, no test
        1853  childrenRemaining = *children++          reachable when
              1848's `children > end` passes but children == end, so the byte
              read is one past the end. This is the read at trieWalk+0xe4.
        1862  char c = *p; while (c != '\\0') ...       the edge scan, no test
        1876  while ((*p & 0x80) != 0) ++p             the child-uleb skip; the
              test that would catch it is AFTER the read

      GUARDED -- dyld2 tests it, so a violation is a logged refusal, not a
      fault: read_uleb128 throws when p == end (1837's uleb), 1848 logs and
      returns NULL, 1877 logs and returns NULL, 1889 logs and returns NULL.

    A FAIL is an UNCHECKED finding. GUARDED findings are printed and counted
    separately, because calling a caught-and-refused read a fault would make
    the audit cry wolf.

    Two structural invariants keep the audit honest about itself, because a
    traversal that mis-parses a trie reports nonsense bounds as confidently as
    it reports real ones:
      * a child offset is decoded from the position where the uleb actually
        starts, recorded BEFORE it is skipped. The --enumerate path
        reconstructs it after the fact with read_uleb(buf, p - 1), which is
        right only for a one-byte uleb and is the known mis-parse;
      * every exported name the traversal can spell is reconstructed, so the
        caller can diff the set against an independent implementation
        (llvm-objdump --exports-trie). Names agreeing means this parse and
        another one agree about the trie's shape.
    """

    def __init__(self, buf):
        self.buf = buf
        self.end = len(buf)
        self.findings = []     # (node, line, "UNCHECKED"/"GUARDED", text)
        self.visited = set()
        self.nodes = 0
        self.edges = 0
        self.max_depth = 0
        self.names = set()
        self.guarded = [0]

    def _find(self, node, line, severity, text):
        self.findings.append((node, line, severity, text))
        if severity == "GUARDED":
            self.guarded[0] += 1

    def _uleb(self, p, node, line, severity):
        """Decode a uleb128, reporting whether it terminated inside the trie.

        Returns (value, next_p) or None when it ran off the end. p is the real
        start of the uleb, which is what makes the decoded value trustworthy.
        """
        buf, end = self.buf, self.end
        result = 0
        bit = 0
        while True:
            if p >= end:
                self._find(node, line, severity,
                           "uleb128 starting at 0x%x never terminates inside the "
                           "trie end 0x%x" % (p, end))
                return None
            b = buf[p]
            result |= (b & 0x7F) << bit
            bit += 7
            p += 1
            if not (b & 0x80):
                return result, p

    def audit(self):
        buf, end = self.buf, self.end
        # Explicit stack, not recursion: a malformed trie can be deep, and the
        # interpreter's recursion limit is not a property of the trie.
        stack = [(0, 0, ())]
        while stack:
            node, depth, path = stack.pop()
            if node in self.visited:
                continue
            self.visited.add(node)
            self.nodes += 1
            if self.nodes > MAX_NODES:
                raise SystemExit("node budget of %d exceeded -- refusing to keep "
                                 "auditing" % MAX_NODES)
            if node > end:
                self._find(node, L_NODE_OFFSET_CHECK, "GUARDED",
                           "node offset 0x%x is past the trie end 0x%x; dyld2's "
                           "line 1889 test (&start[nodeOffset] > end) catches this "
                           "on a matching edge" % (node, end))
                continue
            if node == end:
                # The subtle one. 1889 tests &start[nodeOffset] > end, and
                # &start[end] == end, so an offset of exactly `end` PASSES it.
                # dyld2 then walks to that node and executes 1837's *p++ with
                # p == end: one byte past the trie, with nothing to catch it.
                self._find(node, L_TERMINAL_SIZE, "UNCHECKED",
                           "node offset is exactly the trie end 0x%x. 1889 tests "
                           "(&start[nodeOffset] > end), and &start[end] == end, so "
                           "this offset is ACCEPTED; the terminalSize = *p++ at "
                           "1837 then reads one byte PAST the end" % end)
                continue
            self.max_depth = max(self.max_depth, depth)
            name = "".join(path)

            # 1837: terminalSize = *p++   -- the read itself is unguarded
            first = buf[node]
            p = node + 1
            terminal_size = first
            if first > 127:
                got = self._uleb(node, node, L_TERMINAL_SIZE, "GUARDED")
                if got is None:
                    continue          # dyld2's read_uleb128 throws here
                terminal_size, p = got

            if terminal_size != 0:
                # A node with a non-empty terminal payload spells an exported
                # name, whether or not it also has children.
                self.names.add(name)

            # 1847: children = p + terminalSize
            children = p + terminal_size

            # 1848: dyld2 checks this and logs. Guarded.
            if children > end:
                self._find(node, L_CHILDREN_CHECK, "GUARDED",
                           "children = p + terminalSize = 0x%x is past the trie "
                           "end 0x%x; dyld2 logs and returns NULL (terminalSize=%d)"
                           % (children, end, terminal_size))
                continue

            # 1853: childrenRemaining = *children++  -- unguarded, and reachable
            # because 1848 tests `>` and not `>=`. children == end reads one byte
            # past the trie. This is trieWalk+0xe4.
            if children == end:
                self._find(node, L_CHILDREN_REMAINING, "UNCHECKED",
                           "children = p + terminalSize = 0x%x equals the trie end, "
                           "so childrenRemaining = *children++ (trieWalk+0xe4) reads "
                           "one byte PAST the end; 1848 tests only `children > end` "
                           "and lets this through" % children)
                continue

            children_remaining = buf[children]
            p = children + 1

            for _ in range(children_remaining):
                # 1862: the edge scan reads *p with no bounds test.
                edge_start = p
                while True:
                    if p >= end:
                        self._find(node, L_EDGE_SCAN, "UNCHECKED",
                                   "edge starting at 0x%x has no NUL before the "
                                   "trie end 0x%x; the scan at trieWalk+0x%X runs "
                                   "off the end" % (edge_start, end,
                                                    L_EDGE_SCAN - 1831))
                        p = None
                        break
                    if buf[p] == 0:
                        break
                    p += 1
                if p is None:
                    break
                edge = buf[edge_start:p].decode("utf-8", "replace")
                p += 1                      # 1874/1887: past the terminator

                # 1876: the continuation-byte scan reads *p before the `p > end`
                # test that follows it.
                uleb_start = p
                while True:
                    if p >= end:
                        self._find(node, L_CHILD_ULEB_SKIP, "UNCHECKED",
                                   "child uleb128 after edge %r starts at 0x%x and "
                                   "its continuation scan reads past the trie end "
                                   "0x%x before the `p > end` test can run"
                                   % (edge, uleb_start, end))
                        p = None
                        break
                    b = buf[p]
                    p += 1
                    if not (b & 0x80):
                        break
                if p is None:
                    break

                if p > end:
                    self._find(node, L_CHILD_ULEB_SKIP, "GUARDED",
                               "after skipping edge %r, p = 0x%x is past the trie "
                               "end 0x%x; dyld2 logs and returns NULL"
                               % (edge, p, end))
                    break

                child_off, _ = self._uleb(uleb_start, node, L_NODE_OFFSET_CHECK,
                                          "GUARDED")
                if child_off is None:
                    break
                self.edges += 1
                if child_off == 0 or child_off > end:
                    # 1889 range-checks only the edge that MATCHED, so a skipped
                    # edge is stepped over and never followed: structural
                    # damage, not necessarily a fault.
                    self._find(node, L_NODE_OFFSET_CHECK, "GUARDED",
                               "edge %r points at nodeOffset=0x%x, outside the trie "
                               "(end 0x%x); dyld2 range-checks only a matching edge, "
                               "so this is stepped over unless the lookup takes it"
                               % (edge, child_off, end))
                    continue
                stack.append((child_off, depth + 1, path + (edge,)))
        return self.findings


def _run_audit(buf, path, blob_src, blob_off, blob_size):
    """--audit: bounds-audit every node, print the verdict, name the reads."""
    print("mode:   --audit structural bounds audit -- every node reached through "
          "child offsets, no name semantics")
    print("reads:  UNCHECKED = 1837 *p++, 1853 *children++ (children == end), "
          "1862 edge scan, 1876 child-uleb continuation")
    print("        GUARDED   = 1837 uleb (read_uleb128 throws), 1848, 1877, 1889 "
          "(dyld2 logs and returns NULL)")
    print()
    a = Auditor(buf)
    findings = a.audit()
    unchecked = [f for f in findings if f[2] == "UNCHECKED"]
    guarded = [f for f in findings if f[2] == "GUARDED"]

    print("nodes audited: %d   edges audited: %d   max depth: %d"
          % (a.nodes, a.edges, a.max_depth))
    print("exported names reconstructed: %d   sha256(sorted, newline-joined): %s"
          % (len(a.names),
             hashlib.sha256("\n".join(sorted(a.names)).encode()).hexdigest()))

    # Optional cross-check against an independent name list (llvm-objdump
    # --exports-trie, passed as a file of names). EQUALITY is the wrong test and
    # measuring it is what showed why: llvm-objump does not list every terminal
    # in the trie. libxpc, for one, spells 112 re-exported __vproc_* names the
    # audit reaches and dyld2's own lookup resolves, that llvm-objdump omits
    # entirely. So the audit is expected to be a SUPERSET, and the failure mode
    # that matters is the audit LOSING a name that another implementation found:
    # that means its traversal is dropping subtrees, and every "in bounds" it
    # reports about them is vacuous.
    lost = False
    cmp_path = os.environ.get("AUDIT_COMPARE_NAMES")
    if cmp_path:
        try:
            with open(cmp_path, "r", errors="replace") as f:
                theirs = {ln.strip() for ln in f if ln.strip()}
        except OSError as exc:
            print("compare: could not read %s: %s" % (cmp_path, exc))
            theirs = None
        if theirs is not None:
            lost = bool(theirs - a.names)
            if a.names == theirs:
                rel = "EQUAL"
            elif theirs - a.names:
                rel = "MISSING %d name(s) the other implementation found" % len(theirs - a.names)
            else:
                rel = ("SUPERSET: the audit spells %d name(s) the other "
                       "implementation does not list" % len(a.names - theirs))
            print("compare: %s   (audit=%d other=%d)"
                  % (rel, len(a.names), len(theirs)))
            if lost:
                for nm in sorted(theirs - a.names)[:10]:
                    print("  LOST  %s" % nm)

    if unchecked:
        print()
        print("UNCHECKED findings: %d -- reads dyld2 performs with no guard" % len(unchecked))
        for node, line, _sev, text in unchecked[:20]:
            print("  0x%06x  ImageLoader.cpp:%d  %s" % (node, line, text))
        if len(unchecked) > 20:
            print("  ... and %d more" % (len(unchecked) - 20))
    else:
        print()
        print("UNCHECKED findings: 0")

    if guarded:
        print()
        print("GUARDED findings: %d -- dyld2 tests these and refuses the lookup; "
              "listed so a refusal is not mistaken for a fault" % len(guarded))
        for node, line, _sev, text in guarded[:20]:
            print("  0x%06x  ImageLoader.cpp:%d  %s" % (node, line, text))
        if len(guarded) > 20:
            print("  ... and %d more" % (len(guarded) - 20))

    print()
    if lost:
        print("VERDICT: FAIL -- the traversal LOST %d name(s) another "
              "implementation found, so it is not auditing those subtrees"
              % len(theirs - a.names))
        return 1
    if unchecked:
        print("VERDICT: FAIL -- %d unguarded read(s) can leave the trie" % len(unchecked))
        for node, line, _sev, text in unchecked:
            print("  node 0x%06x  ImageLoader.cpp:%d  %s" % (node, line, text))
    else:
        print("VERDICT: PASS -- no unguarded read in trieWalk can leave this trie, "
              "at any of its %d node(s)%s"
              % (a.nodes, "; traversal verified against an independent name list"
                 if cmp_path else ""))
    return 1 if unchecked else 0


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    brief = "--brief" in sys.argv
    enumerate_mode = "--enumerate" in sys.argv or "structural" in sys.argv
    audit_mode = "--audit" in sys.argv
    deep = "--deep" in sys.argv
    if not args:
        raise SystemExit(__doc__)
    path = args[0]
    root = args[2] if len(args) > 2 else os.environ.get("DARLING_OVERLAY")
    symbol = args[1].encode() if len(args) > 1 and not args[1].startswith("/") else None

    m = MachO(path)
    buf, blob_off, blob_size, blob_src = m.trie()
    print("file:   %s" % path)
    print("trie:   %s dataoff=0x%x size=%d (0x%x)"
          % (blob_src, blob_off, blob_size, blob_size))

    if audit_mode:
        return _run_audit(buf, path, blob_src, blob_off, blob_size)
    print("walk:   dyld2 ImageLoader::trieWalk (src/ImageLoader.cpp:1831)")
    if symbol:
        print("symbol: %s  (faithful dyld2 lookup along this path)" % symbol.decode())
    elif enumerate_mode:
        print("mode:   structural -- every child visited, NOT a symbol lookup")
        print("WARNING: the structural walk is an OPEN DEFECT on the large overlay")
        print("         tries (it mis-parses Foundation and AppKit, producing")
        print("         impossible child counts and re-entering nodes). It is kept")
        print("         for inspection only. The dyld2-faithful symbol path below")
        print("         is the part that has been cross-checked.")
    else:
        print("mode:   no symbol given -- pass one or more symbol names, or")
        print("         --enumerate to inspect the trie structure (see WARNING)")
        raise SystemExit(2)
    print()

    # Phase 1: structural enumeration, to map the trie and harvest the names it
    # exports. dyld2 does not do this -- it only ever follows the one edge that
    # matches the symbol being looked up -- so nothing found here is a verdict.
    w = Walker(buf, brief=brief, enumerate=enumerate_mode)
    w.reexport = m
    w.root = root
    w.deep = deep
    w.path = (symbol,) if symbol else ()
    w.walk(0, 0, symbol if symbol else b"")
    lookup_names = [symbol.decode()] if symbol else sorted(set(w.names))

    print()
    print("nodes visited: %d, max depth: %d" % (w.nodes, w.max_depth))
    if w.feature_nodes:
        print("nodes with reexport/absolute/weak/stub features: %d"
              % len(w.feature_nodes))
        for off, flags, detail in w.feature_nodes[:12]:
            print("  0x%06x  flags=0x%02x  %s" % (off, flags, detail))
        if len(w.feature_nodes) > 12:
            print("  ... and %d more" % (len(w.feature_nodes) - 12))
    else:
        print("nodes with reexport/absolute/weak/stub features: 0")

    if w.candidate:
        off, tsz, p, children, via, depth, path = w.candidate
        print()
        print("deepest node at which trieWalk+0xa4 was evaluated:")
        print("  node            0x%06x (depth %d)" % (off, depth))
        print("  terminalSize    %d via %s" % (tsz, via))
        print("  p               0x%x" % p)
        print("  children=p+tsz  0x%x      <- stored by the instruction at +0xa4"
              % children)
        print("  path            %s" % ("".join(x for x in path if x) or "(root)"))
        print("  trie end        0x%x      %s"
              % (w.end, "children IS past the end (dyld2 logs and returns NULL)"
                 if children > w.end else
                 "children is at the end exactly (next read overreads by 1)"
                 if children == w.end else "children is inside the trie"))

    if w.edges and not w.brief:
        print()
        print("edges enumerated: %d" % len(w.edges))
        for off, edge, child, depth in w.edges:
            print("  node 0x%04x  edge %-26r -> node 0x%04x" % (off, edge, child))

    # Phase 2: the dyld2-faithful test. Walk each exported name the way
    # findExportedSymbol would, following only the matching edge, and let the
    # verdict come from that alone. A couple of names that are NOT in the trie
    # are included, because a lookup that misses is the case that walks furthest.
    if not symbol:
        missing = [n + "_not_in_this_trie" for n in (lookup_names[:1] or ["x"])]
        probe = lookup_names + missing
        w2 = Walker(buf, brief=True, enumerate=False)
        for nm in probe:
            w2.walk(0, 0, nm.encode())
        print()
        print("dyld2-faithful lookups: %d exported name(s) + %d deliberate miss(es)"
              % (len(lookup_names), len(missing)))
        print("nodes visited during lookups: %d, max depth: %d"
              % (w2.nodes, w2.max_depth))
        if w.notes:
            print("structural notes (NOT dyld2 faults, dyld2 skips these edges): %d"
                  % len(w.notes))
            for off, text in w.notes[:5]:
                print("  node 0x%06x  %s" % (off, text))
        if w2.violations:
            print()
            print("VERDICT: FAIL -- a dyld2 symbol lookup would read outside the trie")
            for off, line, what in w2.violations:
                print("  node 0x%06x  ImageLoader.cpp:%d" % (off, line))
                print("    %s" % what)
            return 1
        print()
        print("VERDICT: PASS -- every dyld2 symbol lookup stayed inside the trie")
        return 0

    if w.chain:
        print()
        print("re-export chain followed (%d hop(s)):" % len(w.chain))
        for src, sym, prov, note in w.chain:
            print("  %-34s --%s--> %s%s" % (os.path.basename(src), sym, prov,
                                            "   [%s]" % note if note else ""))
    if w.cycles:
        print("re-export cycles: %d (dyld2 would recurse until it ran out of stack "
              "or hit a missing weak dylib)" % len(w.cycles))

    print()
    if w.violations:
        print("VERDICT: FAIL -- dyld2 would read outside the trie")
        for off, line, what in w.violations:
            print("  node 0x%06x  ImageLoader.cpp:%d" % (off, line))
            print("    %s" % what)
        return 1
    print("VERDICT: PASS -- every read stayed inside the trie")
    return 0


def _run():
    """Entry point: run main() on a thread with a large stack."""
    global _EXIT
    sys.setrecursionlimit(RECURSION_LIMIT)
    result = {}

    def work():
        try:
            result["code"] = main()
        except BaseException as exc:            # noqa: BLE001 - reported, not swallowed
            result["exc"] = exc

    try:
        threading.stack_size(STACK_BYTES)
    except (ValueError, RuntimeError):
        pass                                   # platform refused; fall through
    t = threading.Thread(target=work)
    t.start()
    t.join()
    if "exc" in result:
        raise result["exc"]
    return result.get("code", 1)


if __name__ == "__main__":
    sys.exit(_run())
