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
