# target-exit: the target's exit status after the park

The last slice of the park lane. `log-writer` closed the contradiction
(the log stops when the target leaves; the fd is the darlingserver's
inherited one), so the park ends with the target's exit. The remaining
datum is the fork: clean exit (code 0 — slow path, binding resolved
after >8 s; for Chromium a wait, not a wall) or by signal (SIGSEGV /
SIGABRT — a crash in the vendor wrapper, wall #6).

## Method

`sh build-freebsd/target-exit.sh` (MODE=pty default; MODE=file
available). The probe runs under a shell wrapper that records `$?`
after `timeout … launch-dynamic` — launch-dynamic exec's mldr, which is
the target, so that status **is** the target's. MODE=pty runs the
probe's stdout on a pty via `ptyrun2.py`, which propagates the child's
status. The timing polls the log for `DID-NOT-RETURN` and the wrapper
for its exit, independently of resolving the guest.

## Result (×6 reaching the park)

| run | mode | exit | Δt (DNR→exit) | logmark |
|-----|------|------|----------------|---------|
| 1 | pty | 0 | 0.05 s | LANE FINDING |
| 2 | pty | 0 | 0.05 s | LANE FINDING |
| 3 | pty | 0 | 0.05 s | LANE FINDING |
| 4 | file | 0 | ~0 s | LANE FINDING |
| 5 | file | 0 | ~0 s | LANE FINDING |
| 6 | pty | 0 | 0.05 s | LANE FINDING |

Every run that reached the park exited with **code 0** and the log
reached `[step 12]` (`LANE FINDING`). The Δt is at the poll floor
(0.05 s) because the guest's stdout is block-buffered even on the pty —
the marker and the final output flush together — so the real Δt is not
distinguishable by this harness; what is unambiguous is the exit code.

Caveat: one earlier run (file mode) died at `[step 03]` with the flaky
"Failed to connect to a window server" (exit 132 = SIGILL); that is the
harness's window-server flake, not the park.

## Verdict (one line)

**Exited code 0 (clean) — through a Δt at the poll floor after the
marker** (the marker is block-buffered; the exit code is the solid
datum). No signal: the target does not crash in the vendor wrapper — the
park is the slow path, not wall #6.
