# MLDR-CHECKIN-RECV-CAUSE — the kernel's condition for refusing

## Verdict (one line)

**Ядро отказывает, потому что за путём нет датаграммного endpoint'а в его
таблице привязок — хотя узел сокета существует, процесс жив и fd 3 не закрыт**:
все воспроизводимые хостовые условия отказа (`unlink` при живом fd → `ENOENT
2`, полная очередь → `ENOBUFS 55`, перехват пути → успех у нового владельца,
`shutdown(SHUT_RD)` → `55`) дают НЕ `61`; а в живом прогоне путь после `bind`
не `unlink`ится, не `rebind`ится и fd не закрывается — то есть таблица ядра
должна содержать endpoint, но `sendmsg` возвращает `ECONNREFUSED 61` каждому
отправителю. Условие отказа — расхождение таблицы привязок ядра с видимым
состоянием, причина которого вне userspace-наблюдаемости этого шага.

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
pre-bind unlink, the bind NAMI); `close(0x3)` by pid 66482 after the bind →
none (the three `close(0x3)` hits are `ld-elf` startup, lines 16449/16469/16480,
all before the socket exists). `listen` → 0, `connect` → 0 in the trace.

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
