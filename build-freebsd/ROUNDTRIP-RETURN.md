# roundtrip-return: the roundtrip does NOT return — 120 s bound, and
# the tail marker never fires for the (a) lane

Lane roundtrip-return. The order's two candidates: the post-done tail
of `wl_display_roundtrip_queue` vs the 8 s bound burning under the
LD_DEBUG trace flood. Both anchors landed: the probe's lane bound is
now runtime-overridable (`WL_LANE_BOUND_MS`) and the instrument gained
a TAIL marker between the dispatch loop's `done` and the function's
return.

## The measurement (wl-body-return.log, WL_LANE_BOUND_MS=120000)

```
lane: DID-NOT-RETURN within 120000ms — parked inside wl_display_roundtrip
```

**The (a) lane did not return in 120 SECONDS.** The bound-artifact
hypothesis is refuted by an order of magnitude: under the same trace
load, every OTHER roundtrip in the run completes (see the TAIL
markers) — the (a) call is a real stall.

The TAIL markers, complete inventory:

```
72024 roundtrip_queue TAIL done=1 ret=57 pt=...85840   <- main, session cycle
72124 roundtrip_queue TAIL done=1 ret=22 pt=...85840   <- main, session cycle
74288 roundtrip_queue TAIL done=1 ret=3  pt=...85840   <- main, xdg receipt
   [ (a) lane: sync-callback FIRED per the queue-routing slice — NO TAIL ]
74571 roundtrip_queue TAIL done=1 ret=2  pt=...96080   <- (d) lane: returned 2
74679 lane: roundtrip returned 2 (errno 0)             <- (d) completed
74784 roundtrip_queue TAIL done=1 ret=2  pt=...98128   <- (c) cycle
74889 roundtrip_queue TAIL done=1 ret=2  pt=...85840   <- (c) main
```

**Every cycle except (a) prints TAIL done=1 and returns. The (a)
cycle's TAIL never fires** — with its sync-callback proven fired
(queue-routing slice: cb bound to and dispatched on the session queue
0x...300f8). The stall therefore sits BETWEEN the fired callback and
the tail marker: inside `wl_display_dispatch_queue`'s post-callback
exit path on the spawned lane — the mutex/reader_cond bookkeeping
dispatch_queue performs before returning to the roundtrip loop. That
is the same read-path arbitration family the earlier slices named
(the reader_cond/ppoll parks), now pinned to the exact window: after
the callback fires, before dispatch_queue hands control back.

## Verdict

- Not the bound: 120000 ms, no return.
- Not the routing: the callback fired on the right queue.
- The stall: the (a) lane is stuck inside dispatch_queue's exit path
  after its callback fired — the arbitration bookkeeping (display
  mutex / reader_cond) on a spawned guest thread with main at rest.
- Next: the same two anchors moved one level down — a marker at
  dispatch_queue's own tail (after the closure dispatch, before its
  unlocks) — names which unlock/wait it stands on.
