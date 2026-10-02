# roundtrip-wakeup: the client's side of the contract held — the reply
# is never posted

Lane roundtrip-wakeup. Who wakes the reader, by design: the client's
roundtrip marshals a `wl_display.sync` request and flushes it to the
display fd; the COMPOSITOR (the headless sway seat) answers every sync
with `wl_callback.done` carrying the latest serial; the bytes land in
the display socket, the reader's `ppoll(POLLIN)` wakes, the callback
fires and the roundtrip returns. The client owes exactly two things:
post the request, and poll while being the reader.

## Markers (locale-free writer, no stdio)

The instrumented native copy now formats its `[wlbody]` markers with a
hand-rolled subset formatter and `write(2)` (same fault class as the
survive-window slice — stdio was the locale exposure). Two anchors
added for this lane: `flush: sendmsg fd=%d -> %ld` at the client's
flush site, and `fd-state fd=%d fionread=%d` (ioctl FIONREAD) at the
native ppoll — the socket's contents at the moment the reader enters
its poll. Read-path behaviour untouched.

## The (a) chronology — the contract held on the client side

From `wl-body-wakeup.log` (gated run, WL_SKIP_B=1):

```
wrap ENTER tid=...33040 (stateful display)
roundtrip_queue ENTER
set_queue
sync returned callback=0x3b872f009860      <- request marshalled
flush: sendmsg fd=8 -> 116                 <- request bytes LEFT the client
ppoll ENTER fd=8 events=1 timeout=NULL     <- the lane polls, sole reader
fd-state fd=8 fionread=0                   <- the socket is EMPTY
   ... 8s bound ...
lane: DID-NOT-RETURN within 8000ms — parked inside wl_display_roundtrip
```

Both client obligations are met and measurable: the sync request was
marshalled and **flushed to fd 8 (116 bytes) BEFORE the lane entered
its ppoll**, and the lane polled as the sole reader
(reader_count=1). At poll entry the socket held **zero bytes** — the
compositor's answer was not in flight.

## Discrimination: reply-not-posted (compositor side)

Of the three candidate outcomes, the numbers select
**"ответ не поставлен"**: not a lost wakeup (the reply never existed —
fionread=0 rules out bytes sitting unread), and not a wake-delivery
defect (there was nothing to wake for). The lane waits for the
compositor's `done` callback that the seat does not produce on the
stateful session while main is at rest. Main's own roundtrips on the
same fd completed earlier in the build (xdg receipt), so the socket
and the seat's answering capability are intact — what differs is the
LANE's sync on the established session.

Named for the next slice (compositor side, outside this lane's
boundary): why the headless sway seat does not answer a sync that
arrives from the spawned lane's connection state on the established
session — the candidates are sway's event-loop servicing of the
connection when the client's main thread stops touching it, and the
serial/queue routing of the done callback for a second initiator.

## Note

The first instrumented build of this slice emitted broken `#N`
prefixes (the mini-formatter lacked `%.*s`; the content lines were
intact and are what the chronology above uses). Fixed in the same
commit series — the macro now uses the supported `%lu`.
