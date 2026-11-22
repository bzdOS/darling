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
         10: "suspend-nosignal", 11: "reply-held", 12: "held-released", 13: "nosig-refused", 14: "signal-resent", 15: "checkin-trace"}

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
ci = {}
cyc = []
lat = []
saved_sig = []
ack_fail = []
attach_cnt = {}
win_us = []
lstate = []
ob = []
for i in range(first, nxt):
    ns, tid, nk, ret, n = struct.unpack_from("<QiiQQ", data, 64 + (i % cap) * 32)
    kind = (nk >> 28) & 0xF
    nstid = nk & 0x0FFFFFFF
    kc[kind] = kc.get(kind, 0) + 1
    if kind == 14 and (n >> 56) == 8:
        ack_fail.append(n & 0xFFFF)   # push_reply ack write failed (errno); 32 = EPIPE: the client was gone, the server no longer dies of SIGPIPE
    if kind == 14 and (n >> 56) == 7:
        attach_cnt[n & 0xFF] = attach_cnt.get(n & 0xFF, 0) + 1   # 1 = attach window completed, 2 = PT_DETACH with signal 0 although the wait status was not a stop
    if kind == 14 and (n >> 56) == 6:
        win_us.append(n & ((1 << 56) - 1))
    if kind == 14 and (n >> 56) == 5:
        saved_sig.append(n & 0xFF)   # signal handed back at PT_DETACH of the memory-access shim
    if kind == 14 and (n >> 56) == 4:
        # host state of the target at the watchdog's first re-send: kinfo_proc of the thread
        lstate.append(((n >> 0) & 1, (n >> 1) & 1, (n >> 2) & 1, (n >> 3) & 1, (n >> 4) & 1, (n >> 8) & 0xFF,
                       bytes([(n >> (16 + 8 * k)) & 0xFF for k in range(4)]).rstrip(b"\0").decode("ascii", "replace")))
    if kind == 14 and (n >> 56) == 3:
        # signal latency: first interrupt_enter after a suspend signal: resent<<55 | usSinceResend<<22 | usSinceSend
        lat.append(((n >> 55) & 1, (n >> 22) & 0x3FFFFF, n & 0x3FFFFF))
    if kind == 14 and (n >> 56) == 2:
        # watchdog cycle outcome: outcome<<52 | resends<<44 | usSinceLastResend<<22 | usSinceSend
        cyc.append(((n >> 52) & 0xF, (n >> 44) & 0xFF, (n >> 22) & 0x3FFFFF, n & 0x3FFFFF))
    if kind == 15:
        # checkin trace: n = sub << 56 | value, ret = hash of the destination socket path (sub 1 reply routed, 2 delivery errno, 3 next call index)
        sub, val = n >> 56, n & ((1 << 56) - 1)
        if sub in (4, 5):
            ob.append((sub, val, ns))
        else:
            ci.setdefault(ret, {})[sub] = (val, tid)
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
    elif kind == 15:
        out.write("%s DIAG checkin-trace tid=%d sub=%d value=%d dest=0x%x\n" % (t, tid, n >> 56, n & ((1 << 56) - 1), ret))
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
if hang:
    # checkin replies that were not delivered cleanly: no delivery record (sub 2) or a non-zero errno
    for dest, d in sorted(ci.items()):
        if 2 not in d or d[2][0] != 0:
            out.write("CHECKIN-TRACE dest=0x%x tid=%s route=%s delivery_errno=%s next_call_index=%s\n"
                      % (dest, d.get(1, d.get(3, (0, "?")))[1], d.get(1, ("?",))[0], d[2][0] if 2 in d else "none", d.get(3, ("?",))[0]))
def cycle_summary():
    # One record per suspend signal, at the handler's first interrupt_enter (ring kind 14 sub 3).
    # LOST-ORIGINAL: the watchdog re-sent and the handler started within 2 ms after the re-send (the re-send got through, the original
    # did not, or was merged with it). ORIGINAL-LATE: the handler started with no re-send, or later than 2 ms after one. Heuristic:
    # the kernel's pending state is not readable. Latencies are send -> interrupt_enter of the cycles with no re-send.
    resent = [r for r in lat if r[0]]
    lost = sum(1 for r in resent if r[1] < 2000)
    plain = sorted(r[2] for r in lat if not r[0])
    def pct(q):
        return plain[min(len(plain) - 1, int(len(plain) * q))] // 1000 if plain else 0
    return ("SIG-CYCLES signals=%d resent=%d LOST-ORIGINAL=%d ORIGINAL-LATE=%d (no re-send: %d, latency ms median %d p99 %d max %d)"
            % (len(lat), len(resent), lost, len(plain) + len(resent) - lost, len(plain), pct(0.5), pct(0.99), pct(1.0)))
if counts or hang:
    out.write(cycle_summary() + "\n")
if counts or hang:
    wl = sorted(win_us)
    out.write("MEMACCESS-WINDOWS over 1 ms: %d (median %d us, p90 %d us, max %d us)\n" % (len(wl), wl[len(wl)//2] if wl else 0, wl[int(len(wl)*0.9)] if wl else 0, wl[-1] if wl else 0))
    out.write("PUSH-REPLY-ACK-FAILED %d (EPIPE %d)\n" % (len(ack_fail), sum(1 for x in ack_fail if x == 32)))
    out.write("MEMACCESS-ATTACH windows=%d handed_back_sub5=%d (sig 65: %d) detach_sig0_anomaly=%d\n" % (attach_cnt.get(1, 0), len(saved_sig), sum(1 for x in saved_sig if x == 65), attach_cnt.get(2, 0)))
    out.write("MEMACCESS-SIGNAL-SAVED handed back at detach=%d (sig 65: %d)\n" % (len(saved_sig), sum(1 for x in saved_sig if x == 65)))
if (counts or hang) and lstate:
    # target thread state when the first re-send was needed (a lost original): signal 65 pending / blocked / caught / ignored in the
    # kernel, thread state (ki_stat: 1 idle 2 run 3 sleep 4 stop 5 zomb 6 wait 7 lock) and wait channel
    import collections
    c = collections.Counter(("found" if f else "not-found", "pend=%d" % pe, "blocked=%d" % bl, "caught=%d" % ca, "ignored=%d" % ig, "stat=%d" % st, "wmesg=%s" % wm)
                            for f, pe, bl, ca, ig, st, wm in lstate)
    for k, v in c.most_common():
        out.write("SIG-LOSS-STATE %d x %s\n" % (v, " ".join(k)))
if counts or hang:
    # outbox trace: sub 4 = a sendmmsg that did not send the whole batch (errno in the low 16 bits; 11 = EAGAIN), sub 5 value 1 = outbox
    # full, waiting for EPOLLOUT, value 2 = EPOLLOUT seen. A reply queue stalled for good ends with sub 5/1 and no later sub 5/2.
    part = [v for k, v, t in ob if k == 4]
    eagain = sum(1 for v in part if (v & 0xFFFF) == 11)
    waits = sum(1 for k, v, t in ob if k == 5 and v == 1)
    wakes = sum(1 for k, v, t in ob if k == 5 and v == 2)
    lastk = ob[-1][:2] if ob else None
    out.write("OUTBOX partial_sends=%d eagain=%d waiting_for_epollout=%d epollout_seen=%d last=%s%s\n"
              % (len(part), eagain, waits, wakes, lastk, " STALLED-ON-EPOLLOUT" if lastk == (5, 1) else ""))
if counts:
    out.write("RING-COUNTS recorded=%d shown=%d %s\n" % (nxt, count, " ".join("k%d=%d" % (k, kc.get(k, 0)) for k in (0, 1, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15))))
