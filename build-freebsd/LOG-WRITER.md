# log-writer: who holds the probe log after the guest leaves?

Question: the probe log runs on to `[step 10-12]` after the resolved
guest pid goes ESRCH. A dead host process does not print — so who
writes?

## Method

`sh build-freebsd/log-writer.sh` — MODE=file (stdout to a regular file,
no pty). The watcher resolves the fresh mldr by parentage, and at the
guest's departure runs `sudo lsof <log>` and `sudo fstat` (matched by
the log's inode) to find the descriptor holder, then re-checks 3 s
later. The log's line count and last marker are recorded at each stage.

## Finding (×4) — the holder is the darlingserver

At and after the guest's departure the log's fds **1w/2w** are held by
the **darlingserver** — the process `launch-dynamic` forks before
exec'ing mldr, which inherits stdout/stderr:

```
COMMAND     PID USER FD   TYPE ... NODE     NAME
darlingse  <ds> root 1w  VREG ... <inode>  .../wl-body-logwriter.log
darlingse  <ds> root 2w  VREG ... <inode>  .../wl-body-logwriter.log
fstat:  root darlingserver <ds> 1 / <inode> -rw-rw-r-- <sz> w
        root darlingserver <ds> 2 / <inode> -rw-rw-r-- <sz> w
holder ps: <ds> 1 SN <start> darlingserver
```

The departed guest does not appear in `lsof`/`fstat` for the log. In
all four runs the log was complete (`LANE FINDING`) at the departure and
**did not grow** over the next 3 s (runs a/b/c: 75720→75720,
75650→75650, 75779→75779).

## Verdict (one line)

**Nobody writes the log after the guest leaves — the log stops with the
guest.** The darlingserver merely *holds* the inherited stdout/stderr
fd (1w/2w) and never writes it; `[step 10-12]` are the guest's own
output, drawn from its block-buffered stdout at exit (MODE=file) — with
the pty (line-buffered) they appear before the exit. There is no
successor process printing the log.
