# replay-storm root cause (no-repro branch evidence)

Status: storm NOT reproduced in 2 fresh runs on trunk 5e9099c40; no code fix
committed (a fix is only validatable on a reproduced storm). This note is the
evidence package: log facts + code quotes from both sides.

## Fresh runs (this branch, trunk 5e9099c40, env canon /tmp/ts9-run.sh)

- tsa1 (chrome-sr-tsa1.log in the build dir): `queued for replay` = 0,
  `_createPlatformWindowOnMainThread enter` = 0, GUEST-EXIT 0x6 at t=6s
  (early abort, died before the storm window at ~t=20s).
- tsb1 (chrome-sr-tsb1.log in the build dir): `queued for replay` = 0,
  `NSApplication run enter` reached, GUEST-EXIT 0x5 (SIGTRAP) at t=5s.
- Reference storm run hd1 (chrome-sr-hd1.log in the build dir, same trunk):
  `queued for replay` = 12, enter NOT reached, GUEST-EXIT 0x6 at t=22s,
  abort rip=0x22a2c4 (mldr text 20b000-231000).

Storm rate stays ~40%: 2/5 on trunk (thf1, hd1), 3/5 CHECK-variant. 0/2 here
is consistent with intermittency, not with the storm being gone.

## Storm signature (hd1 lines 25243-25254)

All 12 replays are identical:

```
[dserver] tid=1022256(1022256): call 4 arrived while call 4 is pending
  (active call -1, interrupts 2, queued 0); queued for replay (#1..#12)
```

Call 4 = `kprintf` (guest debug-print RPC):
`generated-rpc/include/darlingserver/rpc.h:42: dserver_callnum_kprintf = 4U`.
Preceded by a tight ~10 Hz SUSPEND/RESUME loop of tid 1022256 by sampler tid
289603 (hd1 lines 25100-25142, one pair every ~5 lines), then
`CRASH-HANDLER-ENTER signal=006`, abort rip=0x22a2c4.

## Server side (dserver)

`src/thread.cpp:320` (via `build-freebsd/patches/darlingserver-pending-call-queue.patch:16`):

```cpp
} else {
    // The client sent a second call before the pending one was started ...
    // queue it behind the pending call and replay it like an interrupt instead.
    ...
    fprintf(stderr, "[dserver] tid=%d(%d): call %d arrived while call %d is pending ...",
```

`src/thread.cpp:717-723` (drain, same patch hunk @703): the queued calls are
only popped one per `doneWorking` cycle, and only `if (!_pendingCall)`. While
the target thread stays parked in suspend, `_pendingCall` never clears, so the
queue only grows and no reply ever goes back to the guest.

## Guest side (libsystem stub — NO retransmit loop)

`scripts/generate-rpc-wrappers.py:1434-1462`: the generated stub has
`retry_send` / `retry_receive` labels that only retry on the interrupt status
(EINTR-style), plus `PUSH_UNKNOWN_REPLIES` handling. There is NO timeout /
retransmit path: one `kprintf()` call sends exactly one message and then
blocks in `receive_message` waiting for the reply.

Therefore the 12 call-4 arrivals are 12 genuine separate `kprintf()` calls the
guest main thread issued in a burst (Chrome logging loop, e.g. around the
`getifaddrs` error line visible just before the burst), already sitting in the
socket buffer when the sampler parked the thread. Messages 2..12 each hit
"already pending" because message 1 can never be processed while the thread
is suspended, and the guest blocks forever on reply 1 -> abort (0x22a2c4).

## Sampler side (duct-tape)

`duct-tape/src/thread.c:2097` (`thread_suspend`) -> `thread_hold`
(`thread.c:1473`) sends `LINUX_SIGRTMIN` via
`dtape_hook_thread_send_signal` (`src/server.cpp:293`, tgkill in
`src/thread.cpp:1711`). Log shows `signal=65` per suspend
(`LINUX_SIGRTMIN+1` = 65 used by the S2C path at `src/thread.cpp:1265`;
the sampler-driven suspends in hd1 arrive through the same signal). The
`PROBE-THREADSUSPEND` prints (`thread.c:2130`) confirm caller 289603 ->
target 1022256 at ~10 Hz with `user_stop_count=1`, i.e. the guest makes no
progress between suspends while its kprintf burst fills the socket.

## What a fix must do (not done here — needs a reproduced storm to validate)

- Server: never leave a `_pendingCall` unprocessed while its thread is parked;
  e.g. process/drain the pending call before parking, or cap the replay queue
  with resume-safe accounting (count suspended-thread calls separately).
- Sampler: the ~10 Hz suspend loop must not park a thread that has an
  unprocessed pending call with a blocked guest waiter, or the guest abort is
  guaranteed once the burst exceeds any bound.
