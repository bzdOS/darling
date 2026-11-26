# MLDR checkin -61: the listener dies — dserver SIGSEGVs between bind and the guest checkin (step 12)

Date: 2026-11-26 00:10-00:40 UTC. Runner: the project head, bench `/tmp/mldr-bisect`,
failing pair unchanged (loader build `2f5976f510f94a1731a49c24a9ce52e5` /
installed `1ff2ed031367d6ee869a45402c99dba5`, server `df9b743a8c3d0a9d31906b2292904f41`),
tree at `58f352ad5`. Reproduction: `BAD SEND STATUS: -61` x2 in every run
(st12a, st12b, st12c, st12d); wrapper ends `WRAPPER_RC=1`.

PROVENANCE of the new runs: `git_head=58f352ad58ed8afbef4ec84b799d79cf34919b3a`,
same overlay md5s as the fd-view step (`appkitextras_staged=450ece77...`,
`appkit_overlay=eb1b622f...`, `wayland_backend_overlay=f34bf90f...`).

## 1. Table visibility of a bound-unconnected dgram listener (corrects the fd-view note §4)

Probe: a python process binds `AF_UNIX`/`SOCK_DGRAM` at a fresh path and stays
alive while the tools run:

```
sockstat -u            -> "user cmd 32821 3 dgram /tmp/vtest-step12.soc ??"   (SEEN)
netstat -f unix        -> 1 row for the path                                  (SEEN)
netstat -Anf unix      -> 1 row with pcb + inode + path                       (SEEN)
procstat -f <pid>      -> "3 s - rw------ 1 0 UDD /tmp/vtest-step12.sock"     (SEEN)
```

The fd-view note §4 ("netstat does NOT list bound-unconnected dgram") was a probe
artifact: its netstat ran after the binder had exited, so the socket was gone —
the 0 measured the process's absence, not kernel blindness. All four tools see
a live bound-unconnected dgram listener on this kernel.

Sendto semantics measured on the same kernel (three states, one script):

```
live bound listener                 -> sendto delivered, ret 0
vnode without socket (binder closed,
the socket file still there)        -> sendto ret -1 errno 61 ECONNREFUSED
no path                             -> sendto ret -1 errno 2 ENOENT
```

So `-61` means exactly: *the vnode at the path exists, no live socket behind it*.

## 2. The watched window (fdwatch6: pid via pgrep from t=0, 20 ms cadence)

`build-freebsd/mldr-unp-fdwatch6.sh` (this note) removes the v5 log-discovery
lag (~2 s) and samples fd types, per-pid sockstat rows, path rows and the
newest socket file every 20 ms, run st12a:

- the server process is caught alive in a **single** 20-ms sample: `fd3=none`
  (the instant between its pre-bind `close(3)` and `socket()`);
- `sockstat -u` rows for the pid: 0 in every sample; `netstat -Anf unix` rows
  for `*.darlingserver.sock`: 0 in every sample of the whole 35-s window;
- the wrapper prints `DSERVER-EXIT graceful` only at teardown bookkeeping —
  the process is long gone by then (the run's own socket file is deleted by
  the wrapper's run-dir cleanup, so the `file=y` samples during the window
  matched older leaked files).

Re-read of the fd-view run under the same light: its RULE block shows
`credential:`, `pwdx:`, `ps flags:` EMPTY while the same sample's `procstat -f`
returned 15 rows — the sampler caught the death instant, not a blind table.

## 3. The traced run (st12d: `ktrace -i -t cnis` from the root side, 1.44M dump lines)

Chronology, dump line numbers (`/tmp/mldr-bisect/st12d.kdump.txt`):

```
548446  server  socket(PF_LOCAL, SOCK_DGRAM|CLOEXEC|NONBLOCK)
548447  server  setsockopt(IP_TOS)          -> 0
548449  server  setsockopt(SO_RCVBUF)       -> 0
548450  server  bind(3, len=106)
548452  server  RET bind 0                  (vnode NAMI created at the path)
54845x  server  socketpair; mkdir private/var/log; openat dserver.log O_APPEND -> fd 4
5485xx  server  cpuset_getaffinity -> 0
548577  guest   execve of the loader (JUSTRETURN)   [trailing the pre-bind fork]
548581  server  PSIG SIGSEGV SIG_DFL code=SEGV_MAPERR     <- THE SERVER DIES
548741  server  last residual thread RET (_umtx_op EINTR)
553453  guest1  sendmsg -> NAMI finds the vnode -> RET -1 errno 61 ECONNREFUSED
558765  guest2  sendmsg -> NAMI finds the vnode -> RET -1 errno 61 ECONNREFUSED
```

The server holds no `close(3)` after the bind and no `exit()` — it dies on the
default SIGSEGV disposition, which a syscall-only trace shows as silence. The
fd-view step's trace (`cause1`) has **PSIG count = 0**: its signal trace point
was off, so its reading "the server stays alive, blocked in read, at send time"
was an inference from a trace that could not see the death. That closes the
"vnode-to-unpcb pairing" open question of the fd-view note: there is no pairing
anomaly — **the listener process is dead before the guest's checkin send**, and
the send hits the measured vnode-without-socket state (= 61).

## Verdict (step 12)

The `-61` is not a kernel-visibility or send-side defect at all: the pinned
server binary SIGSEGVs in its own startup ~130 syscall-lines after `bind -> 0`
(context: right after opening its log and `cpuset_getaffinity`, at/around
entering its event loop; no guest RPC happened yet), the leaked vnode returns
ECONNREFUSED by measured sendto semantics. Whether a run "works" is decided by
whether the guest's checkin lands in the ms-window before the crash.

Leading hypothesis for the trigger: the crash is coupled to the guest loader's
exec (trace-adjacent to the PSIG), and the loader image is the variable that
tracks the failure — the working pair's loader is 518k vs the failing one's
822k (same server binary in both). HYPO, not proven: the working pair's runs
were never traced with the signal point, and the bench's saved dump of the
working-pair checkin run is empty.

## Next step (step 13)

One traced run with the WORKING pair (the pinned loader as the guest loader,
same bench, `ktrace -i -t cnis` from root): does the server PSIG-crash at all
when the old loader execs? Plus a run with the run dir preserved
(`DARLING_KEEP_LOCAL=1` keeps the staging tree) to read the server's own
`dserver.log` (fd 4 in the trace) around the crash. Verdict of the step: the
crash site named (symbol/function via the server binary's symbol table), the
mldr-dependence confirmed or refuted, fix branch dispatched after.
