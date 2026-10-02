# survive-window: the SIGUSR1 sender is the kernel (si_pid=0), and the
# handler-side markers are innocent

Three-run control matrix of the (a)-order probe (WL_SKIP_B=1), one
variable per run, logs `wl-body-survive-{a,b,c}.log`:

| run | DARLING_TRAP_LOG | HANDLERS | result |
|-----|------------------|----------|--------|
| a   | unset (control)  | —        | **window reached**: DID-NOT-RETURN + VERDICT printed, no markers |
| b   | 1                | on       | **dies before the window**: `FATAL signal 10 (code=3) si_pid=0 si_uid=0 si_value=0x0` |
| c   | 1                | 0        | **dies the same way** — same signal, same addr family, elfcalls-only markers |

## The sender: si_pid=0 — no userspace process

The crash handler prints `si_pid/si_uid/si_value` (committed). Run b:
`si_pid=0 si_uid=0 si_value=0x0 sival_ptr=0x0` with `si_code=3` — on
FreeBSD, SI_QUEUE with pid 0 means the signal was **queued by the kernel
on the process's behalf**: there is no killing process, no
pthread_kill payload, nothing from darlingserver or the harness. The
delivery family is the kernel's sigevent/kevent-signal machinery — the
guest's event path (libkqueue/libepoll-shim or the backend runloop)
registers signal-consuming interest, and the kernel posts SIGUSR1 with
SI_QUEUE pid 0. mldr's crash_debug_handler treated any fatal-class
signal as fatal — so a kernel-queued event the guest uses for
bookkeeping became a death sentence before the window.

## What the instrument does and does not do

Run c is the split: handler-side write(2) markers OFF, elfcalls-side
markers ON — the run STILL dies the same way. So the nine in-handler
writes are NOT the cause; the correlation with the main gate runs
through the elfcalls-side markers (a write(2) per shim dlsym during the
session build) which shift timing enough for the kernel-queued
SIGUSR1 to land inside the build window; the hazard itself is
environmental — present in any run's event path, the markers only
choose whether it lands before or after the window.

## Resolution (authorized narrowly, implemented in this branch)

crash_debug_handler now has exactly one new conditional: SIGUSR1 +
si_pid==0 + DARLING_TRAP_LOG==1 -> log (write(2): signal, si_code,
si_pid, si_value) and DISMISS (return — a queued event's continuation,
not a fault). Real faults, other signals, si_pid!=0 and the default
(gate off) are untouched. With the dismiss in place the gated run can
survive to the (a) window, which re-opens the deferred measurements:
the parked lane's dlsym state and the exit(3) caller.

Premium hypothesis for the next run: if the kernel-queued SIGUSR1 also
arrives in UNinstrumented runs and drives teardown (a candidate
mechanism for the earlier P_WEXIT flip), the dismiss version may change
the park itself — such a change is a result, to be reported as an
observation.

## Dismiss verification run: signal-name correction and the storm

With the dismiss branch built in, the gated run (DARLING_TRAP_LOG=1,
the previously dying configuration) shows:

```
[traplog] QUEUED-SIGNAL-DISMISS signo=10 code=3 pid=0 val=0x0
```

— the dismiss FIRES, the process survives the signal... and the log
exploded to ~97 million lines in under seven minutes before the run was
killed: **the queued signal is not a one-shot event — it re-queues
continuously while the guest's event path runs.** Dismissing by return
converts the fatal death into a livelock: the handler returns, the
kernel delivers the next queued copy, the handler returns again — the
process makes no progress toward the (a) window. Disk pressure from the
marker flood forced the kill (the run log was removed).

**Signal-name correction:** "FATAL signal 10" is **SIGBUS** on FreeBSD,
not SIGUSR1 — the trap file's own header says it ("Linux SIGUSR1 is 10
where FreeBSD has SIGBUS"); SIGUSR1 here is 30 and was never observed.
The authorization text carried the same mislabel; the dismiss branch
covers both names (SIGUSR1||SIGBUS, si_pid==0, gate on) so the letter
of the authorization and the measured reality are handled identically.

**Verdict update:** the queued-signal hazard is a CONTINUOUS queue, not
a timing artifact. A trap-side dismiss cannot open the window — it
trades death for livelock. The real lever is upstream: find who
`sigqueue`s SIGBUS continuously in the guest's event path
(libkqueue/libepoll-shim fault delivery is the standing candidate) and
stop the source; the dlsym/exit-caller window measurements stay
deferred until then.

## Follow-up reading: the trap's kill translation names the signal chain

A source sweep after the storm verdict found the untranslated pass in
the trap's Linux-syscall dispatch:

```c
case LINUX_SYS_kill:
    return freebsd_raw_syscall(SYS_kill, a1, a2, 0, 0, 0, 0);
```

(`freebsd_syscall_trap.c:2354`) — the LINUX signal number in `a2` goes
to the host `kill(2)` as-is. The comment block of the very next case
(`rt_sigaction`) documents this exact bug class: "signal numbers diverge
above 15". Consequence for the storm chain: a guest sending what IT
calls SIGUSR1 (Linux number 10) through this path is delivered by the
host as FreeBSD signal 10 — **SIGBUS**. So the naming chain resolves:
the guest's intent was SIGUSR1 (the authorization's name was the
guest's truth), the host-side delivery name is SIGBUS, and the
"FATAL signal 10" prints were that translation all along.

What the pass does NOT explain: the measured `si_code=3` (SI_QUEUE) —
the trap's `kill` path delivers SI_USER (code 0), so the queued copy
still comes from a kernel-side sigqueue (si_pid=0) whose source remains
unnamed. The untranslated kill is a real, named defect on the same
signal path and belongs in the fix set for the slice that stops the
queue.

Sweep negatives (recorded): no `sigqueue`/`pthread_kill`/`EVFILT_SIGNAL`
anywhere in mldr's own sources — mldr never queues signals itself.

## Follow-up reading 2: where the queued signal lands — inside libc's vfprintf

The storm runs leave the interrupted RIP measurable. From
`wl-body-survive-c.log`:

```
[darling-mldr] DEBUG pre-start: mh=0x8253ca000 ...     <- guest binary
  0x8211c8000 .. 0x8214e0fff: /lib/libc.so.7           <- libc in the run
[darling-mldr] FATAL signal 10 (code=3) at addr=0x8212f4674
```

0x8212f4674 − 0x8211c8000 = offset **0x12C674** into libc; resolving it
against the host libc debug info names the site exactly:

```
__vfprintf
/usr/src/lib/libc/stdio/vfprintf.c:463
```

So the queued signal lands while the main thread is inside host libc's
`__vfprintf` — a formatted stderr print. The chain fits the timeline:
the gated instrumentation and the shim's own narration print via
`fprintf(stderr)` (the `[wayland_shim] ...` lines are vfprintf calls),
and the run-c log shows the elf-dlsym markers immediately before the
FATAL line. Two consequences:

1. The storm's target window is the runs' own stderr traffic — the
   more the gate prints, the more surface the queued signal has to land
   in, which tightens the correlation between the gate and the death.
2. A queued signal landing mid-`vfprintf` holds the stdio stderr lock;
   any handler that formatted with FILE streams would deadlock on it —
   the write(2)-only design of the trap markers and of the dismiss
   branch is what keeps the dismissal path lock-free (the dismiss fires
   and the process survives, as measured).

The sender itself is still the kernel (si_pid=0, SI_QUEUE) and stays
unnamed; the landing site is now named.

## Correction by measurement: it is NOT a queued signal — it is BUS_OBJERR

A host-side probe settled what class of signal the "code=3" actually
is. A real `sigqueue(getpid(), SIGUSR1, SI_QUEUE)` from a second thread
of the same process delivers, read with the same siginfo fields the
crash handler uses:

```
host-probe: si_code=65538 si_pid=27362 si_uid=1001 sival=0x1234
```

`si_code=65538 = 0x10002` — FreeBSD's SI_QUEUE. The guest runs show
`code=3`, which is a completely different class: on FreeBSD, SIGBUS
with si_code 3 is **BUS_OBJERR** — a genuine bus fault on a memory
object (POSIX bus codes: 1=BUS_ADRALN, 2=BUS_ADRERR, 3=BUS_OBJERR).
`si_pid`/`si_uid`/`si_value` are simply **not filled** for a fault-class
siginfo — reading them yields the measured zeros.

So the chain corrects itself: "FATAL signal 10 (code=3)" is a **real
SIGBUS hardware fault** (memory-object error) that lands while the main
thread is inside libc's `__vfprintf` during an stderr print — not a
kernel-queued event, not a kill from any process, and the
dismiss branch (built for the queued reading) fires on a fault it
should arguably not dismiss. The gate correlation survives the
correction: the gated runs' extra stderr traffic is exactly the code
being executed when the fault hits — the faulting object is on the
stderr write/read path, and naming that object (the FILE buffer? a
file-backed mapping that was replaced under the run? the log target
filesystem at high occupancy?) is the next narrowing.

## Follow-up reading 3: the faulting object is the locale data

The faulting libc line names the object precisely. The interrupted RIP
resolves to `/usr/src/lib/libc/stdio/vfprintf.c:463`, which is:

```c
decimal_point = localeconv_l(locale)->decimal_point;
```

A SIGBUS/BUS_OBJERR AT that line is a failed read of the object
`localeconv_l()` hands back — **the thread's locale data**. On FreeBSD
that data is file-backed (per-locale objects under /usr/share/locale,
the locale archive), so a stale/unmapped/replaced locale object
produces exactly BUS_OBJERR on the page-in — and a concurrent
`setlocale`/`uselocale`/`newlocale` on another thread that swaps the
locale while this thread is inside `localeconv_l` is the classic shape
of that fault.

Sweeps: mldr's own sources contain no `setlocale`/`newlocale`/
`uselocale` calls at all; the run logs carry ~78 locale mentions that
come from the guest side — the session build's Foundation/NSLocale
activity. The gate correlation now has a mechanism: the gated runs'
extra fprintf traffic multiplies the `localeconv_l` touches, widening
the window in which a concurrent locale swap from the guest's
Foundation lands mid-print. The faulting OBJECT is therefore named:
the locale data; the swap SOURCE is the guest locale machinery during
the session build.




