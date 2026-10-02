# wchan-kvm: what the kernel shows for the parked lane — the ESRCH door,
# reproduced in the window with an observer control

Order: kvm snapshots of the parked (a)-lane's threads (ps -H + procstat),
×3 distribution, an observer-side control, and a one-line verdict of what
the lane stands on at kernel level.

## Reproduction command (acceptance artifact)

The watcher: `build-freebsd/wchan-watch.sh` (committed). It starts the
probe (gate OFF — the run must reach the window), resolves the mldr pid,
and on the `[step 09] ... DID-NOT-RETURN` marker takes timestamped
snapshots: `ps -H -axo pid,tid,state,wchan,comm` for the parked process,
`procstat -kk <pid>` (per-pid), the same dumps on the darlingserver child
of the same session (control), and `procstat -a -kk` rows for the pid.

Run: `sh build-freebsd/wchan-watch.sh` (env-derived roots: the script
reads `DARLING_SRC_DIR`/`DARLING_OVERLAY`/`DARLING_BUILD_DIR` from the
environment, defaults relative to the tree) — output log
`$DARLING_BUILD_DIR/wl-body-wchan.txt` (per-run snapshot) and
`wl-body-wchan-watch.log` (the probe log with the park marker).

## Run matrix (5 runs, honest)

| run | outcome |
|-----|---------|
| 1 | no snapshot — watcher stuck in a nested `ps` spin (fixed: flat loop) |
| 2 | no snapshot — pid never resolved (fixed: `pgrep`, then log-pid, then comm) |
| 3 | **snapshot captured** (output below) |
| 4 | snapshot fired against a stale low pid (1474) — see persistence note |
| 5 | watcher loop exhausted early (sleep timing artifact) — run killed |

## Run 3 snapshot (verbatim)

```
=== park t0=16:44:14 pid=1474 ===
--- ps -H: all threads of the parked process ---
  PID    LWP STAT WCHAN    COMMAND
--- procstat -kk per-pid (ESRCH expected at the park) ---
procstat: sysctl(kern.proc): No such process
procstat: procstat_getprocs()
--- control: darlingserver child of the same session ---
darlingserver pid=99787
  PID    LWP STAT WCHAN    COMMAND
99787 102579 SN   select   darlingserver/darlingserver
99787 206526 SN   uwait    darlingserver/darlingserver
99787 206527 SN   uwait    darlingserver/darlingserver
=== park t1=16:44:16 ===
  PID    LWP STAT WCHAN    COMMAND
--- procstat -a -kk all-route rows for the parked pid ---
SNAPSHOT DONE
```

Reading: at the park, the parked process produces **no rows** from
`ps -H` and **ESRCH** from `procstat -kk` — the thread-side door of
kernel-esrch, reproduced live inside the window. The control on the
same session's darlingserver works perfectly (SN state, wchan
`select`/`uwait`) — so the emptiness is a property of the parked
process, not of the observer.

## The door is persistent, not transient

The resolved pid at the park was 1474 — a low pid. The same pid was
still resolving as a live `mldr-real/mldr` match at run 4 (16:49) and
gone by ~16:52: a process living **at least five minutes** in the
kern.proc-invisible state (procfs-visible to `pgrep -f`, invisible to
`ps`/`procstat`), consistent with the ESRCH door being a stable parked
state rather than a millisecond-wide race. (Its identity could not be
recovered post-mortem: `ps`/`procstat` cannot see it while it lives,
and it is gone now.)

## Observer-side corollary (why early runs missed)

Pattern-based pid resolution (`ps`/`pgrep -f` on the argv path) never
caught the FRESH mldr: the vchroot layer rewrites argv paths, so the
running process's cmdline does not contain the launcher's view of the
path (`Running: .../dserver/mldr-real/mldr ...`). Comm-based or
log-based pid sources are the reliable ones — the same path-rewrite
that made `/proc/self/maps` ENOENT to the guest.

## Verdict (one line, per the order)

На уровне ядра (a)-лейн в парке **не отдаётся**: ps -H пуст,
procstat -kk = ESRCH (дверь на стороне нити, та же, что названа в
kernel-esrch) — wchan и верхний host-кадр для самого лейна из этого
шасси недоступны; наблюдатель-контроль (darlingserver той же сессии)
показывает нормальные wchan (select/uwait), т.е. пустота — свойство
парка, не наблюдателя.

Держатель rtld-лока (tid + последний маркер) — не назван: маркеры
требуют гейт, а гейт-прогон умирает штормом до окна (survive-window);
одиночность из kvm-шасси тоже не доказуема, пока ядро не отдаёт ни
одной нити процесса. Обе ножки вердикта ждут либо гейт без шторма
(источник sigqueue(SIGBUS)), либо дверь ESRCH снята.
