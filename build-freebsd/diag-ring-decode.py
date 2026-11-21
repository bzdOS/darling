#!/usr/bin/env python3
# Decode darlingserver's DIAG ring (dserver-diag-ring.bin, see DSERVER_DIAG_RING in
# darlingserver-resume-pending-wakeup.patch and darlingserver-diag-ring-sigexc-events.patch) into text,
# oldest entry first.
# Usage: diag-ring-decode.py <ring.bin> > <run>-diag-ring.log
#        diag-ring-decode.py --hang-class <ring.bin>   one HANG-CLASS line per thread (livelock kind A/B):
#   saved_reply=yes when the thread's last client-pushed reply save (kind 4) has no later re-send (kind 3),
#   last= the thread's last event (from the per-thread table when the ring has one, else the ring window). Kind A = hang with saved_reply=yes, kind B = saved_reply=no.
#        diag-ring-decode.py --counts <ring.bin>   one RING-COUNTS line (entries per kind; k8/k9 > 0 proves the ring is written)
import struct
import sys

KINDS = {0: "resume-scheduled", 1: "resume-EARLY", 3: "saved-reply-resent", 4: "reply-saved",
         5: "signal", 8: "call", 9: "reply-outbox",
         10: "suspend-nosignal", 11: "reply-held", 12: "held-released", 13: "nosig-refused", 14: "signal-resent"}

counts = sys.argv[1] == "--counts"
hang = sys.argv[1] == "--hang-class"
path = sys.argv[2] if (hang or counts) else sys.argv[1]
with open(path, "rb") as f:
    data = f.read()
if len(data) < 64 or data[:8] != b"DSDRING1":
    sys.exit("not a DSDRING1 file")
cap, nxt = struct.unpack_from("<QQ", data, 8)
count = min(nxt, cap)
first = nxt - count
out = sys.stdout
if not hang and not counts:
    out.write("# ring capacity=%d recorded=%d shown=%d\n" % (cap, nxt, count))
saved, last, kc = {}, {}, {}
for i in range(first, nxt):
    ns, tid, nk, ret, n = struct.unpack_from("<QiiQQ", data, 64 + (i % cap) * 32)
    kind = (nk >> 28) & 0xF
    nstid = nk & 0x0FFFFFFF
    kc[kind] = kc.get(kind, 0) + 1
    t = "%d.%09d" % (ns // 10**9, ns % 10**9)
    if kind in (3, 4, 5, 8) and tid >= 0:
        if kind == 4:
            saved[tid] = "yes saved_kind=%s" % ("pending" if n else "normal")
        elif kind == 3:
            saved[tid] = "no"
        last[tid] = "%s=%d@%s" % (KINDS[kind], n, t)
    if hang or counts:
        continue
    if kind == 1:
        out.write("%s DIAG resume tid=%d nstid=%d EARLY-not-suspended caller-tid=%d ret=0x%x\n"
                  % (t, tid, nstid, struct.unpack("<q", struct.pack("<Q", n))[0], ret))
    elif kind == 0:
        out.write("%s DIAG resume tid=%d nstid=%d scheduled ret=0x%x n=%d\n" % (t, tid, nstid, ret, n))
    else:
        out.write("%s DIAG %s tid=%d nstid=%d n=%d\n" % (t, KINDS.get(kind, "kind%d" % kind), tid, nstid, n))
toff, tslots, tdropped, tstride = struct.unpack_from("<QQQQ", data, 24)
tstride = tstride or 64  # old rings: 64-byte slots, no stride field
table = []
if hang and toff and tslots and len(data) >= toff + tslots * tstride:
    for j in range(tslots):
        tid, nst, lns, lk, _p, ln, cn, cns, sns, ev = struct.unpack_from("<iiQIIQQQQQ", data, toff + j * tstride)
        ext = struct.unpack_from("<8Q", data, toff + j * tstride + 64) if tstride >= 128 else None
        if tid:
            table.append((tid, nst, lns, lk, ln, cn, cns, sns, ev, ext))
if hang and toff:
    out.write("HANG-CLASS table threads=%d slots=%d dropped_events=%d%s\n"
              % (len(table), tslots, tdropped, " TABLE-OVERFLOW" if tdropped else ""))
if table:
    # per-thread table (never wrapped): every thread that ever made a call or got a signal
    now = max(r[2] for r in table)
    def _age(x):
        return "%.3fs" % ((now - x) / 1e9) if x else "never"
    for tid, nst, lns, lk, ln, cn, cns, sns, ev, ext in sorted(table):
        out.write("HANG-CLASS tid=%d saved_reply=%s last=%s=%d age=%.3fs last_call=%d call_age=%.3fs last_signal_age=%s events=%d\n"
                  % (tid, saved.get(tid, "no"), KINDS.get(lk, "kind%d" % lk), ln, (now - lns) / 1e9, cn,
                     (now - cns) / 1e9 if cns else -1, "%.3fs" % ((now - sns) / 1e9) if sns else "never", ev))
        if ext:
            hns, hc, rns, rc, sus, susc, sigc, pk = ext
            nosig, heldr, rel = pk & 0x1FFFFF, (pk >> 21) & 0x1FFFFF, (pk >> 42) & 0x1FFFFF
            out.write("HANG-CLASS-SUSP tid=%d sig5=%d last_sig5_age=%s held6=%d last_held6_age=%s released7=%d last_released7_age=%s suspended_call23=%d last_call23_age=%s nosig10=%d reply_held11=%d released12=%d\n"
                      % (tid, sigc, _age(sns), hc, _age(hns), rc, _age(rns), susc, _age(sus), nosig, heldr, rel))
    tot = [0, 0, 0, 0, 0, 0]
    for r in table:
        if r[9]:
            e = r[9]
            tot[0] += e[6]; tot[1] += e[5]; tot[2] += e[7] & 0x1FFFFF; tot[3] += (e[7] >> 21) & 0x1FFFFF; tot[4] += (e[7] >> 42) & 0x1FFFFF; tot[5] += e[1]
    out.write("HANG-CLASS-SUSP-TOTAL signalled5=%d suspended_call23=%d nosignal10=%d reply_held11=%d released12=%d held6=%d confirmed=%d\n"
              % (tot[0], tot[1], tot[2], tot[3], tot[4], tot[5], tot[1] + tot[2]))
elif hang:
    if not last:
        out.write("HANG-CLASS none (ring has no sigexc/RPC events, recorded=%d)\n" % nxt)
    for tid in sorted(last):
        out.write("HANG-CLASS tid=%d saved_reply=%s last=%s\n" % (tid, saved.get(tid, "no"), last[tid]))
if counts:
    out.write("RING-COUNTS recorded=%d shown=%d %s\n" % (nxt, count, " ".join("k%d=%d" % (k, kc.get(k, 0)) for k in (0, 1, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14))))
