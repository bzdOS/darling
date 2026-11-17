# lldb python: dump darlingserver microthread state.
#   (lldb) command script import ds-microthreads-dump.py
#   (lldb) dsdump [maxframes]
# Read-only: only reads target memory. For every DarlingServer::Thread in the thread registry prints
# tid/nstid, process, state, the saved microthread context (_resumeContext) with a call-return-address stack scan
# of its microthread stack (frame pointers are not reliable), and, for threads parked in dtape_mutex_lock /
# dtape_condvar_wait, the queue head, the mutex owner (a dtape thread, resolved to a Thread) and the other waiters.
# Further sections: ksyn (psynch) wait queues per process and the shared table (user address, owner and waiter
# threads, whether the owner is in the live registry, its saved stack); the kernelAsync queue, its semaphore and the
# runner threads (wait state of each); the server work queue; per-process kqchans with notification-deferral and
# outbox state, and the psynch ring (DSERVER_PSYNCH_RING=1 in the server environment; see
# duct-tape/internal-include/darlingserver/duct-tape/psynch-ring.h for the 128-byte entry layout).
import lldb
import re
import struct
import subprocess

# ---- guest image map: address -> image+offset (from the host mappings of the guest process) -----------------------
_MAPS = {}

def _maps(pid):
    """[(start, end, path)] for the file-backed mappings of host process pid (procstat -v), cached per dump."""
    if pid in _MAPS:
        return _MAPS[pid]
    out = []
    try:
        txt = subprocess.run(["procstat", "-v", str(pid)], capture_output=True, text=True, timeout=10).stdout
        for ln in txt.splitlines()[1:]:
            f = ln.split()
            if len(f) < 10:
                continue
            try:
                out.append((int(f[1], 16), int(f[2], 16), f[10] if len(f) > 10 else ""))
            except ValueError:
                pass
    except Exception:
        pass
    _MAPS[pid] = out
    return out

def img_off(pid, addr):
    """'name+0xoff' (offset from the lowest mapping of that file, an estimate for images mapped in pieces),
    'anon[start-end]+0xoff' for anonymous memory, '?' when the process map is unavailable."""
    if not pid or not addr:
        return "?"
    maps = _maps(pid)
    for st, en, path in maps:
        if st <= addr < en:
            if path and path not in ("-", ""):
                base = min(a for a, _, p in maps if p == path)
                return "%s+%#x" % (path.rsplit("/", 1)[-1], addr - base)
            return "anon[%#x-%#x]+%#x" % (st, en, addr - st)
    return "unmapped"

# ---- psynch ring ------------------------------------------------------------------------------------------------
_RING_OPS = {1: "rd_lock", 2: "wr_lock", 3: "rw_unlock", 4: "rw_handle_unlock", 5: "rd_cont", 6: "wr_cont",
             7: "mutexwait", 8: "mutexdrop", 9: "mutex_cont", 10: "cvwait", 11: "cvsignal", 12: "cvbroad",
             13: "cv_cont", 14: "thread_suspend", 15: "thread_resume", 16: "thread_park", 17: "thread_unpark"}
_RING_RES = {1: "wait", 2: "granted", 3: "woken", 4: "done", 5: "error", 6: "prepost", 7: "spurious"}
_RING_FMT = "<QQQIHH6I2IHBBHH6I6IQ"  # 128 bytes, see psynch-ring.h

def dump_ring(target, proc, info, P):
    on = _gv(target, "dtape_psynch_ring_on")
    ring = _gv(target, "dtape_psynch_ring")
    size = _gv(target, "dtape_psynch_ring_size")
    head = _gv(target, "dtape_psynch_ring_head")
    if not (on and ring and size and head):
        P("(psynch ring symbols not found: server built without the ring)")
        return
    if not _u(on) or not _u(ring):
        P("(psynch ring off: start the server with DSERVER_PSYNCH_RING=1)")
        return
    n = _u(size); hd = _u(head)
    assert struct.calcsize(_RING_FMT) == 128
    raw = _mem(proc, _u(ring), n * 128)
    if raw is None:
        P("(psynch ring unreadable)")
        return
    nst_pid = {}
    for d in info:
        if d.get("nstid") is not None and d.get("pid"):
            nst_pid[d["nstid"]] = d["pid"]
    ents = []
    for i in range(n):
        f = struct.unpack_from(_RING_FMT, raw, i * 128)
        if f[0]:
            ents.append(f)
    ents.sort(key=lambda f: f[0])
    P("psynch ring: size=%d recorded=%d valid=%d (oldest seq %s) format: seq t tid(guest) op res uaddr img type qr qw L/U/S before>after a0 a1 aux wake skip" % (
        n, hd, len(ents), ents[0][0] if ents else "-"))
    for f in ents:
        (seq, ts, uaddr, tid, op, res, l0, u0, s0, l1, u1, s1, a0, a1, ty, qr, qw, nw, nk) = f[:19]
        wake = f[19:25]; skip = f[25:31]; aux = f[31]
        img = img_off(nst_pid.get(tid), uaddr) if uaddr else "-"
        P("ring seq=%d t=%d tid=%d op=%s res=%s uaddr=%#x img=%s type=%d qr=%d qw=%d L=%#x>%#x U=%#x>%#x S=%#x>%#x a0=%#x a1=%#x aux=%#x wake=%d[%s] skip=%d[%s]" % (
            seq, ts, tid, _RING_OPS.get(op, op), _RING_RES.get(res, res), uaddr, img, ty, qr, qw, l0, l1, u0, u1, s0, s1, a0, a1, aux,
            nw, ",".join(str(x) for x in wake[:min(nw, 6)]), nk, ",".join(str(x) for x in skip[:min(nk, 6)])))

def _u(v):
    return v.GetValueAsUnsigned() if v and v.IsValid() else 0

def _mem(proc, addr, n=8):
    err = lldb.SBError()
    b = proc.ReadMemory(addr, n, err)
    return b if err.Success() else None

def _rd64(proc, addr):
    err = lldb.SBError()
    v = proc.ReadUnsignedFromMemory(addr, 8, err)
    return v if err.Success() else None

def _find_registry(target):
    ty = target.FindFirstType("DarlingServer::Registry<DarlingServer::Thread>")
    sc = target.FindSymbols("DarlingServer::threadRegistry()::registry")
    for i in range(sc.GetSize()):
        c = sc.GetContextAtIndex(i)
        sy = c.GetSymbol()
        if sy.IsValid() and sy.GetName() == "DarlingServer::threadRegistry()::registry":
            addr = sy.GetStartAddress().GetLoadAddress(target)
            if ty.IsValid():
                return target.CreateValueFromAddress("threadRegistry", lldb.SBAddress(addr, target), ty)
    return None

class Ctx:
    def __init__(self, target):
        self.t = target
        self.p = target.GetProcess()
        self.codecache = {}

    def sym(self, addr):
        a = self.t.ResolveLoadAddress(addr)
        s = a.GetSymbol()
        if s.IsValid():
            off = addr - s.GetStartAddress().GetLoadAddress(self.t)
            le = a.GetLineEntry()
            loc = ""
            if le.IsValid():
                loc = " at %s:%d" % (le.GetFileSpec().GetFilename(), le.GetLine())
            return "%s`%s+%d%s" % (a.GetModule().GetFileSpec().GetFilename(), s.GetName(), off, loc)
        return None

    def is_code(self, addr):
        if addr in self.codecache:
            return self.codecache[addr]
        a = self.t.ResolveLoadAddress(addr)
        sec = a.GetSection()
        ok = bool(sec.IsValid() and (sec.GetPermissions() & lldb.ePermissionsExecutable))
        if ok:
            # accept only addresses preceded by a call instruction
            b = _mem(self.p, addr - 8, 8)
            ok = False
            if b:
                bb = bytearray(b)
                # e8 rel32 ending at addr ; ff /2 forms (2,3,6,7 bytes)
                if bb[3] == 0xe8:
                    ok = True
                elif bb[6] == 0xff and (bb[7] & 0x38) == 0x10:
                    ok = True
                elif bb[5] == 0xff and (bb[6] & 0x38) == 0x10:
                    ok = True
                elif bb[3] == 0xff and (bb[4] & 0x38) == 0x10:
                    ok = True
                elif bb[2] == 0xff and (bb[3] & 0x38) == 0x10:
                    ok = True
                elif bb[1] == 0xff and (bb[2] & 0x38) == 0x10:
                    ok = True
                elif bb[0] == 0xff and (bb[1] & 0x38) == 0x10 and False:
                    ok = True
        self.codecache[addr] = ok
        return ok

    def scan(self, rip, rsp, stack_lo, stack_hi, maxf, rbp=0):
        # frame-pointer walk (darlingserver and duct-tape are built with frame pointers). The saved context is
        # inside the getcontext stub (called from Thread::suspend); rbp is suspend()'s frame, so [rbp+8] is its caller.
        out = [(rip, self.sym(rip) or "?")]
        bp = rbp
        if not (stack_lo and stack_lo <= bp < stack_hi):
            stack_lo, stack_hi = rsp - 65536, rsp + 262144
        while bp and len(out) < maxf and stack_lo <= bp < stack_hi and bp % 8 == 0:
            ra = _rd64(self.p, bp + 8)
            nbp = _rd64(self.p, bp)
            if not ra:
                break
            out.append((ra, self.sym(ra) or "?"))
            if nbp is None or nbp <= bp:
                break
            bp = nbp
        if not any("microthreadWorker" in x[1] for x in out):
            # chain broken (duct-tape C code is built without frame pointers): conservative scan of the stack words
            # that look like return addresses (preceded by a call). Entries marked '~' may include stale slots.
            have = set(x[0] for x in out)
            a = rsp
            end = min(rsp + 16384, stack_hi if stack_hi > rsp else rsp + 16384)
            while a < end and len(out) < maxf:
                v = _rd64(self.p, a)
                if v and v not in have and v > 0x10000 and self.is_code(v):
                    have.add(v)
                    out.append((v, "~" + (self.sym(v) or "?")))
                a += 8
        return out

def _field(v, *names):
    for n in names:
        c = v.GetChildMemberWithName(n)
        if c.IsValid():
            v = c
        else:
            return None
    return v

# ---- psynch ksyn wait queues, deferred kernel work, kqchan state -----------------------------------------------
def _gv(target, name):
    v = target.FindFirstGlobalVariable(name)
    return v if v.IsValid() else None

def _foff(t, *path):
    off = 0
    for n in path:
        found = None
        for f in t.fields:
            if f.name == n:
                found = f
        if not found:
            return None
        off += found.GetOffsetInBytes()
        t = found.GetType()
    return off

def dump_psynch(target, proc, info, thread_of_dt, xoff, stacks, P, maxf):
    kt = target.FindFirstType("ksyn_wait_queue")
    if not kt.IsValid():
        P("ksyn_wait_queue type not found (no debug info for the duct-tape pthread code)")
        return
    ft = target.FindFirstType("dtape_task")
    o_hash = _foff(kt, "kw_hash"); o_addr = _foff(kt, "kw_addr"); o_owner = _foff(kt, "kw_owner")
    o_type = _foff(kt, "kw_type"); o_inq = _foff(kt, "kw_inqueue"); o_io = _foff(kt, "kw_iocount")
    o_l = _foff(kt, "kw_lword"); o_u = _foff(kt, "kw_uword"); o_s = _foff(kt, "kw_sword")
    o_pf = _foff(kt, "kw_pflags"); o_fake = _foff(kt, "kw_fakecount")
    o_q0 = _foff(kt, "kw_ksynqueues")
    qt = target.FindFirstType("ksyn_queue")
    qsz = qt.GetByteSize()
    o_qcnt = _foff(qt, "ksynq_count")
    # ksyn_waitq_element is opaque in the shared headers; layout from pthread/kern_internal.h
    o_el_next = 0; o_el_thr = 24; o_el_lseq = 36
    o_ph = _foff(ft, "p_pthhash")
    mask_v = _gv(target, "pthhash")
    mask = _u(mask_v) if mask_v else None
    gh = _gv(target, "pth_glob_hashtbl")
    P("pthhash mask=%s (hash table is shared by hashinit calls: last caller wins), pth_glob_hashtbl=%s" % ("%#x" % mask if mask is not None else "?", "%#x" % _u(gh) if gh else "?"))
    def rd32(a):
        b = _mem(proc, a, 4)
        return int.from_bytes(b, "little") if b else None
    def thr_desc(dt_addr):
        d = thread_of_dt(dt_addr)
        if d:
            return "tid=%d/ns%d(%s)" % (d["tid"], d["nstid"], "susp" if d["suspended"] else ("run" if d["running"] else "idle"))
        return "%#x(no Thread)" % dt_addr
    tables = []
    if gh and _u(gh):
        tables.append(("global", _u(gh)))
    seenp = set()
    for d in info:
        if d.get("dtask") and d["dtask"] not in seenp:
            seenp.add(d["dtask"])
            ph = _rd64(proc, d["dtask"] + o_ph)
            if ph:
                tables.append(("pid=%s %s" % (d["pid"], d["exe"]), ph))
    owners = {}
    total = 0
    for label, tbl in tables:
        if mask is None:
            continue
        for b in range(mask + 1):
            kw = _rd64(proc, tbl + b * 8)
            guard = 0
            while kw and guard < 4096:
                guard += 1
                total += 1
                addr = _rd64(proc, kw + o_addr)
                own = _rd64(proc, kw + o_owner)
                inq = rd32(kw + o_inq); io = rd32(kw + o_io); ty = rd32(kw + o_type)
                l = rd32(kw + o_l); u = rd32(kw + o_u); s = rd32(kw + o_s); pf = rd32(kw + o_pf)
                ws = []
                # kw_type is masked to KSYN_WQTYPE_*: 1 mutex, 2 condvar, 4 rwlock, 8 sema. Queue 0 is
                # KSYN_QUEUE_READ, queue 1 KSYN_QUEUE_WRITE (mutex and condvar waiters sit on queue 1).
                tmask = (ty & 0xff) if ty is not None else 0
                for qi in range(2):
                    qa = kw + o_q0 + qi * qsz
                    cur = _rd64(proc, qa)
                    n = 0
                    while cur and n < 512:
                        th = _rd64(proc, cur + o_el_thr)
                        ls = rd32(cur + o_el_lseq)
                        ws.append("%s:%s@seq%#x" % (("RW"[qi] if tmask == 4 else ("C" if tmask == 2 else ("M" if tmask == 1 else "Q%d" % qi))), thr_desc(th - xoff) if th else "none", ls or 0))
                        cur = _rd64(proc, cur + o_el_next); n += 1
                if inq or ws or own:
                    mpid = re.match(r"pid=(\d+)", label)
                    P("kwq %#x [%s] uaddr=%#x img=%s type=%s owner_thread=%#x inqueue=%s iocount=%s pflags=%#x lword=%#x uword=%#x sword=%#x" % (kw, label, addr or 0, img_off(int(mpid.group(1)) if mpid else None, addr or 0), ty, own or 0, inq, io, pf or 0, l or 0, u or 0, s or 0))
                    if own:
                        od = thread_of_dt(own - xoff)
                        P("   owner: %s" % (("Thread tid=%d nstid=%d pid=%s state=%s dead=%s" % (od["tid"], od["nstid"], od["pid"], "suspended" if od["suspended"] else ("running" if od["running"] else "idle/not-scheduled"), od["dead"])) if od else "NOT IN LIVE THREAD REGISTRY (stale kw_owner reference)"))
                        if od:
                            owners[od["tid"]] = od
                    P("   waiters: %s" % (" ".join(ws) if ws else "(none)"))
                kw = _rd64(proc, kw + o_hash)
    P("(%d kwq scanned across %d tables)" % (total, len(tables)))
    for tid, od in sorted(owners.items()):
        P("owner tid=%d: state=%s defer=%d term=%s" % (tid, "suspended" if od["suspended"] else ("running" if od["running"] else "idle"), od["defer"], od["term"]))
        for i, (a, s) in enumerate(stacks.get(tid) or []):
            P("   #%d %#x %s" % (i, a, s))
        if not stacks.get(tid):
            P("   (no saved microthread stack)")

def dtape_wait_state(target, dt):
    ty = target.FindFirstType("dtape_thread")
    if not dt or not ty.IsValid():
        return "?"
    v = target.CreateValueFromAddress("dth", lldb.SBAddress(dt, target), ty)
    x = _field(v, "xnu_thread")
    if x is None:
        return "?"
    return "xnu state=%#x wait_result=%d wait_event=%#x" % (_u(_field(x, "state")), _u(_field(x, "wait_result")), _u(_field(x, "wait_event")))

def dump_async(target, proc, info, P, C, maxf):
    P("--- deferred kernel work (Thread::kernelAsync, thread.cpp) ---")
    for nm in ("kernelAsyncRunnerQueue", "kernelAsyncRunnersAvailable", "permanentKernelAsyncRunners", "kernelAsyncRunnerQueueSempahore"):
        v = None
        sc = target.FindGlobalVariables(nm, 4)
        for i in range(sc.GetSize()):
            v = sc.GetValueAtIndex(i); break
        if v is None or not v.IsValid():
            P("  %s: not found" % nm); continue
        if nm == "kernelAsyncRunnerQueue":
            nv = v.GetNonSyntheticValue()
            c = nv.GetChildMemberWithName("c")
            sz = _field(nv, "c", "__size_", "__value_")
            P("  %s: queued=%s" % (nm, _u(sz) if sz is not None else "?(fields: %s)" % [c.GetChildAtIndex(j).GetName() for j in range(c.GetNumChildren())]))
        elif nm == "kernelAsyncRunnersAvailable":
            P("  %s=%d" % (nm, _u(v)))
        elif nm == "permanentKernelAsyncRunners":
            P("  %s: size=%d" % (nm, v.GetNumChildren()))
            for j in range(v.GetNumChildren()):
                e = v.GetChildAtIndex(j)
                tp = e.GetChildMemberWithName("__ptr_")
                if not tp.IsValid():
                    tp = e
                th = tp.Dereference()
                tid = _u(_field(th, "_tid"))
                susp = bool(_u(_field(th, "_suspended"))); run = bool(_u(_field(th, "_running")))
                P("    runner[%d] Thread %#x tid=%d nstid=%d state=%s dead=%s term=%s in_registry=%s" % (j, _u(tp), tid, _u(_field(th, "_nstid")), "suspended(parked)" if susp else ("running" if run else "idle"), bool(_u(_field(th, "_dead"))), bool(_u(_field(th, "_terminating"))), any(d["addr"] == _u(tp) for d in info)))
                P("      flags: deferralState=%d running=%s pendingCall=%#x continuationCb=%#x; %s" % (_u(_field(th, "_deferralState")), run, _u(_field(th, "_pendingCall", "__ptr_")), _u(_field(th, "_continuationCallback")), dtape_wait_state(target, _u(_field(th, "_dtapeThread")))))
                mc = _field(th, "_resumeContext", "uc_mcontext")
                if susp and mc is not None:
                    lo = _u(_field(th, "_stack", "base")); hi = lo + _u(_field(th, "_stack", "size"))
                    for k, (a, sy) in enumerate(C.scan(_u(_field(mc, "mc_rip")), _u(_field(mc, "mc_rsp")), lo, hi, maxf, _u(_field(mc, "mc_rbp")))):
                        P("      #%d %#x %s" % (k, a, sy))
        else:
            sp = _u(v)
            P("  %s=%#x" % (nm, sp))
            if sp:
                try:
                    xs = _u(_field(v.Dereference(), "xnu_semaphore"))
                    st = target.FindFirstType("semaphore")
                    if xs and st.IsValid():
                        sv = target.CreateValueFromAddress("sem", lldb.SBAddress(xs, target), st)
                        P("    xnu semaphore %#x: count=%s active=%s" % (xs, _field(sv, "count").GetValue(), _field(sv, "active").GetValue()))
                        wq = _field(sv, "waitq")
                        P("    waitq: %s" % str(wq).replace("\n", " ")[:700])
                    else:
                        P("    semaphore: %s" % str(v.Dereference()).replace("\n", " ")[:600])
                except Exception as e:
                    P("    (deref failed %s)" % e)

def dump_diag(target, proc, P):
    P("--- dsdiag counters (resume/suspend/doWork trace) ---")
    sc = target.FindGlobalVariables("dsdiag_", 0)
    out = []
    for nm in ("resume_calls","resume_early_not_suspended","resume_scheduled","suspend_calls","suspend_after_early_resume","dowork_calls","dowork_deferred","dowork_already_running","dowork_terminating","dowork_dead","dowork_top_return","dowork_resume_ctx","dowork_no_pending_call","dowork_new_call"):
        v = _gv(target, "dsdiag_" + nm)
        if v is None:
            out.append("%s=?" % nm); continue
        x = _field(v.GetNonSyntheticValue(), "__a_", "__a_value") if v.GetNonSyntheticValue() is not None else None
        out.append("%s=%s" % (nm, _u(x) if x is not None else str(v.GetValue() or v.GetSummary())))
    P("  " + " ".join(out))

def dump_workqueue(target, proc, P):
    P("--- server work queue (Server::scheduleThread) ---")
    sp = _gv(target, "sharedInstancePointer")
    if sp is None:
        sc = target.FindGlobalVariables("sharedInstancePointer", 2)
        sp = sc.GetValueAtIndex(0) if sc.GetSize() else None
    if sp is None or not _u(sp):
        P("  server instance not found"); return
    srv = sp.Dereference()
    wq = _field(srv, "_workQueue")
    items = _field(wq, "_workItems", "c", "__size_", "__value_")
    P("  _workItems raw: %s" % str(_field(wq, "_workItems")).replace("\n", " ")[:400])
    P("  queued work items=%s threadsAvailable=%s workers=%s dying=%s" % (_u(items) if items is not None else "?", _u(_field(wq, "_threadsAvailable")), _field(wq, "_workerThreads").GetNumChildren(), _u(_field(wq, "_dying"))))

def dump_kqchans(target, proc, info, P):
    P("--- kqchans (per process _kqchannels) ---")
    seen = set()
    for d in info:
        pa = d.get("procaddr")
        if not pa or pa in seen:
            continue
        seen.add(pa)
        pv = d["procval"]
        km = pv.GetChildMemberWithName("_kqchannels")
        if not km.IsValid():
            continue
        n = km.GetNumChildren()
        if n == 0:
            continue
        P("process pid=%s %s: %d kqchans" % (d["pid"], d["exe"], n))
        # libc++ unordered_map node: next@0 hash@8 key@16 shared_ptr(ptr@24); first node pointer at table+16
        node = _rd64(proc, km.GetLoadAddress() + 16)
        kqt = target.FindFirstType("DarlingServer::Kqchan")
        while node:
            key = _rd64(proc, node + 16)
            kaddr = _rd64(proc, node + 24)
            node = _rd64(proc, node)
            if not kaddr:
                continue
            kq = target.CreateValueFromAddress("kq", lldb.SBAddress(kaddr, target), kqt)
            ptr = target.CreateValueFromData("kqp", lldb.SBData.CreateDataFromUInt64Array(target.GetByteOrder(), 8, [kaddr]), target.GetBasicType(lldb.eBasicTypeUnsignedLong))
            dyn = kq.GetDynamicValue(lldb.eDynamicCanRunTarget)
            tn = dyn.GetTypeName() if dyn and dyn.IsValid() else "?"
            def ct(box):
                m = _field(kq, box, "_messages")
                return m.GetNumChildren() if m is not None and m.IsValid() else -1
            de = _field(kq, "_deferredNotification", "__engaged_")
            P("  kqchan %#x key=%#x type=%s debugID=%d canSend=%s canSendNotification=%s deferNotification=%s deferredNotificationHeld=%s notificationCount=%d outbox=%d inbox=%d" % (
                _u(ptr), key, tn, _u(_field(kq, "_debugID")), bool(_u(_field(kq, "_canSend"))),
                bool(_u(_field(kq, "_canSendNotification"))), bool(_u(_field(kq, "_deferNotification"))),
                bool(_u(de)) if de is not None else "?", _u(_field(kq, "_notificationCount")), ct("_outbox"), ct("_inbox")))

def dsdump(debugger, command, result, internal_dict):
    target = debugger.GetSelectedTarget()
    proc = target.GetProcess()
    maxf = int(command.strip() or "40")
    C = Ctx(target)
    out = []
    P = out.append
    reg = _find_registry(target)
    if reg is None:
        print("cannot find threadRegistry")
        return
    m = reg.GetChildMemberWithName("_map")
    n = m.GetNumChildren()
    P("registry _map entries=%d" % n)
    threads = []
    for i in range(n):
        kv = m.GetChildAtIndex(i)
        sec = kv.GetChildMemberWithName("second")
        if not sec.IsValid():
            sec = kv.GetChildAtIndex(1)
        ptr = sec.GetChildMemberWithName("__ptr_")
        if not ptr.IsValid():
            ptr = sec
        threads.append(ptr)
    # dtape_thread_t ranges for owner / waiter resolution
    dtt = target.FindFirstType("dtape_thread")
    dsz = dtt.GetByteSize() if dtt.IsValid() else 0
    xoff = 0
    mlink_off = 0
    for f in dtt.get_fields_array() if hasattr(dtt, "get_fields_array") else dtt.fields:
        if f.name == "xnu_thread": xoff = f.GetOffsetInBytes()
        if f.name == "mutex_link": mlink_off = f.GetOffsetInBytes()
    mt = target.FindFirstType("dtape_mutex")
    cvt = target.FindFirstType("dtape_condvar")
    def off(t, name):
        for f in t.fields:
            if f.name == name: return f.GetOffsetInBytes()
        return None
    mq_off = off(mt, "dtape_queue_head") if mt.IsValid() else None
    cq_off = off(cvt, "queue_head") if cvt.IsValid() else None
    info = []
    for tp in threads:
        th = tp.Dereference() if tp.GetType().IsPointerType() else tp
        d = {}
        d["addr"] = _u(tp)
        d["tid"] = _u(_field(th, "_tid"))
        d["nstid"] = _u(_field(th, "_nstid"))
        pr = _field(th, "_process", "__ptr_")
        d["pid"] = d["exe"] = None
        if pr is not None and _u(pr):
            pd = pr.Dereference()
            d["pid"] = _u(_field(pd, "_pid"))
            d["procaddr"] = _u(pr); d["procval"] = pd
            d["dtask"] = _u(_field(pd, "_dtapeTask"))
            d["nspid"] = _u(_field(pd, "_nspid"))
            ex = _field(pd, "_executablePath")
            d["exe"] = ex.GetSummary() if ex else None
        d["suspended"] = bool(_u(_field(th, "_suspended")))
        d["running"] = bool(_u(_field(th, "_running")))
        d["dead"] = bool(_u(_field(th, "_dead")))
        d["term"] = bool(_u(_field(th, "_terminating")))
        d["defer"] = _u(_field(th, "_deferralState"))
        d["pending"] = _u(_field(th, "_pendingCall", "__ptr_"))
        d["active"] = _u(_field(th, "_activeCall", "__ptr_"))
        d["cont"] = _u(_field(th, "_continuationCallback"))
        d["dt"] = _u(_field(th, "_dtapeThread"))
        d["stack_base"] = _u(_field(th, "_stack", "base"))
        d["stack_size"] = _u(_field(th, "_stack", "size"))
        mc = _field(th, "_resumeContext", "uc_mcontext")
        d["rip"] = _u(_field(mc, "mc_rip")) if mc else 0
        d["rsp"] = _u(_field(mc, "mc_rsp")) if mc else 0
        d["rbp"] = _u(_field(mc, "mc_rbp")) if mc else 0
        info.append(d)
    by_dt = {}
    for d in info:
        if d["dt"]:
            by_dt[d["dt"]] = d
    def thread_of_dt(a):
        for base, d in by_dt.items():
            if base <= a < base + dsz:
                return d
        return None
    # dtape_suspend_lock
    sl = target.FindFirstGlobalVariable("dtape_suspend_lock")
    if sl.IsValid():
        P("dtape_suspend_lock @%#x = %s" % (sl.GetLoadAddress(), sl.GetValue() or sl.GetSummary() or str([sl.GetChildAtIndex(i).GetValue() for i in range(sl.GetNumChildren())])))
    else:
        P("dtape_suspend_lock: symbol not found")
    P("")
    stacks = {}
    for d in info:
        if d["dt"] and d["suspended"] and d["rip"]:
            lo = d["stack_base"]; hi = lo + d["stack_size"]
            stacks[d["tid"]] = C.scan(d["rip"], d["rsp"], lo, hi, maxf, d["rbp"])
    for d in sorted(info, key=lambda x: x["tid"]):
        st = "dead" if d["dead"] else ("suspended(parked)" if d["suspended"] else ("running" if d["running"] else "idle/not-scheduled"))
        P("=== Thread %s tid=%d nstid=%d pid=%s exe=%s state=%s defer=%d term=%s" % (
            "%#x" % d["addr"], d["tid"], d["nstid"], d["pid"], d["exe"], st, d["defer"], d["term"]))
        P("    pendingCall=%#x activeCall=%#x contCallback=%#x dtape_thread=%#x %s" % (d["pending"], d["active"], d["cont"], d["dt"], dtape_wait_state(target, d["dt"])))
        fr = stacks.get(d["tid"])
        if fr:
            P("    saved context rip=%#x rsp=%#x rbp=%#x  (frame-pointer walk; frames without fp, e.g. libc, end the chain)" % (d["rip"], d["rsp"], d["rbp"]))
            for i, (a, s) in enumerate(fr):
                P("      #%d %#x %s" % (i, a, s))
            names = " ".join(s for a, s in fr)
            hd = None; kind = None
            if d["dt"] and ("dtape_mutex_lock" in names or "dtape_condvar_wait" in names):
                kind = "mutex" if "dtape_mutex_lock" in names.split("dtape_condvar_wait")[0] or "dtape_condvar_wait" not in names else "condvar"
                link = d["dt"] + mlink_off
                # walk tqe_prev back until the address is not another thread's mutex_link
                seen = 0; cur = link; chain = []
                while seen < 4096:
                    prev = _rd64(proc, cur + 8)
                    if prev is None or prev == 0:
                        hd = None; break
                    pt = thread_of_dt(prev)
                    if pt is not None and prev == pt["dt"] + mlink_off:
                        chain.append(pt); cur = prev; seen += 1
                        continue
                    hd = prev; break
                if hd:
                    qoff = mq_off if kind == "mutex" else cq_off
                    base = hd - (qoff or 0)
                    first = _rd64(proc, hd)
                    ws = []; cur = first; k = 0; linked = False
                    while cur and k < 256:
                        if cur == link:
                            linked = True
                        wt = thread_of_dt(cur)
                        ws.append("tid=%d" % wt["tid"] if wt else "%#x?" % cur)
                        cur = _rd64(proc, cur); k += 1
                    if not linked:
                        P("    (frames mention dtape_mutex_lock/condvar_wait but this thread is not on that queue)")
                    else:
                        P("    PARKED on %s %#x (queue head %#x)" % (kind, base, hd))
                        if kind == "mutex":
                            own = _rd64(proc, base)
                            od = thread_of_dt(own - xoff) if own else None
                            P("      owner xnu_thread=%#x -> %s" % (own or 0, ("Thread tid=%d nstid=%d pid=%s %s" % (od["tid"], od["nstid"], od["pid"], "suspended" if od["suspended"] else ("running" if od["running"] else "idle")) if od else "no matching microthread (kernel/finished/none)")))
                        P("      waiters(FIFO): %s" % " ".join(ws))
        else:
            P("    (no saved microthread context: %s)" % ("running now" if d["running"] else "not parked"))
        P("")
    for title, fn in (("psynch ksyn wait queues", lambda: dump_psynch(target, proc, info, thread_of_dt, xoff, stacks, P, maxf)),
                      ("kernelAsync", lambda: dump_async(target, proc, info, P, C, maxf)),
                      ("psynch ring", lambda: dump_ring(target, proc, info, P)),
                      ("dsdiag", lambda: dump_diag(target, proc, P)),
                      ("workqueue", lambda: dump_workqueue(target, proc, P)),
                      ("kqchans", lambda: dump_kqchans(target, proc, info, P))):
        P("--- %s ---" % title)
        try:
            fn()
        except Exception as e:
            P("  section failed: %r" % (e,))
    # owners' stacks summary
    P("--- real OS thread backtraces ---")
    for i in range(proc.GetNumThreads()):
        th = proc.GetThreadAtIndex(i)
        P("os thread #%d tid=%d" % (th.GetIndexID(), th.GetThreadID()))
        for j in range(min(th.GetNumFrames(), 40)):
            f = th.GetFrameAtIndex(j)
            P("   #%d %s" % (j, f))
    print("\n".join(out))

def __lldb_init_module(debugger, internal_dict):
    debugger.HandleCommand("command script add -f ds_microthreads_dump.dsdump dsdump")
