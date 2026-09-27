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

import struct
import sys

MH_MAGIC_64 = 0xFEEDFACF
LC_SEGMENT_64 = 0x19
# LC_REQ_DYLD | 0x33 -- the 0x80000000 marks it as a dyld-only load command.
LC_DYLD_EXPORTS_TRIE = 0x80000033

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
        self.exports_trie = None
        off = 32
        for _ in range(ncmds):
            cmd, cmdsize = struct.unpack_from("<II", self.d, off)
            if cmd == LC_DYLD_EXPORTS_TRIE:
                dataoff, datasize = struct.unpack_from("<II", self.d, off + 8)
                self.exports_trie = (dataoff, datasize)
            off += cmdsize

    def trie(self):
        if self.exports_trie is None:
            raise SystemExit("no LC_DYLD_EXPORTS_TRIE in this file")
        off, size = self.exports_trie
        return self.d[off:off + size]


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

    def walk(self, start_offset=0, depth=0, s=b"", path=()):
        buf, end = self.buf, self.end
        p = start_offset
        while p is not None:
            self.nodes += 1
            self.max_depth = max(self.max_depth, depth)
            node_off = p

            # 1837: terminalSize = *p++;   NO CHECK ON p
            byte = self.read(p, L_TERMINAL_SIZE, "terminalSize byte")
            if byte is None:
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
            children_remaining = self.read(children, L_CHILDREN_REMAINING,
                                           "childrenRemaining")
            if children_remaining is None:
                return None
            p = children + 1

            # the terminal payload dyld2 steps over with its bare pointer
            payload = None
            if terminal_size != 0:
                try:
                    payload, p = self._payload(p, end, node_off)
                except UlebError as exc:
                    self.violations.append((node_off, L_TERMINAL_SIZE, str(exc)))
                    return None

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
                            self.violations.append(
                                (node_off, L_NODE_OFFSET_CHECK,
                                 "edge %r points at nodeOffset=0x%x, outside the "
                                 "trie (end 0x%x)" % (edge, skipped, end)))
                            return None
                        self._descend(node_off, skipped, end,
                                      depth, path, edge)
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
            if node_offset != 0:
                rest = s[ss:] if matched else b""
                return self.walk(node_offset, depth + 1, rest,
                                 path + (matched or "",))
            p = None
        return None

    def _descend(self, node_off, child_off, end, depth, path, edge):
        """Structural mode only: visit every child, not just the matching one.

        dyld2 never does this -- it breaks out of the child loop on the first
        match -- so this is an enumeration of the trie, not a symbol lookup,
        and it is labelled as such in the output.
        """
        self.edges.append((node_off, edge, child_off, depth))
        return self.walk(child_off, depth + 1, b"", path + (edge,))

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


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    brief = "--brief" in sys.argv
    if not args:
        raise SystemExit(__doc__)
    path = args[0]
    symbol = args[1].encode() if len(args) > 1 else None

    m = MachO(path)
    buf = m.trie()
    print("file:   %s" % path)
    print("trie:   LC_DYLD_EXPORTS_TRIE dataoff=0x%x size=%d (0x%x)"
          % (m.exports_trie[0], m.exports_trie[1], m.exports_trie[1]))
    print("walk:   dyld2 ImageLoader::trieWalk (src/ImageLoader.cpp:1831)")
    if symbol:
        print("symbol: %s  (faithful dyld2 lookup along this path)" % symbol.decode())
    else:
        print("mode:   structural -- every child visited, not a symbol lookup")
    print()

    w = Walker(buf, brief=brief, enumerate=(symbol is None))
    w.walk(0, 0, symbol if symbol else b"")

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

    print()
    if w.violations:
        print("VERDICT: FAIL -- dyld2 would read outside the trie")
        for off, line, what in w.violations:
            print("  node 0x%06x  ImageLoader.cpp:%d" % (off, line))
            print("    %s" % what)
        return 1
    print("VERDICT: PASS -- every read stayed inside the trie")
    return 0


if __name__ == "__main__":
    sys.exit(main())
