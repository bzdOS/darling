# Control #142 step 19 — mldr death core: SIGSEGV, rip and backtrace from the core

**Date:** 2026-10-09
**Branch:** task/mldr-death-core
**Base:** pr-arm64 = f84e4afaea (after step 18 merge)

## sysctl values (read before the run)

```
kern.coredump: 1
kern.corefile: %N.core
```

## Run — same recipe as step 18, core enabled

`ulimit -c unlimited` inside the run shell (verified: `sudo sh -c 'ulimit -c
unlimited; ulimit -c'` → `unlimited`). One sysctl substitution so the core
lands in a writable path: `kern.corefile: %N.core -> /tmp/mldr-dead.core`
(restored to `%N.core` after the run). Log: `diag/trap-resume-fix-3.log`
(1 292 569 lines, 99 851 497 B — the same five hits as step 18, death after
hit #5).

## dmesg — the death line

```
pid 88069 (mldr), jid 0, uid 0: exited on signal 11 (core dumped)
```

**The death mode is SIGSEGV (signal 11), with a core dump.** (Earlier runs
without the core showed the same signal: `pid 38451 (mldr) ... exited on
signal 11 (no core dump - coredumpsize limit is 0)` — the signal was always
SIGSEGV; only the core was missing.)

## Core and backtrace

Core: `/tmp/mldr-dead.core` (332 832 768 B). `lldb -batch -c <core> -o 'bt 5'
-o 'register read rip'`:

```
* thread #1, name = 'mldr', stop reason = signal SIGSEGV
  * frame #0: 0x00000a60cdf6ef12
  * frame #1: 0x00000a60cdf6f347
  * frame #2: 0x00000008216c32d3
  * frame #3: 0x00000a60d0a5a3e8
  * frame #4: 0x00000a60d0a5987e
rip = 0x00000a60cdf6ef12
```

rip = 0x00000a60cdf6ef12 (frame #0). The frames are host addresses of the
mldr process (the core is a FreeBSD host core of mldr, not a guest core).

## sysctl restored

```
kern.corefile: /tmp/mldr-dead.core -> %N.core
```

## Verdict

The silent death is a **SIGSEGV in mldr** (host process), not a clean exit
and not a guest-visible signal. The guest dies because mldr (its host loader)
crashes. The crash is deterministic across steps 17–19 (same five hits, same
death point after hit #5 — the second `__platform_bzero` call on
`this+0x1640`, 8242 bytes).

## Repro

```sh
sysctl kern.coredump kern.corefile          # 1, %N.core
sudo sysctl -w kern.corefile=/tmp/mldr-dead.core
sudo sh -c 'ulimit -c unlimited; <step-18 run command> > trap-resume-fix-3.log 2>&1'
sudo dmesg | tail -20                       # pid N (mldr) ... exited on signal 11 (core dumped)
sudo lldb -batch -c /tmp/mldr-dead.core -o 'bt 5' -o 'register read rip'
sudo sysctl -w kern.corefile=%N.core        # restore
```

Logs: `diag/trap-resume-fix-3.log` (99 851 497 B), core `/tmp/mldr-dead.core`.
