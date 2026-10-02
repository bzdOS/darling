# tty-window: is the park a guest block in ttydev_write (a pty artifact)?

Question: the accepted run-2 baseline kstack showed the guest in
`tty_wait ← ttydev_write ← sys_write`, and in the window `kill -0` was
sometimes EPERM (kernel-resident while ps-empty). Is the park a guest
block writing to a full/under-drained pty — an artifact of the observer
— or something else?

## Method

`sh build-freebsd/tty-watch.sh` (MODE=pty runs the probe's stdout on a
pty via ptyrun.py; MODE=file redirects to a file — the control).
`build-freebsd/tty-fast.sh` polls the guest every 0.05 s and snapshots
the privileged reads (`sudo procstat -kk <pid>`, `sudo fstat -p <pid>`,
`sudo ps -p <pid>`) on the first ps-empty. The fresh mldr is resolved by
parentage (fresh darlingserver → its parent), as in WCHAN-TOPO.md.

## Result 1 — the park reproduces WITHOUT the pty (control)

MODE=file (no pty) reaches the same markers as MODE=pty:

```
[step 01] ... [step 09] variant (a) ...
lane: DID-NOT-RETURN within 8000ms — parked inside wl_display_roundtrip
LANE FINDING reproduced: ...
```

So the park is not caused by the pty. A regular-file stdout does not
block on write, so the run-2 `ttydev_write` kstack is the guest's normal
output at baseline, not the park's foothold.

## Result 2 — the window is an exit, not a tty block

`tty-fast.sh`, guest 78856, full transition at 0.05 s resolution:

```
t=20:04:48.481 G=78856 ps=SN+  k0=[kill: 78856: Operation not permitted]
t=20:04:48.590 G=78856 ps=RN   k0=[kill: 78856: Operation not permitted]
t=20:04:48.813 G=78856 ps=RN   k0=[kill: 78856: Operation not permitted]
t=20:04:48.866 G=78856 ps=GONE k0=[kill: 78856: No such process]
```

While ps-visible the process is alive (`kill -0` = EPERM, root-owned);
at the departure it is absent from ps AND `kill -0` = ESRCH. The
"EPERM while ps-empty" seen once in the earlier run 3 was a transient
during this transition, not a stable state.

## Result 3 — not a sysctl permission filter

In the window the privileged reads agree with the unprivileged ones:
`sudo procstat -kk <pid>` = `sysctl(kern.proc): No such process`,
`sudo fstat -p <pid>` = the same, `sudo ps -p <pid>` = empty. So the
ESRCH is not a sysctl permission filter — the pid is genuinely absent
from `kern.proc` (measured with privileges).

## Result 4 — the pty reader

`ptyrun.py` is the pty master reader; it was present (`pgrep -f
ptyrun.py` showed it) while the guest ran. Its output goes to the log
file. No full-buffer stall was observed: the guest's own pty writes
completed (the log reached `[step 12]` and the process exited cleanly).

## Verdict (one line)

**Not tty**: the park reproduces without the pty (MODE=file reaches
DID-NOT-RETURN), and in the window the resolved pid is absent from
`kern.proc` even under sudo — the guest is not blocked in
`ttydev_write`; the run-2 `ttydev_write` kstack is normal baseline
output. The observed shape is the process leaving the kernel's
observable view (ps/procstat/fstat/kill -0), while the probe log runs on
to `[step 12]`.
