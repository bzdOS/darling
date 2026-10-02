#!/usr/bin/env python3
# ptyrun.py LOGPATH CMD... — run CMD with stdout on a pty (line-buffered
# for the guest) and copy the pty output to LOGPATH, flushing every read
# so the log is real-time.
import os, sys, select

logpath = sys.argv[1]
argv = sys.argv[2:]

pid, fd = os.forkpty()
if pid == 0:
    os.execvp(argv[0], argv)
    os._exit(127)

log = open(logpath, "wb")
while True:
    try:
        r, _, _ = select.select([fd], [], [], 1.0)
    except OSError:
        break
    if fd in r:
        try:
            data = os.read(fd, 4096)
        except OSError:
            break
        if not data:
            break
        log.write(data)
        log.flush()
try:
    os.waitpid(pid, 0)
except OSError:
    pass
