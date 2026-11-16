# lldb python: dump darlingserver microthread state.
#   (lldb) command script import ds-microthreads-dump.py
#   (lldb) dsdump [maxframes]
# Read-only: only reads target memory. For every DarlingServer::Thread in the thread registry prints
# tid/nstid, process, state, the saved microthread context (_resumeContext) with a call-return-address stack scan
# of its microthread stack (frame pointers are not reliable), and, for threads parked in dtape_mutex_lock /
# dtape_condvar_wait, the queue head, the mutex owner (a dtape thread, resolved to a Thread) and the other waiters.
import lldb

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
        P("    pendingCall=%#x activeCall=%#x contCallback=%#x dtape_thread=%#x" % (d["pending"], d["active"], d["cont"], d["dt"]))
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
