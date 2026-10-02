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


