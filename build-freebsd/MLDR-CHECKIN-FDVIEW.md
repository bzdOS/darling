# MLDR checkin -61: provenance of the pinned dserver + fd view of the live window (step 11)

Date: 2026-11-25 22:15-22:55 UTC. Runner: the project head, bench `/tmp/mldr-bisect`
(failing config: mldr 2f5976f5 + dserver df9b743a; guest dies on
`dserver_rpc_explicit_checkin: BAD SEND STATUS: -61` before the guest start).

PROVENANCE `git_head=a0a5bbaa03880c9bf1ae3113967216b56f84e4ae` (branch with the
fd-view watcher), same pair as the failing series:
`mldr_installed=1ff2ed031367d6ee869a45402c99dba5 darlingserver_installed=df9b743a8c3d0a9d31906b2292904f41`,
`mldr_build=2f5976f510f94a1731a49c24a9ce52e5 darlingserver_build=df9b743a8c3d0a9d31906b2292904f41`.

## 1. Provenance of the pinned dserver — CLOSED: the pin is the tree's own build

- `md5 /tmp/ck61-test/dserver/darlingserver` = `df9b743a8c3d0a9d31906b2292904f41` (the pin).
- `md5 /opt/darling/build/dserver/dserver-build/darlingserver` = the same (build dir, mtime Nov 25 10:42).
- `md5 /usr/local/libexec/darling/usr/libexec/darling/darlingserver` = the same (installed libexec).
- `git log --since="2026-11-25 08:43" -- src/external/darlingserver` is EMPTY: no commit
  touched the dserver source after the build was configured — the pin matches the current tree.

Verdict: pin = build = install = current tree source. The dserver binary is NOT the variable
between the working bench pair (mldr bb857) and the failing one (mldr 2f5976f5/1ff2ed03).

## 2. fd view of the live window (run fdv1, watcher v5 `mldr-unp-fdwatch.sh`)

Run `chrome-sr-fdv1.log`: BAD SEND -61, GUEST-EXIT 0x100/1s. Watcher `fdv1-watch.txt`:
1013 samples, live=1 — the watcher found the run's own DSERVER pid (67101, from the
`[launch] DSERVER pid=` log line) only at sample 81 (~3 s in), and the server was alive for
exactly one sample:

```
W 81 t=1795646228 pid=67101 alive=67101 file=? rows=0 mine=0
FD 81 ... pid=67101 nfd=15
  3 v d r------- /usr/local/libexec/darling        <- fd 3 is a DIRECTORY vnode
  4 p / 5 p                                        <- lifetime pipes
  6..8 v d /usr/local/libexec/darling{,/usr,/usr/libexec,/usr/libexec/darling}
  9 v r text /usr/local/libexec/darling/.../darlingserver (mmap'd)
  10 v r -w 2097152
SOCK 81 ... (sockstat -u rows for the pid: NONE)
W 82 t=1795646228 pid=67101 alive=no file=y rows=0 mine=0   <- server dead, socket file alive
```

At its only live sample the server holds NO socket fd at all (fd 3 is a directory), and
`sockstat -u` shows no unix socket for the pid. The socket file `/tmp/darling-run.qGa9ey/.darlingserver.sock`
survives the server's death (56 file=y samples). Caveat: one sample, and the log-pid
discovery lag ate the first ~2 s of the server's life — the fd view covers the state at
death, not the bind moment.

## 3. The existing ktrace settles the bind side (cause1, DSERVER pid=66482)

- `66463 launch-dynamic NAMI "<prefix>/.darlingserver.sock"` (line 21564) — launcher touches the path;
- `66482 darlingserver NAMI ...` (21574) — the server's own unlink of a stale path;
- `66482 darlingserver CALL socket(PF_LOCAL, SOCK_DGRAM|CLOEXEC|NONBLOCK)` + `bind(0x3, ..., 0x6a)`
  → NAMI path → **RET bind 0** (lines 21606-21608): the listener IS bound, len 106;
- every `close(0x3)` of the server sits at lines 16449-21554 — ALL BEFORE the bind: the server
  never closes its bound fd 3 in this trace;
- the guest binds its OWN endpoint (`66490 mldr NAMI "/tmp/darling-mldr-66490-647513"`,
  bind -> 0), never the server path; its lookup of the server path (NAMI 26489) is the
  connect that returns ECONNREFUSED (the run's `CTL ... -> ret=-1 errno=61`).

## 4. Kernel visibility — the probe that invalidates the earlier "rows=0" readings

Host probe (this measurement, 15 s):

```sh
python3 -c "import socket; s=socket.socket(socket.AF_UNIX,socket.SOCK_DGRAM); s.bind('/tmp/vtest.sock')"
netstat -f unix | grep -c vtest.sock   -> 0      # bound, unconnected dgram socket: NOT in the table view
# after the process exits (socket closed):
test -S /tmp/vtest.sock                -> true   # vnode SURVIVES the close/process death
```

On this kernel (FreeBSD 15.1-RC2): `netstat -f unix` does NOT list bound-unconnected dgram
sockets, and a bound socket's vnode outlives its process. The `/var/run/log` rows that
steps 7-9 used as the visibility control are connected sockets, not listeners — the control
did not control this case.

## Verdict (step 11)

1. The pinned dserver is the tree's own binary — provenance is not the variable.
2. The "post-bind, server alive, endpoint absent from the table" readings of steps 7-9 were
   an ARTIFACT: `rows=0` is the normal table view for a bound dgram listener on this kernel,
   and `file=y` after death is the normal vnode lifetime. Neither ever measured the endpoint.
3. Measured hard facts: the server binds its listener (ktrace, bind -> 0); at fdv1's death
   sample the server holds no socket fd; the guest's connect to the path gets ECONNREFUSED
   while the vnode exists. The contradiction "server holds bound fd 3 per trace / connect
   finds no socket" is now the only open question — the vnode-to-unpcb pairing at the
   guest's connect moment.

## Next step (step 12)

1. Visibility first: which tool SEES a bound-unconnected dgram listener on this kernel —
   `sockstat -u` vs `netstat -Anf unix` (the vtest probe again, fresh path, both tools while bound).
2. Re-run the watched probe with the visible tool in the watcher (real table rows during the
   window) and the DSERVER pid prefilled (kill the log-discovery lag): one run to learn the
   pid shape, one watched run.
3. From `cause1.kdump.txt`: the CALL lines directly above NAMIs 21564/21574 (what operation
   the launcher and the server performed on the path before the bind) and the server's
   `getsockname(3)` / fstat(3) results after the bind — does the kernel agree the bound
   socket carries that vnode.
