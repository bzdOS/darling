#!/usr/bin/env python3
# ptyrun2.py LOGPATH CMD... — like ptyrun.py (pty for the child so its
# stdout is line-buffered, copied to LOGPATH in real time), but it exits
# with the CHILD's status so the caller can read the target's exit code.
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
    _, status = os.waitpid(pid, 0)
except OSError:
    status = 0
if os.WIFEXITED(status):
    sys.exit(os.WEXITSTATUS(status))
if os.WIFSIGNALED(status):
    sys.exit(128 + os.WTERMSIG(status))
sys.exit(0)
