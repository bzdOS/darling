# MLDR-CHECKIN-CONTROL — the sockaddr is not it; the receiver side is

## Verdict (one line)

**Причина -61 не в sockaddr отправителя**: снабжённая байтами `sun_len=0` и
починенная до `sun_len=106` копия даёт один и тот же `BAD SEND STATUS: -61`,
и такой же `errno 61` получает образцовый хостовый клиент (root, правильный
`sun_len`, живой файл сокета) — то есть сокет `darlingserver` по своему пути не
принимает датаграмму, хотя сам процесс жив, `bind` прошёл и fd закрыт не был.

## 1. Runs and PROVENANCE

All three runs: same stand, same dserver, one fresh RAW run each.

```
ctl1 (ДО правки) PROVENANCE git_head=a55a9b38adf05407e8c73045b8f2f9fa9496ac55
                 PROVENANCE mldr_build=33f7ed073f9c8c520146a915f7219ffd darlingserver_build=df9b743a8c3d0a9d31906b2292904f41
                 (838744 B, instrumentation only)          log chrome-sr-ctl1.log
ctl2 (ПОСЛЕ)     PROVENANCE git_head=a55a9b38adf05407e8c73045b8f2f9fa9496ac55
                 PROVENANCE mldr_build=2f5976f510f94a1731a49c24a9ce52e5 darlingserver_build=df9b743a8c3d0a9d31906b2292904f41
                 (838752 B, instrumentation + sun_len)     log chrome-sr-ctl2.log
ctl3 (ПОСЛЕ, повтор + контроль)  mldr_build=2f5976f510f94a1731a49c24a9ce52e5
                 log chrome-sr-ctl3.log, ktrace 31347 lines
```

Every run prints `BAD SEND STATUS: -61` twice (staging and main phase), e.g.
`ctl3`: lines 28 and 65 — the `sun_len` change did not move the number.

## 2. Bytes of msg_name, ДО and ПОСЛЕ

Instrumentation was a 12-line hexdump at the top of
`dserver_rpc_hooks_send_message` (removed again after measuring — see §6).

```
ДО  (ctl1) [send-probe] namelen=106 sun_len=0 sun_family=1
           bytes=00012f746d702f6461726c696e672d72756e2e3171567277
           path=/tmp/darling-run.1qVrwP/.darlingserver.sock
ПОСЛЕ (ctl3) [send-probe] namelen=106 sun_len=106 sun_family=1
           bytes=6a012f746d702f6461726c696e672d72756e2e35416a5751
           path=/tmp/darling-run.5AjWQv/.darlingserver.sock
```

`00 01` = length byte 0, family 1; `6a 01` = length 106, family 1. The field
lives in `src/startup/mldr/mldr.c`: the static initializer sets only
`.sun_family`/`.sun_path`, and nothing filled `sun_len` — while the working
client bind in the same file does `sa_client.sun_len = sizeof(sa_client)`.
The change applied to reach the ПОСЛЕ bytes was exactly

```c
		__dserver_socket_address_data.sun_len = sizeof(__dserver_socket_address_data);
```

measured effect: dump goes `0 → 106`, and `-61` **stays** (ctl2/ctl3, 2 hits).

## 3. Control: a textbook host sender fails the same way

Against the live socket of `ctl3`, as root, with a correctly built
`sockaddr_un` (`sun_len = offsetof+strlen`, `msg_namelen = 106`):

```
CTL path=/tmp/darling-run.5AjWQv/.darlingserver.sock mode=set   sun_len=45  -> ret=-1 errno=61 (Connection refused)
CTL path=/tmp/darling-run.5AjWQv/.darlingserver.sock mode=zero  sun_len=0   -> ret=-1 errno=61 (Connection refused)
the file itself: srwxr-xr-x 1 root wheel 0 … .darlingserver.sock   (a socket node)
```

Both `sun_len` modes fail identically, so `sun_len` is not the discriminator.
(An earlier control as the unprivileged user returned `errno 13` against a
stale socket — permissions and staleness, set aside as not this failure.)

## 4. Receiver side, from the same trace

```
21600  9704 darlingserver CALL  bind(0x3,0x8206d71a5,0x6a)
21601  9704 darlingserver NAMI  "/tmp/darling-run.5AjWQv/.darlingserver.sock"
21602  9704 darlingserver RET   bind 0
... between that bind and the guest's send: 0 closes of fd 3 (awk → 0) ...
26482  9716 mldr CALL  sendmsg(0x9e159,0x82085d890,0)
26484  9716 mldr RET   sendmsg -1 errno 61 Connection refused
last records of pid 9704 before the send: read … RET RESTART, _umtx_op … EINTR
```

`bind(0x3` total in the trace: 1. pid 9704 stays alive to the end of the trace
(3414 records, last one `_umtx_op … EINTR`), its fd 3 never closed after the
bind — and still both the guest and the control client get `ECONNREFUSED`, i.e.
the kernel finds no datagram endpoint behind that path.

## 5. What is established, what is not

Established:
- the sender's bytes are not the cause (ДО/ПОСЛЕ identical outcome);
- permissions are not the cause (root client, world-writable socket node);
- `darlingserver` is alive and bound at the moment of the send;
- the failure is on the receiver side of that path.

Not measured: why the kernel finds no endpoint behind a file that a successful
`bind` just created (fbt on `unp_connect`/`unp_output` would be next) — no fix
was committed, by the order of the dispatch ("либо … цитатное доказательство
причины без фикса").

## 6. Why the branch carries no code

Both edits made for the measurement — the 12-line hexdump in
`src/startup/mldr/resources/dserver-rpc-defs.h` and the `sun_len` assignment in
`src/startup/mldr/mldr.c` — were reverted after the measurement: the first is
scaffolding, and the second was measured **not** to remove `-61`, so shipping
it as a fix would claim something this run disproved. Their exact content and
measured effect are quoted in §2.
