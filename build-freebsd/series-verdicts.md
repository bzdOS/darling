# Run verdicts of the chrome series, read with the guest-exit verdict

Every row is `chrome-run-verdict.sh` over one finished run log; the log itself is the
source, not the harness rc. The tool prints `GUEST-EXIT=<status>/<t>s macho=<n>
unimpl=<n> windows=<w>`, and `chrome-run-summary.sh` adds `RC=WRAPPER_RC` and the
guest lifetime. `GUEST-EXIT=none` means the log carries no `[launch] GUEST-EXIT`
line at all - that wrapper predates the guest-exit report, so there is nothing for
the new metric to read.

Reproduction (read-only, no build and no run):

```sh
sh build-freebsd/chrome-run-summary.sh <series logs...>
sh build-freebsd/chrome-run-verdict.sh <one log>
```

## Verdict per log

All 44 chrome run logs in the build directory, oldest first. `RC` is the wrapper's
own exit code and is kept only as a secondary field: 124 is the 120 s harness
timeout, 133/139/132 are 128+signal of the wrapper's child. In the log names the
series token of the last three runs is written `<series>`: the rest of the name
(run letter and index) plus the start date identifies the file.

| log | date (UTC) | GUEST-EXIT / t | macho | unimpl | windows= | RC |
| --- | --- | --- | --- | --- | --- | --- |
| chrome-sr-fix241.log | 2026-11-20T17:04:05Z | GUEST-EXIT=none / 0s | 0 | 0 | unknown | 133 |
| chrome-sr-fix242.log | 2026-11-20T17:04:32Z | GUEST-EXIT=none / 0s | 0 | 0 | 0 | 133 |
| chrome-sr-fix243.log | 2026-11-20T17:06:04Z | GUEST-EXIT=none / 0s | 0 | 0 | unknown | 133 |
| chrome-sr-fix244.log | 2026-11-20T17:06:20Z | GUEST-EXIT=none / 0s | 0 | 0 | unknown | 133 |
| chrome-sr-fix245.log | 2026-11-20T17:06:36Z | GUEST-EXIT=none / 0s | 0 | 0 | unknown | 133 |
| chrome-sr-fix246.log | 2026-11-20T17:06:59Z | GUEST-EXIT=none / 0s | 0 | 0 | unknown | 133 |
| chrome-sr-fix247.log | 2026-11-20T17:07:21Z | GUEST-EXIT=none / 0s | 0 | 0 | unknown | 133 |
| chrome-sr-fix248.log | 2026-11-20T17:07:33Z | GUEST-EXIT=none / 0s | 0 | 0 | unknown | 133 |
| chrome-sr-fix249.log | 2026-11-20T17:07:50Z | GUEST-EXIT=none / 0s | 0 | 0 | unknown | 133 |
| chrome-sr-fix2410.log | 2026-11-20T17:08:03Z | GUEST-EXIT=none / 0s | 0 | 0 | 0 | 133 |
| chrome-sr-fix261.log | 2026-11-21T03:16:58Z | GUEST-EXIT=none / 0s | 0 | 0 | 0 | 124 |
| chrome-sr-fix262.log | 2026-11-21T03:19:29Z | GUEST-EXIT=none / 0s | 0 | 0 | unknown | 139 |
| chrome-sr-fix263.log | 2026-11-21T03:21:08Z | GUEST-EXIT=none / 0s | 0 | 0 | unknown | 133 |
| chrome-sr-fix264.log | 2026-11-21T03:21:34Z | GUEST-EXIT=none / 0s | 0 | 0 | unknown | 133 |
| chrome-sr-fix265.log | 2026-11-21T03:21:53Z | GUEST-EXIT=none / 0s | 0 | 0 | unknown | 133 |
| chrome-sr-fix266.log | 2026-11-21T03:22:05Z | GUEST-EXIT=none / 0s | 0 | 0 | unknown | 133 |
| chrome-sr-fix267.log | 2026-11-21T03:22:25Z | GUEST-EXIT=none / 0s | 0 | 0 | unknown | 133 |
| chrome-sr-fix268.log | 2026-11-21T03:22:39Z | GUEST-EXIT=none / 0s | 0 | 0 | 0 | 133 |
| chrome-sr-fix269.log | 2026-11-21T03:24:10Z | GUEST-EXIT=none / 0s | 0 | 0 | unknown | 133 |
| chrome-sr-fix2610.log | 2026-11-21T03:24:31Z | GUEST-EXIT=none / 0s | 0 | 0 | unknown | 133 |
| chrome-sr-fix271.log | 2026-11-21T03:44:19Z | GUEST-EXIT=none / 0s | 0 | 0 | 0 | 124 |
| chrome-sr-fix272.log | 2026-11-21T03:46:31Z | GUEST-EXIT=none / 0s | 0 | 0 | 0 | 133 |
| chrome-sr-fix273.log | 2026-11-21T03:47:04Z | GUEST-EXIT=none / 0s | 0 | 0 | unknown | 133 |
| chrome-sr-fix274.log | 2026-11-21T03:47:13Z | GUEST-EXIT=none / 0s | 0 | 0 | unknown | 133 |
| chrome-sr-fix275.log | 2026-11-21T03:47:25Z | GUEST-EXIT=none / 0s | 0 | 0 | 0 | 133 |
| chrome-sr-fix276.log | 2026-11-21T03:48:58Z | GUEST-EXIT=none / 0s | 0 | 0 | unknown | 133 |
| chrome-sr-fix277.log | 2026-11-21T03:49:27Z | GUEST-EXIT=none / 0s | 0 | 0 | unknown | 133 |
| chrome-sr-fix278.log | 2026-11-21T03:49:47Z | GUEST-EXIT=none / 0s | 0 | 0 | unknown | 133 |
| chrome-sr-fix279.log | 2026-11-21T03:50:01Z | GUEST-EXIT=none / 0s | 0 | 0 | 0 | 139 |
| chrome-sr-fix2710.log | 2026-11-21T03:51:40Z | GUEST-EXIT=none / 0s | 0 | 0 | unknown | 133 |
| chrome-sr-fix281.log | 2026-11-21T05:06:37Z | GUEST-EXIT=none / 0s | 0 | 0 | unknown | 133 |
| chrome-sr-fix29b1.log | 2026-11-21T06:03:53Z | GUEST-EXIT=none / 0s | 0 | 0 | unknown | 133 |
| chrome-sr-fix301.log | 2026-11-21T06:17:32Z | GUEST-EXIT=none / 0s | 0 | 0 | 0 | 124 |
| chrome-sr-fix311.log | 2026-11-21T06:26:32Z | GUEST-EXIT=none / 0s | 0 | 0 | 0 | 133 |
| chrome-sr-fix321.log | 2026-11-21T06:45:42Z | GUEST-EXIT=none / 0s | 0 | 0 | 0 | 124 |
| chrome-sr-fix331.log | 2026-11-21T07:29:43Z | GUEST-EXIT=none / 0s | 0 | 0 | unknown | 133 |
| chrome-sr-fix341.log | 2026-11-21T07:49:31Z | GUEST-EXIT=none / 0s | 0 | 0 | unknown | 132 |
| chrome-sr-fix351.log | 2026-11-21T07:57:27Z | GUEST-EXIT=none / 0s | 0 | 0 | unknown | 133 |
| chrome-sr-fix361.log | 2026-11-21T08:05:12Z | GUEST-EXIT=none / 0s | 0 | 0 | unknown | 133 |
| chrome-sr-fix371.log | 2026-11-21T08:17:10Z | GUEST-EXIT=none / 0s | 0 | 0 | 0 | 133 |
| chrome-sr-fix391.log | 2026-11-21T10:30:24Z | GUEST-EXIT=none / 0s | 0 | 0 | 0 | 133 |
| `chrome-sr-<series>h1.log` | 2026-11-22T05:28:30Z | GUEST-EXIT=0x5 / 11s | 0 | 0 | 0 | 124 |
| `chrome-sr-<series>i1.log` | 2026-11-22T05:38:38Z | GUEST-EXIT=0x5 / 32s | 0 | 0 | 0 | 124 |
| `chrome-sr-<series>j1.log` | 2026-11-22T05:49:11Z | GUEST-EXIT=0x5 / 12s | 0 | 0 | 0 | 124 |

## What the rows say

- Only 3 of the 44 logs carry a `GUEST-EXIT` line: the last three runs, 2026-11-22
  05:28-05:49. The other 41 are `GUEST-EXIT=none` - their wrapper printed `WRAPPER_RC`
  only, so the new metric has nothing to read in them.
- In all three the guest died at t=11 s, 32 s, 12 s with `GUEST-EXIT=0x5`. The
  wrapper's `RC=124` is only the 120 s harness timeout: the harness kept waiting for
  a guest that had been dead for 100-170 s. That is the whole point of the metric -
  `rc=124` and "the guest died at 12 s" are different facts.
- `macho=0` and `unimpl=0` in all 44 logs. The pattern the macho counter looks for
  (`chrome-macho[<pid>] ...`) appears in 0 of 44 logs, so here the field is a
  constant and does not separate "reached the AppKit stage" from "never got there".
  Read it as "the guest never emitted a stage line", not as a stage verdict.
- `windows=` is 0 in the three new runs and `unknown` in 28 of the 41 older ones (no
  snapshot ran there). In the three new runs the `SNAP windows=0` line is written
  after `[launch] GUEST-EXIT`, i.e. the snapshot ran on an already dead guest.

## The first blocker between chrome-macho launch and a window

Stage: guest start-up, inside the signal-exception path - before any window exists.

How far the guest got in all three runs: dyld loaded 136 images (the last
`[dyld-trace]` line is `[135] ... libxkbcommon.dylib <-NEW`), the Wayland client shim
connected to the compositor, finished its first roundtrip (`[WaylandDisplay]
roundtrip1 returned 57` in the 05:28 and 05:38 runs, `2` in the 05:49 run) and
installed its blocking event wait
(`WAYLAND-EVENTS installed: blocking event wait in -[WaylandDisplay
nextEventMatchingMask:...]`). Nothing after that: no window (`windows=0`), and the
guest is dead at 11-32 s.

The line that ends the guest, identical offset in all three runs:

```
*** dserver_rpc_interrupt_exit failed with code -111 ***
[darling-mldr] genuine SIGILL (ud2, not a patched trampoline) at libsystem_kernel.dylib+0x28c2a rip=0x2ecb1198c2a rax=0x0
[darling-mldr] FATAL signal 4 (code=5) at addr=0x2ecb1198c2a
[launch] GUEST-EXIT pid=64117 status=0x5 t=12s
```

- The stage has a name: `libsystem_kernel.dylib+0x28c2a` is the `ud2` that terminates
  `___simple_abort`, and its caller frame `+0x2bebb` is the abort right after
  `sigexc_handler` fails `dserver_rpc_interrupt_exit` with -111 (ECONNREFUSED). So the
  guest aborted itself from its signal handler, it was not killed from outside.
- In the two earlier runs (05:28, 05:38) the next line is `CRASH-HANDLER-ENTER
  signal=006` / `FATAL signal 6 (code=65537)` - the crash handler aborting on top of
  the same `ud2`, same offset.
- The number it is visible by: `GUEST-EXIT=0x5` at t=11/32/12 s, `windows=0`, and the
  same `libsystem_kernel.dylib+0x28c2a` in 3 of 3 runs.

What the logs do not show, so it is not a verdict: why `dserver_rpc_interrupt_exit`
returned -111. All three logs also carry `darlingserver: launcher closed the lifetime
pipe, exiting`, and 05:49 is the only run that printed the -111 line, so the order of
those two events is not established here. That is the next thing to measure, not a
conclusion.

## Logs that could not be judged

- `thread-lane-5.log` - 0 bytes; the tool prints `GUEST-EXIT=none/0s macho=0
  unimpl=0 windows=unknown` and exits 2 (unreadable).
- `smoke-chrome-*.log` - no such file in the tree.
- The run before them (`...g`) - its log is gone; only the old-format series summary
  survives
  (rc=124, windows=0, no `GUEST-EXIT` line), so there is nothing to parse.