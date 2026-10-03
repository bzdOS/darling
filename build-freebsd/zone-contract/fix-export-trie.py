#!/usr/bin/env python3
"""fix-export-trie.py — rebuild a Mach-O dylib's export trie with an
explicit list of symbol names. ld64.lld's -exported_symbol flags put
symbols in the symtab but not always in the export trie; this script
replaces the trie wholesale.

Usage: fix-export-trie.py <dylib> <export-names-file> <output>
  export-names-file: one symbol name per line (with leading underscore)
  The script reads the dylib's LC_SYMTAB to find each symbol's vmaddr,
  builds a fresh export trie, and writes a new dylib with the trie
  replaced (the LC_DYLD_INFO export offset/size updated, subsequent
  sections shifted).
"""
import struct
import sys


def uleb_encode(v):
    out = bytearray()
    while True:
        b = v & 0x7F
        v >>= 7
        if v:
            out.append(b | 0x80)
        else:
            out.append(b)
            break
    return bytes(out)


def uleb_decode(data, off):
    r = 0
    s = 0
    while True:
        b = data[off]
        off += 1
        r |= (b & 0x7F) << s
        if not (b & 0x80):
            break
        s += 7
    return r, off


def parse_macho(d):
    """Return (ncmds_header_size, [(cmd, cmdsize, off)], symtab_info, dyld_info_off)."""
    magic = struct.unpack_from("<I", d, 0)[0]
    assert magic == 0xFEEDFACF, f"not 64-bit Mach-O: {magic:#x}"
    ncmds = struct.unpack_from("<I", d, 16)[0]
    sizeofcmds = struct.unpack_from("<I", d, 20)[0]
    o = 32
    cmds = []
    symtab = None
    dyld_info = None
    for _ in range(ncmds):
        cmd, cmdsize = struct.unpack_from("<II", d, o)
        cmds.append((cmd, cmdsize, o))
        if cmd == 0x2:  # LC_SYMTAB
            symoff, nsyms, stroff, strsize = struct.unpack_from("<IIII", d, o + 8)
            symtab = dict(symoff=symoff, nsyms=nsyms, stroff=stroff, strsize=strsize)
        elif cmd in (0x22, 0x80000022):  # LC_DYLD_INFO(_ONLY)
            dyld_info = o
        o += cmdsize
    return ncmds, sizeofcmds, cmds, symtab, dyld_info


def get_symbol_vmaddr(d, symtab, name):
    """Find a symbol's vmaddr (the value field in the nlist_64 entry)."""
    for i in range(symtab["nsyms"]):
        so = symtab["symoff"] + i * 16
        n_strx = struct.unpack_from("<I", d, so)[0]
        n_type = d[so + 4]
        n_sect = d[so + 5]
        n_desc = struct.unpack_from("<H", d, so + 6)[0]
        n_value = struct.unpack_from("<Q", d, so + 8)[0]
        sname = d[symtab["stroff"] + n_strx:].split(b"\0")[0].decode(errors="replace")
        if sname == name and (n_type & 0x0E) in (0x0E, 0x0C, 0x0A, 0x08) and (n_type & 0x01):
            return n_value
    return None


def build_trie(symbols):
    """Build an export trie from a list of (name, address_offset) pairs.
    Returns bytes of the trie.
    """
    if not symbols:
        return b"\x00"  # empty trie: single node, terminal_size=0, children=0

    # Build a prefix tree
    class Node:
        def __init__(self):
            self.children = {}  # char -> Node
            self.terminal_addr = None  # None = not terminal

    root = Node()
    for name, addr in symbols:
        node = root
        for ch in name:
            if ch not in node.children:
                node.children[ch] = Node()
            node = node.children[ch]
        node.terminal_addr = addr

    # Serialize: we need to emit nodes in an order where children offsets
    # are known. Use a two-pass approach: first compute sizes, then emit.
    # Actually, the trie format uses absolute offsets from the start of
    # the trie data. We'll do a recursive emit with a post-pass to fix
    # offsets, or emit in reverse (children first).

    # Emit strategy: emit all nodes in DFS order, recording positions.
    # Since child offsets point FORWARD (to already-emitted or later nodes),
    # we emit depth-first and fix up offsets after.

    # Simpler: emit in reverse DFS (post-order), so children are emitted
    # before parents. Then parent's child offsets point backward.
    # But the trie spec says offsets are from the start of the trie,
    # and children can be anywhere.

    # Let's just emit linearly and collect fixups.
    parts = bytearray()
    fixups = []  # (position_in_parts, node_object)

    def emit_node(node):
        start = len(parts)
        # terminal_size
        if node.terminal_addr is not None:
            # terminal info: flags (ULEB128) + address (ULEB128)
            # flags = 0 (regular export), address = offset from image base
            tinfo = uleb_encode(0) + uleb_encode(node.terminal_addr)
            parts.extend(uleb_encode(len(tinfo)))
        else:
            parts.extend(uleb_encode(0))
        # children_count
        parts.extend(uleb_encode(len(node.children)))
        # children: null-terminated string + offset (ULEB128, placeholder)
        for ch in sorted(node.children):
            parts.extend(ch.encode("ascii"))
            parts.append(0)
            fixups.append((len(parts), node.children[ch]))
            parts.extend(b"\x00" * 4)  # placeholder for ULEB128 offset (max 4 bytes)
        return start

    # Emit root first, then process fixups
    # We need a BFS/DFS that emits children after their offset placeholder
    # is written. Use an explicit stack.
    emit_order = []
    stack = [root]
    while stack:
        node = stack.pop()
        pos = emit_node(node)
        emit_order.append((node, pos))
        for ch in sorted(node.children, reverse=True):
            stack.append(node.children[ch])

    # Now fix child offsets: each fixup points to (position, node).
    # We need to know each node's start position.
    node_pos = {}
    for node, pos in emit_order:
        node_pos[id(node)] = pos

    for fixpos, child_node in fixups:
        target = node_pos[id(child_node)]
        # Write ULEB128 at fixpos
        encoded = uleb_encode(target)
        # We reserved 4 bytes; pad with continuation if needed
        if len(encoded) > 4:
            raise RuntimeError(f"offset {target} needs {len(encoded)} bytes, reserved 4")
        # Write encoded, pad rest with... we can't pad ULEB128.
        # Instead, reserve exactly len(encoded) bytes. But we don't know
        # the length until we know the target. Two-pass needed.
        pass

    # The above approach has a problem: ULEB128 length depends on the value.
    # Let's do a proper two-pass: first emit with max-size ULEB128 (5 bytes
    # for 32-bit offsets), then re-emit with actual sizes.
    # Actually, for simplicity, use a fixed-width encoding for child offsets.
    # But the trie spec uses ULEB128...
    #
    # Alternative: emit nodes in an order where we know all offsets before
    # writing child references. Post-order DFS: emit children first, then
    # the parent. Then when emitting the parent, all child positions are known.
    # But we still need to write the parent's child entries BEFORE its own
    # children are emitted (because the parent's header comes first).
    #
    # OK let me just do it properly: recursive emit that returns the bytes,
    # with children emitted inline after the parent's header.

    # Clear and redo with recursive inline emission
    parts = bytearray()

    def emit_recursive(node):
        """Emit node + all descendants, return the bytes."""
        buf = bytearray()
        # We'll emit header first with placeholder offsets, then append
        # children, then patch offsets. Track child start positions.
        # Actually, let's emit children recursively first to know their sizes,
        # but that doesn't give us their absolute positions.
        #
        # Simplest correct approach: emit in two phases.
        # Phase 1: compute the byte layout (positions of every node).
        # Phase 2: emit with correct offsets.
        return None

    # Let me use a different strategy entirely: build the trie as a list of
    # nodes with explicit child indices, then serialize with known positions.
    nodes = []  # each: (terminal_addr_or_None, [(char, child_index)])
    node_index = {}

    def build_node(node):
        idx = len(nodes)
        nodes.append((node.terminal_addr, []))
        node_index[id(node)] = idx
        for ch in sorted(node.children):
            child_idx = build_node(node.children[ch])
            nodes[idx][1].append((ch, child_idx))
        return idx

    build_node(root)

    # Compute positions: each node's serialized size
    # We emit nodes in index order (root=0, then DFS).
    # Position of node i = sum of sizes of nodes 0..i-1.
    # But node sizes depend on child offset lengths, which depend on positions...
    # Circular dependency. Solve by iteration: start with max-size offsets,
    # compute positions, re-encode with actual sizes, repeat until stable.
    MAX_ULEB = 5  # 32-bit offset fits in 5 bytes ULEB128

    def node_size(n):
        taddr, children = nodes[n]
        sz = 0
        # terminal_size ULEB128
        if taddr is not None:
            tinfo_len = len(uleb_encode(0)) + len(uleb_encode(taddr))
            sz += len(uleb_encode(tinfo_len))
        else:
            sz += 1  # uleb(0) = 1 byte
        # children_count ULEB128
        sz += len(uleb_encode(len(children)))
        # children: char + 0 + offset ULEB
        for ch, _ in children:
            sz += len(ch) + 1 + MAX_ULEB  # placeholder
        return sz

    # Iterate to convergence
    offset_sizes = [MAX_ULEB] * len(nodes)  # actual ULEB size for each node's position
    for iteration in range(20):
        positions = []
        pos = 0
        for i in range(len(nodes)):
            positions.append(pos)
            sz = node_size(i)
            # Adjust for actual offset sizes
            taddr, children = nodes[i]
            actual_sz = 0
            if taddr is not None:
                tinfo_len = len(uleb_encode(0)) + len(uleb_encode(taddr))
                actual_sz += len(uleb_encode(tinfo_len))
            else:
                actual_sz += 1
            actual_sz += len(uleb_encode(len(children)))
            for ch, child_idx in children:
                actual_sz += len(ch) + 1 + offset_sizes[child_idx]
            pos += actual_sz
        # Check convergence
        new_sizes = [len(uleb_encode(positions[i])) for i in range(len(nodes))]
        if new_sizes == offset_sizes:
            break
        offset_sizes = new_sizes

    # Emit
    parts = bytearray()
    for i, (taddr, children) in enumerate(nodes):
        if taddr is not None:
            tinfo = uleb_encode(0) + uleb_encode(taddr)
            parts.extend(uleb_encode(len(tinfo)))
        else:
            parts.extend(uleb_encode(0))
        parts.extend(uleb_encode(len(children)))
        for ch, child_idx in children:
            parts.extend(ch.encode("ascii"))
            parts.append(0)
            parts.extend(uleb_encode(positions[child_idx]))

    return bytes(parts)


def main():
    if len(sys.argv) != 4:
        print(__doc__)
        sys.exit(1)
    dylib_path, names_path, out_path = sys.argv[1], sys.argv[2], sys.argv[3]

    d = bytearray(open(dylib_path, "rb").read())
    ncmds, sizeofcmds, cmds, symtab, dyld_info_off = parse_macho(d)
    assert symtab, "no LC_SYMTAB"
    assert dyld_info_off, "no LC_DYLD_INFO"

    # Read export names
    names = []
    for line in open(names_path):
        line = line.strip()
        if line:
            names.append(line)

    # Find each symbol's vmaddr
    symbols = []
    missing = []
    for name in names:
        addr = get_symbol_vmaddr(d, symtab, name)
        if addr is None:
            missing.append(name)
        else:
            symbols.append((name, addr))
    print(f"symbols found in symtab: {len(symbols)}/{len(names)}")
    if missing:
        print(f"missing from symtab: {missing[:10]}{'...' if len(missing)>10 else ''}")

    # Build the trie
    trie = build_trie(symbols)
    print(f"new trie size: {len(trie)} bytes")

    # Read current dyld_info export offset/size
    # LC_DYLD_INFO: rebase_off, rebase_sz, bind_off, bind_sz, weak_off, weak_sz,
    #               lazy_off, lazy_sz, export_off, export_sz (10 x uint32)
    old_export_off = struct.unpack_from("<I", d, dyld_info_off + 8 + 32)[0]
    old_export_sz = struct.unpack_from("<I", d, dyld_info_off + 8 + 36)[0]
    print(f"old export stream: off=0x{old_export_off:x} size=0x{old_export_sz:x}")

    # Strategy: append the new trie at the end of the file, update the
    # dyld_info to point there. This avoids shifting all sections.
    # The old export stream becomes dead space (harmless).
    new_export_off = len(d)
    d.extend(trie)
    # Pad to 8-byte alignment
    while len(d) % 8:
        d.append(0)

    # Update LC_DYLD_INFO export offset and size
    struct.pack_into("<I", d, dyld_info_off + 8 + 32, new_export_off)
    struct.pack_into("<I", d, dyld_info_off + 8 + 36, len(trie))

    open(out_path, "wb").write(d)
    print(f"wrote {out_path} ({len(d)} bytes), export stream now at 0x{new_export_off:x} size 0x{len(trie):x}")


if __name__ == "__main__":
    main()
