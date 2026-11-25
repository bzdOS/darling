# MLDR-CHECKIN-RECV-CAUSE — the kernel's condition for refusing

## Verdict (one line)

**Источник -61 = ядро не находит датаграммный endpoint за путём, хотя узел
существует и привязка не снята**: оба отправителя (staging mldr 66490 и main
mldr 66499, свои `SOCK_DGRAM`-сокеты, `bind → 0`) получают `errno 61` на один
и тот же путь; в таблице `unpcb` (`netstat -f unix`, 109 строк) строки пути
нет; хостовые условия дают только 2/55. Причина отсутствия endpoint'а в
таблице вне userspace-наблюдаемости этого шага.

## 6. Senders' side (cause1 trace, dserver pid 66482, prefix Ok2GYV)

Only two senders to the path exist in the whole trace (`grep -c
'CALL  sendmsg'` → 2, `grep -c 'CALL  connect('` → 0): the staging mldr and
the main mldr, each with its own socket. Both fail identically:

```
26488 66490 mldr     CALL  sendmsg(0x9e159,0x820980670,0)
26489 66490 mldr     NAMI  "/tmp/darling-run.Ok2GYV/.darlingserver.sock"
26490 66490 mldr     RET   sendmsg -1 errno 61 Connection refused
31309 66499 mldr     CALL  sendmsg(0x9e159,0x821086240,0)
31310 66499 mldr     NAMI  "/tmp/darling-run.Ok2GYV/.darlingserver.sock"
31311 66499 mldr     RET   sendmsg -1 errno 61 Connection refused
```

Each sender's fd comes from the same shape (`__mldr_create_rpc_socket`:
`socket` + `dup2` to the bitmap fd + `bind` of its own pid-named path):

```
26398 66490 mldr     CALL  socket(PF_LOCAL,0x2<SOCK_DGRAM>,0)
26399 66490 mldr     RET   socket 4
26403 66490 mldr     CALL  dup2(0x4,0x9e159)
26404 66490 mldr     RET   dup2 647513/0x9e159
26413 66490 mldr     NAMI  "/tmp/darling-mldr-66490-647513"
26415 66490 mldr     CALL  bind(0x9e159,0x820980670,0x6a)
26416 66490 mldr     NAMI  "/tmp/darling-mldr-66490-647513"
26417 66490 mldr     RET   bind 0
31219 66499 mldr     CALL  socket(PF_LOCAL,0x2<SOCK_DGRAM>,0)
31220 66499 mldr     RET   socket 4
31236 66499 mldr     CALL  bind(0x9e159,0x821086240,0x6a)
31237 66499 mldr     NAMI  "/tmp/darling-mldr-66499-647513"
31238 66499 mldr     RET   bind 0
```

No other pid sends to or connects to the path. The senders are well-formed
bound `SOCK_DGRAM` sockets; the refusal is not sender-specific.

## 7. The unpcb table at refusal (unp4 run, own prefix IakboR)

One RAW run (`chrome-sr-unp4.log`, BAD SEND -61 x2 lines 28/65, same
binaries `mldr_build=2f59…`, `dserver_build=df9b…`); captured at the first
appearance of the run's own socket file (foreign `/tmp/darling-run.*`
prefixes excluded by before-list):

```
stat: st_ino=33388378 st_mode=0140755 st_uid=0 (file present at capture)
CTL root control at capture: mode=set sun_len=45 -> ret=-1 errno=61
netstat -f unix (109 lines, raw /tmp/mldr-bisect/unp4-netstat.txt): NO line carries the
  run's path (grep -c 'IakboR' -> 0); the only dgram-with-path rows are
  /var/run/logpriv and /var/run/log
```

Limits, recorded honestly: `procstat -f <dserver pid>` at capture returned
`No such process` (raw /tmp/unp4-procstat.txt) — the server exits within the
same second as the guest (`GUEST-EXIT t=0s`, `DSERVER-EXIT graceful`), so the
table snapshot may postdate the server's death; and dtrace fbt on the AF_UNIX
path is unavailable on this host:

```
dtrace -l -f unp_bind -> "No probe matches description"
fbt::unp_connect:entry -> "does not match any probes"
```

So (2) does not prove the table lacked the endpoint while the server lived —
it proves the endpoint leaves no trace once refused: neither the table nor
any rebind/unlink record explains where it went.

## 1. Runs and PROVENANCE

Host matrix (no VM, plain host binaries, sources `/tmp/dgtest-*.c`):

```
dgtest-pairs    base/bound-sender/otherdir send+recv OK; shutrd-sendto errno 55; recreate OK
                raw: /tmp/mldr-bisect/dgtest-pairs.log (22 lines)
dgtest-unlink   unlink-while-bound -> errno 2 (ENOENT), rebind OK
                raw: /tmp/mldr-bisect/dgtest-unlink.log (8 lines)
dgtest-full     flood tiny-rcvbuf: ok=3 enobufs=1997 econnrefused=0; cred-socket OK
                raw: /tmp/mldr-bisect/dgtest-full.log (4 lines)
dgtest-takeover unlink+rebind same path: send OK to new owner; inode path != A != B
                raw: /tmp/mldr-bisect/dgtest-takeover.log (7 lines)
```

Live runs (trunk tip `4fb65acb7`, same stand/binaries as step 6):

```
cause1 PROVENANCE git_head=4fb65acb7 mldr_build=2f5976f510f94a1731a49c24a9ce52e5
       log /tmp/mldr-bisect/chrome-sr-cause1.log (BAD SEND -61 x2, lines 28/65)
       trace /tmp/mldr-bisect/cause1.ktrace + /tmp/mldr-bisect/cause1.kdump.txt (31353 lines)
       live control during run: /tmp/mldr-bisect/cause1-stat.txt (errno 61 on live node)
cause2 same shape (log chrome-sr-cause2.log); fast-poll capture missed the <1s window,
       no procstat/sockstat — recorded as missed, not retried (trace already proves §3)
unp4   PROVENANCE git_head=f8482b165 mldr_build=2f5976f510f94a1731a49c24a9ce52e5
       log /tmp/mldr-bisect/chrome-sr-unp4.log (BAD SEND -61 x2, lines 28/65)
       live capture at own socket: /tmp/mldr-bisect/unp4-netstat.txt (109 lines,
       no path row), /tmp/mldr-bisect/unp4-stat.txt (ino=33388378, control
       errno 61), /tmp/mldr-bisect/unp4-debug.txt, /tmp/mldr-bisect/unp4-procstat.txt
```

## 2. Host matrix: what gives 61, what does not

```
pairs.log:    base-sendto ret=4 errno=0; bound-sender-sendto ret=4; otherdir-sendto ret=4
              shutrd-sendto ret=-1 errno=55 (No buffer space available)
              recreate-sendto ret=4, recreate-recv-new ret=4
unlink.log:   before-unlink sendto ret=4 ok
              after-unlink sendto ret=-1 errno=2 (No such file or directory)
              after-rebind sendto ret=4 ok
full.log:     flood ok=3 enobufs=1997 econnrefused=0 other=0
              credsrv-sendto ret=4 ok
takeover.log: B rebind ret=0; sendto-after-takeover ret=4 (goes to B, A gets EAGAIN)
              inodes path=33389401 A=4034463 B=4034464
              sendto-after-B-gone ret=-1 errno=2 [A alive, no node]
```

No host variation reproduces `errno 61` against a live node: missing node
gives 2, full queue gives 55, a rebound path delivers. The refusal in the
live runs happens with the node present (`srwxr-xr-x`, `stat` in
cause1-stat.txt) — a condition none of the host pairs reaches.

## 3. Live state: path and fd never diverge (cause1 trace)

dserver pid 66482, prefix `/tmp/darling-run.Ok2GYV`:

```
21600 66482 darlingserver CALL  socket(PF_LOCAL,0x30000002<SOCK_DGRAM|SOCK_CLOEXEC|SOCK_NONBLOCK>,0)
21601 66482 darlingserver RET   socket 3
21606 66482 darlingserver CALL  bind(0x3,0x8210a1885,0x6a)
21607 66482 darlingserver NAMI  "/tmp/darling-run.Ok2GYV/.darlingserver.sock"
21608 66482 darlingserver RET   bind 0
```

After that line: `bind` by pid 66482 appears exactly once in the whole trace
(`grep -c` → 1); no `unlink` of the path after the bind (all 71 `Ok2GYV`
references are other files or the three pre-bind lines: launch-dynamic stat,
pre-bind unlink, the bind NAMI); `close(0x3)` by pid 66482 before the bind →
24 (ld-elf/libmap startup, lines 16449…21554 — correction to the earlier
"three": the count was taken on a truncated window); after the bind (lines
>21608) → 0 (`grep` + `awk` both 0). So no close/unlink after bind stands.
`listen` → 0, `connect` → 0 in the trace.

So the vnode behind the path IS the node fd 3 holds: one bind created it, no
unlink removed it, no close dropped it, no rebind replaced it. The kernel
lookup still finds no endpoint — that is the established contradiction, and
no userspace-visible state explains it.

## 4. The "working" combination has no live state to compare

`/tmp/ck61-test/chrome-sr-ck611.log` (pin bb857 + df9b743a): `grep -c checkin`
→ 0. The pin dies on `Failed to allocate stack of 65536 bytes: 3` BEFORE any
checkin (lines 25/59), so its "OK" is the absence of the event, not a working
checkin (already established in the bin-diff note, confirmed here on the raw
log). There is no working pair whose live socket state could be diffed
against the refusing one — measurement (3) of the dispatch is void for lack
of a subject, not skipped.

## 5. What is established, what is not

Established:

- `errno 61` is not produced by any userspace-reachable socket condition
  tested: not missing node (2), not full queue (55), not shutdown (55), not
  takeover (delivers), not creds (message layer, post-delivery).
- In the live run the path↔fd binding is intact by trace (single bind, no
  unlink, no close), yet the kernel refuses every sender.
- There is no working live state to compare: the pin never reaches checkin.

Not measured: the kernel's own reason (fbt/dtrace on the AF_UNIX path) —
kernel work, outside this dispatch.

No fix was made, by the order of the dispatch.
