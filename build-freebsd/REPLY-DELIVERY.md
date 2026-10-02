# reply-delivery: the reply ARRIVED in the window — 36 bytes read, the
# lane's callback never fired

Lane reply-delivery. The hole the order named: FIONREAD=0 at the lane's
poll entry does not exclude "done came and main drained it in the
flush->poll window" — the measured concurrent-reader link (c). The
read-side anchor (`read: fd=%d -> %ld` + `read-tid` at
wl_connection_read, locale-free writer) closes it with numbers.

## The (a) chronology, complete (wl-body-reply.log)

```
flush: sendmsg fd=8 -> 116              <- the lane's sync request out
ppoll ENTER fd=8 events=1 timeout=NULL  <- the lane polls
fd-state fd=8 fionread=0                <- socket empty at poll entry
[wlbody] #61 ppoll LEAVE ret=1          <- the poll WOKE
[wlbody] #62 dispatch: wl_display_read_events call
[wlbody] #63 wl_display_read_events ENTER tid=<lane>
[wlbody] #64 read_events ENTER tid=<lane> reader_count=1 -> 0
[wlbody] #65 branch: this thread is the reader
[wlbody] #66 wl_connection_read ENTER
read: fd=8 -> 36                        <- bytes WERE on the socket
[wlbody] #67 wl_connection_read LEAVE total=36
[wlbody] #68 wl_display_read_events LEAVE ret=0
   ... 8s bound, no callback ...
lane: DID-NOT-RETURN within 8000ms
```

## Discrimination with numbers

- **The compositor answered.** The reply did not exist at poll entry
  (FIONREAD=0) but existed milliseconds later: the lane's own poll
  woke (ppoll LEAVE ret=1), the lane read as the sole reader
  (reader_count=1 -> 0), and **36 bytes came off fd=8 inside the
  lane's own read chain** (#66 -> #67 total=36). "Done never arrived"
  (the sway-side branch) is excluded by measurement.
- **The lane's callback did not fire.** The later (c)-era cycles on
  the same display read the sync answer as a clean 24-byte frame
  (flush 12 -> read 24, twice) — the done frame is 24 bytes when it
  comes solo. The (a) window delivered a 36-byte batch instead, the
  lane consumed it, and its roundtrip stayed parked: the bytes that
  included its sync answer were decoded but never routed to its
  callback. The loss point is therefore **post-read routing — the
  proxy/queue delivery of the done frame — not the compositor and not
  the poll** (the onary's option 1: done-arrived-and-not-delivered;
  "routing очередей прокси" is the named mechanism to pin next).
- One open measurement wrinkle, recorded honestly: the `read-tid`
  marker printed the same thr_self value (101092, the host main lwpid
  per the startup traplog) on reads whose surrounding dispatch
  markers (#63-#66) carry the lane's pthread-space id — the two id
  spaces (host lwpid vs guest pthread_t) or the marker's tid source
  needs one calibration run before the routing slice relies on it;
  the byte counts and the chain placement (#66 -> #67) do not depend
  on it.

## Next

The routing slice: name where the decoded done frame goes between
`wl_connection_read LEAVE` and the callback — the candidate sites are
the event queue's proxy lookup (wl_closure dispatch by proxy id) and
the display's default-queue ownership when the reader is a spawned
lane; a read-side marker at the queue-queue hand-off (the closure
dispatch point) with the proxy id closes it.
