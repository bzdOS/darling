# MLDR-CHECKIN-TABLE — the unix-socket table while the server lives

## Verdict (one line)

**While `darlingserver` is alive and after its `bind`, the path
`.darlingserver.sock` is ABSENT from the host unix-socket table**:
`bind(3) -> 0` creates the vnode (the socket file the launcher itself waits
for with an `fstatat` poll), but the address never appears in `netstat -f
unix` — senders by path therefore get `ECONNREFUSED (-61)`.

## Instrument

`build-freebsd/mldr-unp-table-watcher.sh` (v4): one passwordless-sudo
re-exec, then ~28 Hz samples that pair, in the same iteration:

- server liveness: `pgrep -x darlingserver` + `procstat basic <pid>`;
- the unix table: every `netstat -f unix` row carrying `.darlingserver.sock`;
- the run's own socket FILE: `test -S <prefix>/.darlingserver.sock`.

`file=y` pins a sample to after the bind: the launcher starts the guest only
after that file exists (trace: launch-dynamic `fstatat -> ENOENT -> nanosleep`
poll; dserver does `unlink` then `bind(3) -> 0`), and the file outlives the
server (it leaks), so post-death samples carry `file=y` too — the `alive`
column separates post-bind-live from leaked. The prefix comes from the run
log's `send-probe` line (printed at guest start, inside the live window).

Every watched run is paired with a control run without the observer: staging
steps and `GUEST-EXIT` timings identical (`t=0/1s` both) — the watcher does
not slow the launch.

## Run

PROVENANCE `git_head=723ef93cd36a9db28fea6df8839397765a7b521b`, same binaries
`mldr_build=2f5976f510f94a1731a49c24a9ce52e5`,
`darlingserver_build=df9b743a8c3d0a9d31906b2292904f41`.

- control `chrome-sr-unp13b.log`: BAD SEND -61 x2, GUEST-EXIT t=1s, DSERVER
  EXIT graceful.
- watched `chrome-sr-unp15.log`: BAD SEND -61 x2, GUEST-EXIT t=1s, DSERVER
  pid=30906. `unp15-watch.txt`, 1619 samples, live=3:

```
W 65 t=1795641302 pid=30906 alive=30906 file=?  rows=0 mine=0
W 66 t=1795641302 pid=30906 alive=30906 file=y  rows=0 mine=0
W 67 t=1795641302 pid=30906 alive=30906 file=y  rows=0 mine=0
W 68 t=1795641302 pid=30906 alive=no   file=y  rows=0 mine=0
```

W66/W67: post-bind, server alive, NO row for the path. No sample in the whole
run — nor in the earlier v3 run (`unp14-watch.txt`, 1250 samples) — ever
carried a `.darlingserver.sock` row, while `/var/run/log` and
`/var/run/logpriv` rows are present in every snapshot, so the table view
under this exact command does show bound dgram sockets of other processes.

## RULE block facts (first live sample)

- `jls`: empty — no jail.
- credential: uid 0, groups `0,0,5`.
- unp14's RULE: `root=/` — no chroot (the unp15 RULE's fd view was lost to
  the death race: `procstat: kinfo_getfile(): No such process` — the server
  died inside the RULE block; unp14 carries those facts).

## What is now excluded

- jail / chroot / uid effects — excluded (RULE blocks above).
- "the snapshot postdated the server's death" (the limit recorded at the
  earlier one-shot snapshot) — excluded: `file=y` and `alive=<pid>` in the
  same sample.
- "the observer slowed the launch" — excluded: control vs watched timings
  identical (`t=0/1s`).
- kernel-side visibility of bound dgram paths under this command — works for
  `/var/run/log*` rows in the same snapshots.

## Remaining question (next step)

Why does `bind(3) -> 0` (listener socket `SOCK_DGRAM|NONBLOCK|CLOEXEC`,
`sun_len=106`) not publish the address: host-kernel behaviour with this exact
socket, or something in the dserver's bind path. Narrowest probe first: a
minimal host C probe that binds a `SOCK_DGRAM` unix socket with
`sun_len=106` over the same path shape and then reads `netstat -f unix` — if
the row appears there, the kernel is not the cause and the difference is in
what the dserver actually binds.

## Artifacts (raw, on the build host)

`chrome-sr-unp13b.log` (control), `chrome-sr-unp15.log`, `unp15-watch.txt`
(1619 samples), `chrome-sr-unp14.log`, `unp14-watch.txt` (1250 samples),
`cause1.kdump.txt` (trace: fstatat poll, unlink, `bind(3) -> 0`).
