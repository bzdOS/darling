# queue-routing: the done went to the RIGHT queue and the callback
# FIRED — the gap is after the callback

Lane queue-routing. The order's question: which queue the decoded done
landed on, which queue the lane's callback waits on, and where the two
diverge. Anchors added (locale-free writer, behaviour untouched): the
sync-callback fire (callback pointer + its proxy's queue), the
queue_event landing (event id/opcode/proxy -> queue), and the
dual-space read marker (host lwpid + guest pthread_t in one line —
the calibration the reply-delivery slice flagged).

## The (a) cycle, as measured (wl-body-queue.log)

```
74283 roundtrip_queue ENTER tid=<cycle> display=0x...30000 queue=0x...300f8
74285   set_queue wrapper=0x...097a0 queue=0x...300f8   <- display queue set
74287   sync(display_wrapper=0x...097a0) returned callback=0x...09860
74294 flush: sendmsg fd=8 -> 116                        <- request out
74296   ppoll ENTER fd=8 events=1 timeout=NULL
74317 queue_event: ... -> ...                            (ids garbled, see note)
74318 queue_event: ...
74319 queue_event: ...
74324 sync-callback fired cb=0x3216c1609860 q=0x3216c16300f8
74459 lane: DID-NOT-RETURN within 8000ms
```

The callback pointer fired (0x...09860) is EXACTLY the one created by
this cycle's sync (74287), and the queue it was bound to
(0x...300f8) is EXACTLY the queue the cycle's roundtrip_queue entered
and the set_queue installed. **The receiver queue equals the callback
queue: the routing is consistent — both mismatch hypotheses
(receiver != callback / queue right but never dispatched) are refuted
by the numbers: the callback ran.**

## Where the gap actually is

The callback fired (which sets *done=1 inside sync_callback) yet the
probe's lane reported DID-NOT-RETURN — the loss sits BETWEEN the
fired callback and the roundtrip's return to the lane thread: the
post-done path of `wl_display_roundtrip_queue` on the spawned lane, or
— calibration caveat recorded — the 8 s wall-clock bound expiring under
the LD_DEBUG trace flood before the return landed. The routing layer
is exonerated; the next probe targets the roundtrip's post-done tail
(a marker between the dispatch-loop's done=1 and the function's
return) and/or a bound-free variant of the (a) lane.

## Calibration and instrument notes

- The dual-space read marker resolves the reply-delivery wrinkle:
  `read-tid fd=8 lwp=184926 pt=55073311053840` — the host kernel lwpid
  and the guest pthread_t are different id spaces and are now both
  printed; trails can be joined safely.
- The first build of this slice garbled `%u` fields (the mini-formatter
  lacked %u; the queue_event ids/queues printed as `u`/collapsed, and
  the subsequent %p desynced). The routing conclusion above does not
  depend on them — it rests on the sync-callback's `%p` pair, which
  printed correctly. `%u` support added in the same commit series; the
  next cycle's queue_event lines will carry readable ids and queues.
