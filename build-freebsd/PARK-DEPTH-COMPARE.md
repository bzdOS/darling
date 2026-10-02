# park-depth-compare: with the fault gone, (a) and (d) park at DIFFERENT
# depths

Reading of `wl-body-nolocale-t.log` (the fault-free gated run, merged
as part of the traplog-nolocale slice): the two spawned opaque
roundtrips no longer park at the same depth, and the difference is now
measurable.

## Variant (a) — parks INSIDE the native body

The (a) lane's trail (altstack OK tid 167686, host lane tid
...670096): `elf-dlsym ENTER -> RETURN 0x83a5ab3d0` (bridge resolves),
then `wrap ENTER`, `#68 roundtrip_queue ENTER` .. `#73
prepare_read_queue reader_count=1` .. **`#74 ppoll(fd=8, POLLIN,
timeout=NULL)` — last marker, no return.** The (a) lane is THE reader
of the stateful display and its infinite poll never becomes readable
while main is at rest.

## Variant (d) — parks ABOVE the native entry

The (d) lane (altstack OK tid 167687) shows `elf-dlsym ENTER ->
RETURN 0x83a5ab3d0` at lines 74528-74529 — the bridge resolution also
COMPLETES on the fresh-display path — and then **nothing: no `wrap
ENTER` for its thread anywhere in the log.** Exactly five wrap lines
exist in the whole run: three on the main thread (setup/xdg), one for
the (a) lane, and the two (c)-era lines (74533, 74732 — both on the
SESSION display, with a full dispatch cycle under them). The (d)
lane's park therefore sits between the successful dlsym RETURN and the
native `wl_display_roundtrip` entry — inside the vendored wrapper's
post-`__lazy` steps or the fresh-display object handling — i.e. a
shallower gate than (a)'s.

## What the depth difference says

- The bridge (`elfcalls->dlsym_fatal`) is innocent for BOTH lanes:
  both resolutions complete on their threads.
- The (a) park is a state/wait inside the native read path
  (reader arbitration: one reader, infinite ppoll, nobody wakes it).
- The (d) park is an entry-path stall above the native body — the
  same shape the ORIGINAL (a) park had before the locale fault was
  removed; with the fault gone, (a) moved deeper and (d)'s shallow
  park became visible for the first time.
- Candidate for the (d) gate: the fresh `wl_display_connect` object's
  post-connect state on a guest-created thread (the connect ran in the
  lane; its display wrapper/shim state is what the subsequent call
  touches before entering the native body).

## Next

A wrap-anchor one step EARLIER in the vendored path is not possible
(the dylib is prebuilt and untouchable), so the (d) gate gets named
from the probe side: extend the probe's (d) block with a note() on
each step between the fresh connect and the roundtrip call — which
sub-step stalls becomes visible without touching the dylib.
