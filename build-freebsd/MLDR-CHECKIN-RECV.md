# MLDR-CHECKIN-RECV — the receiver side of -61

## Verdict (one line)

**Источник -61 = ядро не находит датаграммный endpoint за путём
`.darlingserver.sock`, хотя слушающий сокет `darlingserver` по этому пути
привязан и жив**: `bind(fd 3) → 0`, закрытий fd 3 после bind нет, `listen()` в
трассе нет вообще (0 вызовов), `connect()` нет (0 вызовов, обе стороны —
`SOCK_DGRAM` connectionless), гость получает `sendmsg → -1 errno 61` в обоих
прогонах, и образцовый хостовый клиент (root, правильный `sun_len`) получает
тот же `errno 61` на живом узле сокета.

## 1. Code: what kind of socket it is (no listen by construction)

`src/external/darlingserver/src/server.cpp:616` (tip `04bb15e03`):

```
_listenerSocket = socket(AF_UNIX, SOCK_DGRAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
```

`bind` right below, `server.cpp:664`:

```
if (bind(_listenerSocket, (struct sockaddr*)&addr, sizeof(addr)) != 0) {
```

No `listen`, no `accept` anywhere in the daemon: `grep -rn` for
`listen(`/`accept(` over `src/external/darlingserver/src`,
`internal-include`, `tools` → no hits. Receiving is `recvmmsg` on the bound
datagram socket (`message.cpp:723`, `receiveMany`). A `SOCK_DGRAM` listener
never calls `listen` — its absence is not the defect; it only closes the "the
server forgot to listen" hypothesis, which the trace below confirms is not
how this Unix-domain datagram path works.

The guest side is `SOCK_DGRAM` too: `src/startup/mldr/mldr.c:869`
(`socket(AF_UNIX, SOCK_DGRAM, 0)`), sends via `sendmsg` with `msg_name` set to
the server path (`src/startup/mldr/resources/dserver-rpc-defs.h:69-75`),
no `connect` anywhere in the trace (see §3).

## 2. Runs and PROVENANCE

Two fresh RAW runs, same stand `/tmp/mldr-bisect`, same binaries, one probe
each; both repeat -61 twice (staging + main phase):

```
recv1 PROVENANCE git_head=04bb15e03b4703dd8e09ea71d70b49954b89e804
      PROVENANCE mldr_build=2f5976f510f94a1731a49c24a9ce52e5 darlingserver_build=df9b743a8c3d0a9d31906b2292904f41
      log /tmp/mldr-bisect/chrome-sr-recv1.log (70 lines)
      trace /tmp/mldr-bisect/recv1.ktrace (root-owned, use sudo kdump), decoded /tmp/mldr-bisect/recv1.kdump.txt (31348 lines, mode 644)
recv2 PROVENANCE git_head=04bb15e03b4703dd8e09ea71d70b49954b89e804
      PROVENANCE mldr_build=2f5976f510f94a1731a49c24a9ce52e5 darlingserver_build=df9b743a8c3d0a9d31906b2292904f41
      log /tmp/mldr-bisect/chrome-sr-recv2.log (70 lines)
      trace /tmp/mldr-bisect/recv2.ktrace + /tmp/mldr-bisect/recv2.kdump.txt (31349 lines, mode 644)
      live control during this run (socket still present), see §4
```

Run recipe: `chrome-wl-run.sh` direct (the batch wrapper refuses at disk
use 96% ≥ 85% stop-guard), env `DARLING_BUILD_DIR=/tmp/mldr-bisect`,
`DARLING_KTRACE_POINTS=cn`, log excerpts below from recv2:

```
chrome-sr-recv2.log:28 *** 94771:1022209: dserver_rpc_explicit_checkin: BAD SEND STATUS: -61 ***
chrome-sr-recv2.log:61 darlingserver socket ready: /tmp/darling-run.H2yIxV/.darlingserver.sock
chrome-sr-recv2.log:65 *** 94774:1022209: dserver_rpc_explicit_checkin: BAD SEND STATUS: -61 ***
```

recv1 is identical in shape (recv1.log:28/61/65; its run prefix was
`/tmp/darling-run.AMqrTQ`).

## 3. The trace (recv2; recv1 matches line for line modulo pids/prefix)

Listener setup, one bind only:

```
21586 94753 darlingserver CALL  socket(PF_LOCAL,0x30000002<SOCK_DGRAM|SOCK_CLOEXEC|SOCK_NONBLOCK>,0)
21598 94753 darlingserver CALL  bind(0x3,0x82102a6d5,0x6a)
21599 94753 darlingserver NAMI  "/tmp/darling-run.H2yIxV/.darlingserver.sock"
21607 94753 darlingserver RET   bind 0
```

No `listen` in the whole trace (`grep -c 'CALL  listen('` → 0), no `connect`
(`grep -c 'CALL  connect('` → 0). No close of fd 3 after the bind
(`awk 'NR>21607' … close(0x3'` → 0; the only dserver close past that line is
fd 4). The listener stays alive (blocked in `read … RESTART` / `_umtx_op …
EINTR`, same as step 5).

Guest side (both phases; quoting the staging one):

```
26394 94771 mldr     CALL  socket(PF_LOCAL,0x2<SOCK_DGRAM>,0)
26409 94771 mldr     NAMI  "/tmp/darling-mldr-94771-647513"
26411 94771 mldr     CALL  bind(0x9e159,0x820707970,0x6a)
26413 94771 mldr     RET   bind 0
26484 94771 mldr     CALL  sendmsg(0x9e159,0x820707970,0)
26485 94771 mldr     NAMI  "/tmp/darling-run.H2yIxV/.darlingserver.sock"
26486 94771 mldr     RET   sendmsg -1 errno 61 Connection refused
```

Second phase: same at `31305/31307` (pid 94774). The death is exactly at the
first `sendmsg`; nothing of the guest's is reached after it.

## 4. Live control: a textbook host sender fails identically

During recv2, while `/tmp/darling-run.H2yIxV/.darlingserver.sock` was present
(`socket ready` logged, guest still running), as root, via
`/tmp/mldr-bisect/ctl-send` (AF_UNIX/SOCK_DGRAM, `sendto`, modes `set` =
`sun_len=2+plen` and `zero` = `sun_len=0`, both `namelen=106`):

```
CTL path=/tmp/darling-run.H2yIxV/.darlingserver.sock mode=set  sun_len=45 namelen=106 -> ret=-1 errno=61 (Connection refused)
CTL path=/tmp/darling-run.H2yIxV/.darlingserver.sock mode=zero sun_len=0  namelen=106 -> ret=-1 errno=61 (Connection refused)
srwxr-xr-x  1 root wheel 0 … /tmp/darling-run.H2yIxV/.darlingserver.sock
```

Both `sun_len` modes fail identically on the live node, so the sender's
bytes are not the discriminator (same result as step 5, now on the fresh
trunk tip with the fresh trace).

## 5. What is established, what is not

Established:

- The listener is a bound, alive `SOCK_DGRAM` socket (bind → 0, no
  post-bind close, process blocked waiting) — there is no missing
  `listen()`; a datagram listener does not listen.
- The failure is decided by the kernel at `sendmsg` time for every sender:
  guest (both phases, 2/2 runs) and root host control alike, same errno 61.
- The question of step 6 ("why is there no endpoint behind the socket file")
  is narrowed to: a bound datagram socket whose node exists, whose owner is
  alive, yet `sendmsg` to its path returns `ECONNREFUSED` — i.e. the
  `unp_connect`/delivery lookup finds no destination, not anything about the
  sender's address.

Not measured: the kernel's own reason for refusing the lookup
(fbt/dtrace on the AF_UNIX path) — kernel work, outside this dispatch.

No fix was made, by the order of the dispatch.
