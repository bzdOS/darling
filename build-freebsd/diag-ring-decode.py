#!/usr/bin/env python3
# Decode darlingserver's DIAG ring (dserver-diag-ring.bin, see DSERVER_DIAG_RING in
# darlingserver-resume-pending-wakeup.patch and darlingserver-diag-ring-sigexc-events.patch) into text,
# oldest entry first.
# Usage: diag-ring-decode.py <ring.bin> > <run>-diag-ring.log
#        diag-ring-decode.py --hang-class <ring.bin>   one HANG-CLASS line per thread (livelock kind A/B):
#   saved_reply=yes when the thread's last client-pushed reply save (kind 4) has no later re-send (kind 3),
#   last= the thread's last ring event. Kind A = hang with saved_reply=yes, kind B = saved_reply=no.
import struct
import sys

KINDS = {0: "resume-scheduled", 1: "resume-EARLY", 3: "saved-reply-resent", 4: "reply-saved",
         5: "signal", 8: "call", 9: "reply-outbox"}

hang = sys.argv[1] == "--hang-class"
path = sys.argv[2] if hang else sys.argv[1]
with open(path, "rb") as f:
    data = f.read()
if len(data) < 64 or data[:8] != b"DSDRING1":
    sys.exit("not a DSDRING1 file")
cap, nxt = struct.unpack_from("<QQ", data, 8)
count = min(nxt, cap)
first = nxt - count
out = sys.stdout
if not hang:
    out.write("# ring capacity=%d recorded=%d shown=%d\n" % (cap, nxt, count))
saved, last = {}, {}
for i in range(first, nxt):
    ns, tid, nk, ret, n = struct.unpack_from("<QiiQQ", data, 64 + (i % cap) * 32)
    kind = (nk >> 28) & 0xF
    nstid = nk & 0x0FFFFFFF
    t = "%d.%09d" % (ns // 10**9, ns % 10**9)
    if kind in (3, 4, 5, 8) and tid >= 0:
        if kind == 4:
            saved[tid] = "yes saved_kind=%s" % ("pending" if n else "normal")
        elif kind == 3:
            saved[tid] = "no"
        last[tid] = "%s=%d@%s" % (KINDS[kind], n, t)
    if hang:
        continue
    if kind == 1:
        out.write("%s DIAG resume tid=%d nstid=%d EARLY-not-suspended caller-tid=%d ret=0x%x\n"
                  % (t, tid, nstid, struct.unpack("<q", struct.pack("<Q", n))[0], ret))
    elif kind == 0:
        out.write("%s DIAG resume tid=%d nstid=%d scheduled ret=0x%x n=%d\n" % (t, tid, nstid, ret, n))
    else:
        out.write("%s DIAG %s tid=%d nstid=%d n=%d\n" % (t, KINDS.get(kind, "kind%d" % kind), tid, nstid, n))
if hang:
    if not last:
        out.write("HANG-CLASS none (ring has no sigexc/RPC events, recorded=%d)\n" % nxt)
    for tid in sorted(last):
        out.write("HANG-CLASS tid=%d saved_reply=%s last=%s\n" % (tid, saved.get(tid, "no"), last[tid]))
