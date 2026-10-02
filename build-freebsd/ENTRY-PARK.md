# Entry park: variant (a) parks ABOVE the native library — measured

Question: where does variant (a) of the session repro — stateful session,
main at rest, spawned lane, opaque `wl_display_roundtrip` — park? With the
13-anchor instrumented copy (previous slice) the parked lane emitted no
markers at all, so the park was somewhere between the guest's call and the
first instrumented line of `wl_display_roundtrip_queue`. Two hypotheses
survived: the guest-side export/lazy-bind chain into libwayland, or the
display mutex before the body's first statement.

## Instrumentation added

`build-freebsd/wl-native-debug/instrument-body.py` gains a 14th anchor — the
entry of the one-line wrapper itself:

```
wl_display_roundtrip(struct wl_display *display)
{
	WLBW("ENTER tid=%lu display=%p", (unsigned long)pthread_self(), (void *)display);
	return wl_display_roundtrip_queue(display, &display->default_queue);
}
```

The marker uses a separate `WLBW` tag that does NOT consume the `#N`
sequence, so every number-to-site mapping from the previous slice stays
valid for the control comparison. The wrapper's entry is the first
reachable point of the NATIVE library after the guest-side bind and the
shim's forwarding; the shim export itself cannot be instrumented (the
backend dylib is a prebuilt artifact without sources in this tree).

## Reproduction

```
cd <run-dir> && sudo env DARLING_SRC_DIR=$DARLING_SRC_DIR \
    DARLING_OVERLAY=$DARLING_OVERLAY DARLING_BUILD_DIR=$DARLING_BUILD_DIR \
    DARLING_TEST_BINARY=guest-wl-session-roundtrip-macho \
    DARLING_STAGING_TREES='System/Library/Frameworks:System/Library/PrivateFrameworks:usr/lib' \
    LD_LIBRARY_PATH=<dir> LD_DEBUG=all \
    XDG_RUNTIME_DIR=<wayland-runtime> WAYLAND_DISPLAY=wayland-1 \
    timeout --foreground -k 5 180 $DARLING_BUILD_DIR/launch-dynamic \
    > wl-body-entry-a.log 2>&1
```

Environment: the ON condition of the previous slice (`LD_LIBRARY_PATH`
non-empty), and the instrumented copy planted in the run CWD at
`usr/lib/libwayland-client.so.0` — the relative component of the pinned-on
walk opens it (`Opened "usr/lib/libwayland-client.so.0", fd 8`). A copy
planted in the overlay cache is wiped before every run by the harness
`cleanup()`; plant in the run CWD or the source overlay instead.

## Result — variant (a): no markers, park above libwayland

The parked lane's whole window, verbatim:

```
[step 10] variant (a): main at rest, spawned lane runs wl_display_roundtrip
         lane started; main is NOT touching the display
[darling-mldr] unhandled Linux syscall 99 — ENOSYS        <- x4, the lane runs
         lane: DID-NOT-RETURN within 8000ms — parked inside wl_display_roundtrip
```

No `[wlbody] wrap ENTER`, no `roundtrip_queue ENTER`, nothing. The
instrumentation is not silent by accident: in the SAME process and the SAME
run the wrapper marker fires on every other lane — three times on the main
thread during session setup, once on the spawned lane of variant (d) (fresh
display, returned 2), and on both lanes of variant (c).

So the parked call never reaches the native library's exported entry. The
park is in the guest-side chain ABOVE libwayland: the backend's
`wl_display_roundtrip` export (prebuilt shim — lazy-bind/TLS/its own
locking), or the guest thread's bind machinery. Variant (d) proves the
identical chain completes on a fresh display, so whatever gates it reads
session state before entering the native library. The `ENOSYS` noise shows
the lane is scheduled and making raw syscalls up to the park.

## Control — variant (c): the read-path arbitration park stands

Same run, same anchors: variant (c)'s spawned lane emits the full trail
(`wrap ENTER` -> `roundtrip_queue ENTER` -> `sync` -> `dispatch_queue` ->
`prepare_read_queue reader_count=2` -> `ppoll ENTER` -> `ppoll LEAVE
ret=1` -> `read_events ENTER` -> `read_events ... reader_count=2 -> 1` ->
**`branch: other readers present -> reader_cond WAIT`** -> the
`pthread_cond_wait` bind), and parks there while another thread
(`reader_count 1 -> 0`, `this thread is the reader`) drains the 24-byte
reply from the fd and keeps dispatching. Main's roundtrip on the same
display returns 2. The new anchors did not disturb the observation: the
park link for the stateful session remains the read-path reader
arbitration — `reader_cond` wait (this run) or the blocking `ppoll` (the
previous run's trail) on the spawned lane while the reply is consumed by a
concurrent reader.

Secondary observation: in this run variant (b)'s decomposed lane also
parked (`done-callback DID-NOT-RETURN — parked at stage 3`), leaving a live
third thread that then competed as a reader in (c). The (c) park reproduced
in the previous slice WITHOUT that leftover, so the arbitration issue does
not depend on it; the leftover only changes which wait site is hit.

## Next observable step for (a)

The park is above the native entry and the shim has no sources here, so the
next measurement is outside the library: `truss` the guest run and read the
last syscalls of the parked lane's tid in the (a) window — the trail of
`ENOSYS` raw syscalls already shows the lane is alive and executing; the
syscall that stops appearing names the gate. Alternative probe-side lever:
have the probe `dlsym` `wl_display_roundtrip` itself and call it through
its own function pointer, bypassing the shim's export — if the park
disappears, the shim's export is the gate; if it stays, the gate is the
guest bind/TLS machinery below the shim.
