# MLDR-CHECKIN-BINDPUBLISH — does the host kernel publish the bind?

## Verdict (one line)

**YES — the kernel publishes the bind for every sockaddr shape probed,
including the exact server-code replica; the `sun_len` field is irrelevant
at the syscall boundary, and the bind-length argument of 106 publishes
fine.** The live-run divergence (no table row, `ECONNREFUSED`) is therefore
NOT the kernel and NOT the visible bind shape — it is in what the live
server's socket actually is at send time (binary provenance / fd-3 lifetime),
which is the next step.

## What the probe fixed

`build-freebsd/bind-publish-probe.c` + `bind-publish-probe.sh` (host-only,
no guest, no overlay). Matrix over the same path shape
(`/tmp/darling-run.XXXXXX/.darlingserver.sock`, SOCK_DGRAM|NONBLOCK|CLOEXEC):

- `sunlen=45` (client post-fix-up shape), `sunlen=106` (bind-length
  hypothesis), `sunlen=44` (one short), `sunlen=3` (namelen 1),
- `server` ×6: byte-for-byte replica of the server's bind site —
  uninitialized stack struct (stack pre-dirtied with 0xA5), `sun_family`
  set, `strncpy` into `sun_path`, `bind(fd, addr, sizeof(addr))`.

Result (log `bind-publish-probe.out` on the build host, PROVENANCE
`git_head=7b73f32038… kernel=15.1-RC2`): all 10 variants — `bind rc=0`,
socket file present, `netstat -f unix` carries the path row, client
`connect rc=0` and a 1-byte `send rc=1`, both with `auto` and `106` client
lengths. The replica observed `sun_len_byte=165` (the stack marker) and
still published.

## Why the field does not matter (kernel refs)

- `uipc_syscalls.c:1527` (`getsockaddr`): `sa->sa_len = len;` — the length
  argument of bind/connect/sendmsg OVERWRITES the first byte the user
  stored as `sun_len`. The kernel never sees the caller's field.
- `uipc_usrreq.c` (`uipc_bindat`): `sun_len > sizeof(sockaddr_un) → EINVAL`,
  `namelen = sun_len - offsetof(sun_path)`, namei CREATE, then
  `VOP_UNP_BIND(vp, unp); unp->unp_vnode = vp; unp->unp_addr = soun`.
- `uipc_usrreq.c` (`unp_connect`): length checks, namei LOOKUP of the
  client's path, `so2 = vp->v_socket; if (so2 == NULL) → ECONNREFUSED`.
- Probe cross-check: with the field set to 3 but the length argument 106,
  the FULL 43-char path bound and published — under field semantics that
  bind would have tried `/` and failed EADDRINUSE. Field ignored,
  measured.

So the earlier hypothesis "sun_len=106" (read from the kdump's
`bind(3,…,0x6a)` third argument) is a non-issue by construction: 106 is
exactly what the server passes as the LENGTH argument, and it publishes.

## The server code fact (unchanged behaviour, now harmless by this proof)

The bind site never initializes the struct: no `memset`, `sun_len` stays
stack garbage (`server.cpp`, the darlingserver listener creation:

    struct sockaddr_un addr;
    addr.sun_family = AF_UNIX;
    addr.sun_path[sizeof(addr.sun_path) - 1] = '\0';
    strncpy(addr.sun_path, _socketPath.c_str(), sizeof(addr.sun_path) - 1);

    if (bind(_listenerSocket, (struct sockaddr*)&addr, sizeof(addr)) != 0) {

). Per the probe this cannot affect publication. Hygiene fix
(memset + SUN_LEN) is worth one line someday, but it is NOT the -61 cause.

## The remaining divergence (measured, still open)

In the live RAW run (same trace file the previous steps used):

- server: `unlink → socket(PF_LOCAL, DGRAM|CLOEXEC|NONBLOCK) → setsockopt
  ×2 → bind(3,…,0x6a) → NAMI create → 0`; afterwards kevent loop,
  `close(0x4)`, mkdirs, `dserver.log` — **no unlink, no rebind, no
  `close(3)`** anywhere in the trace window;
- client (`mldr`): `sendmsg(fd, …, msg_name=path) → NAMI path → errno 61`
  — namei found the vnode, so the file exists but `v_socket` is NULL:
  the vnode at the path is NOT carrying a bound socket at that moment;
- watcher: post-bind live samples `file=y rows=0` ×2 (no table row while
  the server lives).

A bind of this exact shape publishes (probe). The live run's vnode carries
no socket. ⇒ the difference is in what the LIVE server's socket actually
is at send time. Narrowest candidates, next step:

1. binary provenance: the pinned server binary
   (`dserver_build=df9b743a…` in the run PROVENANCE) vs the tree — is the
   running binary built from this code at all (the pin predates the
   latest darlingserver commits);
2. fd-3 lifetime in the live window: is the binding socket still open and
   still the same socket at send time (`procstat -f` on the server pid
   inside the watcher window) — the watcher already samples liveness;
   add the fd view (one sudo re-exec, same protocol).

## Reproduce

    sh build-freebsd/bind-publish-probe.sh /tmp/bind-publish-probe.out
    # then: cat /tmp/bind-publish-probe.out
    # every KIND line must carry file=y rows=1 and client rc=0/rc=1

## Artifacts (raw, on the build host)

`bind-publish-probe.out` (10 KIND lines + PROBE lines + PROVENANCE).
