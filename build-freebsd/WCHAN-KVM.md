# wchan-kvm: the kernel view of the parked lane's process, on a
# correctly-resolved pid

Order: kvm snapshots of the parked variant-(a) lane's process — ps -H
thread rows with wchan, per-pid procstat -kk, and a kernel-existence
check — taken at a pid resolved by parentage (no target argv pattern),
×3 distribution, with a pre-park baseline.

## Pid resolution (no target argv patterns)

The fresh mldr that runs the probe is the **parent** of the fresh
darlingserver: `launch-dynamic` exec's mldr, mldr forks the
darlingserver as its child and runs the target in-process. So:

1. fresh darlingserver = the highest-pid `darlingserver
   /tmp/darling-dynamic-smoke` process by `ps args`;
2. its **parent** (`ppid`) = the fresh mldr;
3. cross-checked against the target's own `start: pid=N` line — they
   match;
4. validation at snapshot time: `ps -p PID -o lstart,comm` must be
   non-empty.

A ppid-walk from the darlingserver to its *children* resolves the
launchd-global, which defuncts early — not the target. The old
`pgrep -f 'mldr-real/mldr'` route is blind to the fresh mldr (vchroot
rewrites the target's argv) and matched a stale low pid (1474) in the
earlier run 3; it is not used here.

## Reproduction command (acceptance artifact)

`sh build-freebsd/wchan-watch.sh` — the watcher starts the probe (gate
OFF), resolves the fresh mldr by parentage, and snapshots:
`ps -H -axo pid,tid,state,wchan,comm` for the guest, `procstat -kk
<pid>`, `kill -0 <pid>` (kernel existence), and the same for the
darlingserver (control). The probe's stdout runs on a pty
(`ptyrun.py`) so the step markers are real-time. Env-derived roots
(`$DARLING_SRC_DIR`/`$DARLING_OVERLAY`/`$DARLING_BUILD_DIR`); dump in
`$DARLING_BUILD_DIR/wl-body-wchan.txt`.

## Run matrix (×3, all reached the window)

| run | T1 first sight (baseline) | T2/T3 window |
|-----|---------------------------|--------------|
| 1 | guest 92499 visible, `92499 101182 RN - mldr` | absent: ps empty, procstat ESRCH, kill -0 "No such process" |
| 2 | guest visible, ps -H row present | absent, same shape |
| 3 | guest visible, ps -H row present | absent, same shape |

In all three the resolved pid is ps-visible with its thread rows at
first sight and absent from the ps/kvm view later in the run — while
the probe's log continues through `[step 12]`. That is the
process-scope door, not the stale-pid artifact of run 3.

## Run 1 verbatim (the differential)

```
=== T1 pre-park baseline (first sight) 19:44:45 guest=92499 logpid= logmark=[] ===
--- validation ps -p guest -o lstart,comm (must be non-empty) ---
  PID STARTED                  COMMAND
92499 Fri Oct  2 19:44:45 2026 mldr
--- ps -H rows for the guest (all its threads + wchan) ---
  PID    LWP STAT WCHAN    COMMAND
92499 101182 RN   -        mldr
--- procstat -kk per-guest ---
92499 101182 mldr   -   <running>
--- kernel existence: kill -0 ---
kill: 92499: Operation not permitted        (alive)
--- control: darlingserver threads (pid 92553) ---
92553 123143 SN+  select   darlingserver/darlingserver
92553 231496 SN+  uwait    darlingserver/darlingserver
92553 231497 SN+  uwait    darlingserver/darlingserver

=== T2 in the window (guest left ps) 19:44:46 guest=92499 logpid=92499 logmark=[LANE FINDING] ===
--- validation ps -p guest -o lstart,comm ---
PID STARTED COMMAND                       (empty)
--- ps -H rows for the guest ---
  PID    LWP STAT WCHAN    COMMAND          (empty)
--- procstat -kk per-guest ---
procstat: sysctl(kern.proc): No such process
--- kernel existence: kill -0 ---
kill: 92499: No such process

=== T3 confirm (stable) 19:44:50 guest=92499 logmark=[LANE FINDING] ===
(identical to T2)
```

Reading: at first sight the guest is ps-visible with one thread row
(wchan `-`) and `kill -0` returns EPERM (alive). Later in the same run
the pid yields no `ps -H` rows, `procstat -kk` returns ESRCH, and
`kill -0` returns ESRCH — the whole process has left the kernel's
observable process view, while the probe log runs on to `[step 12]`.
The control darlingserver shows normal waiters (`select`/`uwait`) at
first sight.

## Cross-check with kernel-wait (accepted)

kernel-wait (accepted earlier) resolved its pid by the argv pattern
`mldr-real/mldr` and saw a **main-thread row present** (`wchan -`) with
only the lane row absent — the *thread*-scope door of kernel-esrch.
This measurement, on a pid resolved by **parentage** and validated,
finds the *whole process* absent from `ps -H` — the process-scope door.
The two are different scopes: kernel-esrch = the process is findable,
its thread enumeration is not; here the process itself is not in the
kvm listing. The argv route can land on the wrong mldr (the accepted
topology note: it is ambiguous between the target and the
launchd-global), so kernel-wait's "main visible" cannot be assumed to
be the parked lane's process; the parentage-resolved pid measured here
is.

## Verdict (one line, per the order)

On a parentage-resolved, lstart-validated pid the parked lane's process
yields **no** `ps -H` row at the park (no wchan, no host frame) — it
leaves the kvm process view, while the pre-park baseline has its thread
rows; the rtld-lock holder is **not named** (markers need the gate,
which dies in the sigqueue storm before the window), and singularity is
not proven from this chassis.
