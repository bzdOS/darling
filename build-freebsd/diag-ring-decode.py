#!/usr/bin/env python3
# Decode darlingserver's DIAG resume ring (dserver-diag-ring.bin, see DSERVER_DIAG_RING in
# darlingserver-resume-pending-wakeup.patch) into text, oldest entry first.
# Usage: diag-ring-decode.py <ring.bin> > <run>-diag-ring.log
import struct
import sys

with open(sys.argv[1], "rb") as f:
    data = f.read()
if len(data) < 64 or data[:8] != b"DSDRING1":
    sys.exit("not a DSDRING1 file")
cap, nxt = struct.unpack_from("<QQ", data, 8)
count = min(nxt, cap)
first = nxt - count
out = sys.stdout
out.write("# ring capacity=%d recorded=%d shown=%d\n" % (cap, nxt, count))
for i in range(first, nxt):
    ns, tid, nk, ret, n = struct.unpack_from("<QiiQQ", data, 64 + (i % cap) * 32)
    kind = (nk >> 28) & 0xF
    nstid = nk & 0x0FFFFFFF
    if kind == 1:
        out.write("%d.%09d DIAG resume tid=%d nstid=%d EARLY-not-suspended caller-tid=%d ret=0x%x\n"
                  % (ns // 10**9, ns % 10**9, tid, nstid, struct.unpack("<q", struct.pack("<Q", n))[0], ret))
    else:
        out.write("%d.%09d DIAG resume tid=%d nstid=%d scheduled ret=0x%x n=%d\n"
                  % (ns // 10**9, ns % 10**9, tid, nstid, ret, n))
