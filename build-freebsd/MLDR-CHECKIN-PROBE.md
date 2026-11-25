# MLDR-CHECKIN-PROBE — where -61 actually comes from, from a live run

## Verdict (one line)

**Источник -61 = `sendmsg()` в госте (mldr):** ядро вернуло `-1 / errno 61
Connection refused` на путь `/tmp/darling-run.<run>/.darlingserver.sock`, и это
при том, что слушатель `darlingserver` по этому пути **уже был привязан** — его
`bind → 0` идёт в трассе раньше отправки гостя.

## 1. Runs (nothing rebuilt)

Both runs are the vintage mldr on the intact stand, one probe each time; the
second one carries the socket-path trace.

```
probe1  PROVENANCE git_head=f6a33fb71bd73351a9fe654b94e6d1a7f5daab01
        PROVENANCE mldr_build=979075c6351ece4c71126926798a7f06 darlingserver_build=df9b743a8c3d0a9d31906b2292904f41
        (md5 979075c6351ece4c71126926798a7f06, 510640 B)
        trace NOT taken: DARLING_KTRACE was passed as a script argument and the
        script reads it from its own environment, so no file was created —
        measured: /tmp/mldr-bisect/checkin.ktrace: No such file or directory
        log $DARLING_BUILD_DIR/chrome-sr-probe1.log
          57: darlingserver socket ready: /tmp/darling-run.IsFzzp/.darlingserver.sock
          59: *** 24592:314788: dserver_rpc_explicit_checkin: BAD SEND STATUS: -61 ***
          61: *** 24591:773372: dserver_rpc_explicit_checkin: BAD SEND STATUS: -61 ***

probe2  PROVENANCE git_head=f6a33fb71bd73351a9fe654b94e6d1a7f5daab01
        PROVENANCE mldr_build=979075c6351ece4c71126926798a7f06 darlingserver_build=df9b743a8c3d0a9d31906b2292904f41
        (md5 979075c6351ece4c71126926798a7f06, 510640 B)
        trace OK: ktrace -i -t cn (c = system calls with results, n = namei),
        file $DARLING_BUILD_DIR/checkin.ktrace, 2091764 B, root 0600 — read back
        with the same passwordless sudo the run harness itself uses
        log $DARLING_BUILD_DIR/chrome-sr-probe2.log
          27: *** 29528:314733: dserver_rpc_explicit_checkin: BAD SEND STATUS: -61 ***
          60: darlingserver socket ready: /tmp/darling-run.Qq0Raq/.darlingserver.sock
          62: *** 29534:216133: dserver_rpc_explicit_checkin: BAD SEND STATUS: -61 ***
```

## 2. The socket path the vintage binds for checkin

```
29528 mldr   CALL  unlink(0x21eb80)
29528 mldr   NAMI  "/tmp/darling-mldr-29528"
29528 mldr   RET   unlink -1 errno 2 No such file or directory
29528 mldr   CALL  bind(0x9e159,0x820bfce60,0x6a)
29528 mldr   NAMI  "/tmp/darling-mldr-29528"
29528 mldr   RET   bind 0
```

its own socket, pid-named, `sun_len = 0x6a` (106 = `sizeof(struct sockaddr_un)`),
type `PF_LOCAL / SOCK_DGRAM` (`socket(PF_LOCAL,0x2<SOCK_DGRAM>,0)` at trace line
22036). The **destination** of the checkin is dserver's socket:

```
29519 darlingserver CALL  socket(PF_LOCAL,0x30000002<SOCK_DGRAM|SOCK_CLOEXEC|SOCK_NONBLOCK>,0)
29519 darlingserver RET   socket 3
29519 darlingserver CALL  setsockopt(0x3,IPPROTO_IP,IP_TOS,0x8212e645c,0x4)
29519 darlingserver RET   setsockopt 0
29519 darlingserver CALL  setsockopt(0x3,SOL_SOCKET,SO_RCVBUF,0x8212e643c,0x4)
29519 darlingserver RET   setsockopt 0
29519 darlingserver CALL  bind(0x3,0x8212e6295,0x6a)
29519 darlingserver NAMI  "/tmp/darling-run.Qq0Raq/.darlingserver.sock"
29519 darlingserver RET   bind 0
```

## 3. What the sender returned

There is **no `connect` anywhere in the trace** (`grep -c 'CALL  connect('` → 0):
the checkin is a connectionless datagram addressed by `msg_name`. Exactly two
sends exist in the whole trace, both failing identically:

```
22072 29528 mldr CALL  sendmsg(0x9e159,0x820bfcde0,0)
22073 29528 mldr NAMI  "/tmp/darling-run.Qq0Raq/.darlingserver.sock"
22074 29528 mldr RET   sendmsg -1 errno 61 Connection refused
...
22482 29534 mldr CALL  sendmsg(0x9e159,0x8209fe7c0,0)
22483 29534 mldr NAMI  "/tmp/darling-run.Qq0Raq/.darlingserver.sock"
22484 29534 mldr RET   sendmsg -1 errno 61 Connection refused
```

On this host `61 = ECONNREFUSED` (`kdump`'s own string above), and the guest
prints the same number through its hook, which is `sendmsg` + `-errno`:

```
dserver_rpc_hooks_send_message(int socket, const ...msghdr_t* message) {
    ssize_t ret = sendmsg(socket, message, 0);
    if (ret < 0) return -errno;
    return ret;
}
```

so `long_status = -61`, `dserver_rpc_explicit_checkin` prints
`BAD SEND STATUS: -61` and returns it — that is the whole arithmetic of the
number.

## 4. dserver at that moment

- The listener's `bind` at line 21600 **succeeds (`RET 0`)** before the guest's
  send at line 22072 — same path, one listener only (`grep 'CALL  bind('` → 3
  records total: dserver's and the two guests' own pid-named sockets).
- The path is never unlinked after that bind (the only `unlink` of it is the
  pre-bind one at line 21591, `errno 2`), and pid 29519 never closes fd 3
  (`grep 'CALL  close(0x3'` → records only for `timeout` and `launch-dynamic`).
- pid 29519 stays alive: its last records before the send are its own log file
  opening `…/private/var/log/dserver.log` → `RET openat 4` and then
  `read … RET RESTART`, i.e. it is blocked waiting, not gone.
- Its own lines do **not** reach the run log: `DARLING_RPC_LOG=1`,
  `DSERVER_LOG_STDERR=1` and `CHROME_SR_SERVER_LOG=debug` produced nothing —
  measured `grep -nE '\(calls|received|send attempt' <log>` → empty in both
  runs; dserver writes that category to `<prefix>/private/var/log/dserver.log`,
  and the prefix is removed when the run ends (measured: the prefix of probe1
  is gone, `ls` → nothing).

## 5. Not measured

- **Why the kernel refuses a datagram whose target is bound and alive**: the
  guest side, the syscall result and dserver's state are all measured above,
  but the kernel's decision itself was not traced (fbt/dtrace on the AF_UNIX
  path) — that is kernel work and outside this dispatch.
- No fix was made, by order.
