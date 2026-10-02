# dispatch-exit: the dispatch path completes cleanly — the (a) lane
# stands at a DIFFERENT gate this run

Lane dispatch-exit. The order asked: at which unlock/wait the (a) lane
stands on dispatch_queue's post-callback exit. The anchors landed
(pre-invoke unlock, post-callback re-lock + ACQUIRED, dispatch_queue
TAIL, dispatch-pending unlock with reader_count, cancel_read unlock)
and the run answers with numbers — but not at the dispatch exit.

## The dispatch-exit path: clean, measured (wl-body-dispexit.log)

The main thread's cycles (pt=10419345567760) run the whole exit path
in order, e.g. the xdg-receipt cycle:

```
74543 dispatch_event pre-invoke unlock rc=0
74549 dispatch_event pre-invoke unlock rc=0
74550 sync-callback fired cb=0x...09860 q=0x...330f8 ser=69
74551 dispatch_event post-callback re-lock rc=0
74552 dispatch_event re-lock ACQUIRED
74553 dispatch_queue TAIL count=3 err=0
74554 dispatch-pending unlock rc=3 reader_count=0
74559 roundtrip_queue TAIL done=1 ret=3
```

Every unlock fires, the re-lock is ACQUIRED, the queue tail returns
count=3 with no error, the pending unlock releases with
reader_count=0. **The dispatch-exit path is not where anything stands
— in this run.**

## Where the (a) lane stands: inside the host dlsym

The (a) lane (altstack OK tid=188252) shows `elf-dlsym ENTER` at
74792 and **no RETURN, no wrap ENTER, no roundtrip_queue ENTER** — the
lane is parked inside `elfcalls->dlsym_fatal` (host rtld dlsym), the
shallow gate of the original slices. It never reaches the native body
in this run at all.

## The standing point varies between runs — a measured fact

The same (a)-order probe now shows two different park depths with the
same instrumentation family:

- nolocale run: the lane RESOLVES the symbol (dlsym RETURN), enters
  the native body, runs the full trail, and parks deep at
  `#74 ppoll(fd=8, POLLIN, timeout=NULL)` with reader_count=1;
- this run: the lane enters `elf-dlsym` and never returns — the
  shallow gate at the bridge, before the native body.

Correlation candidate with numbers: in this run the session's main
thread completed a roundtrip milliseconds before the (a) lane's
bridge call (xdg receipt 74502-74559, lane dlsym 74792) — the shallow
park follows a just-touched session; the deep park followed quieter
states. The (a) gate is state-dependent at the native boundary, and
its DEPTH is not a fixed property of the code path.

## Verdict and next

- The dispatch-exit unlocks are exonerated for this run (clean
  completion on the control cycles).
- The (a) lane stands inside the host dlsym (bridge) in this run —
  the same shallow shape the earliest slices measured.
- Next discriminator: an ordering probe with a deliberate idle gap
  between the last main roundtrip and the (a) lane's start — if the
  deep ppoll park returns with the gap, the shallowness is a hot-
  session artifact of the bridge call, and the real gate remains the
  deep read-path arbitration.
