# dlsym-window: dlsym never blocks — the stall is post-bridge, in the
# vendored wrapper's __lazy gap

Lane dlsym-window. Suspect under test: the vendored dylib's `__lazy`
takes a fresh `dlsym` on every call and `dlsym` takes the rtld lock —
a concurrent holder (first-use lazy binding, a parallel dlsym) would
make the (a) lane's bridge call block, and the park depth would race.
Enriched markers landed first: `elf-dlsym ENTER/RETURN` carry the
symbol name, the monotonic tick in ms and the host lwpid, with a meta
line adding the guest pthread id and the handle (both id spaces, one
run); the writer stays locale-free.

## The ×3 distribution (wl-body-dlsymwin-{1,2,3}.log, identical config)

| run | (a) outcome | (a) depth                                | (d) outcome        |
|-----|-------------|------------------------------------------|--------------------|
| 1   | PARKED 8000 | shallow: dlsym resolved, no native entry | returned 2         |
| 2   | PARKED 8000 | shallow: dlsym resolved, no native entry | PARKED (fresh too) |
| 3   | PARKED 8000 | shallow: dlsym resolved, no native entry | returned 2         |

**3/3 (a) parked, 0/3 returned; the depth is shallow in every run** —
the lane's bridge resolves the symbol and then stalls ABOVE the native
`roundtrip_queue` entry (no lane `roundtrip_queue ENTER` anywhere,
while the main/(d)/(c) cycles enter and leave with TAIL ret=3/2).

## The holder question: there is no blocked dlsym

Per run: `elf-dlsym ENTER` and `elf-dlsym RETURN` counts are EQUAL —
56/56, 57/57, 57/57 — **every dlsym in every run completes, including
the (a) lane's**. No dlsym is ever left open, so no thread is inside
`dlsym` holding the rtld lock at the stall moment. The concurrent-
holder hypothesis is refuted for these runs: `dlsym` does not block,
alone or with company.

## Verdict

- The depth race is real but its shallow form dominates this
  configuration (3/3 here, 1 deep in the nolocale run).
- The stall sits AFTER the bridge returns: inside the vendored dylib's
  `__lazy` post-dlsym steps (the wrapper's `call *%rax` into the
  native function never lands, though the pointer resolved). The
  boundary forbids touching that dylib, so the next lever is
  probe-side (the (d)-probe note() lane the head queued) or rtld-side
  reading of the call path after `dlsym_fatal` returns.
- The rtld-side question the order named ("виснет и в одиночку") is
  answered for the dlsym itself: it never hangs — the hang is past it.

## Follow-up reading: the stall point is even EARLIER — before the
lane's first elf-dlsym ENTER

A per-tid pass over the same three logs sharpens the location. The
(a) lane's own markers (identified by its altstack OK tid — run 1:
tid 189575) show **altstack registration and then NOTHING**: no
`elf-dlsym ENTER` for that tid anywhere before the DID print, while
the only dlsym in the window belongs to the main thread
(`str=wl_display_get_error`, lwp=103167, its xdg-era resolution). The
wrap-ENTER content lines (recovered despite their two-write format)
exist for main (x3), the (d) lane and the (c) cycles — **none for the
(a) lane**.

So in these runs the (a) lane stalls between its thread start (altstack
OK) and the elfcalls `dlsym_fatal` ENTRY — inside the vendored
wrapper's `__lazy` entry region (the wrapper's prologue through the
indirect call into the elfcalls table). Combined with the
ENTER/RETURN parity (no dlsym ever blocks) the gate is now pinned to
**the vendored dylib's __lazy pre-dlsym instructions, state-dependent**
— it can catch the lane at thread start, inside the bridge, after the
bridge, or inside the native body, and the ×3 runs show the earliest
form dominating. The dylib remains untouchable; the probe-side
(d)-note lane and rtld/elfcalls-side observation (the dlsym_fatal
ENTER marker's ABSENCE on the parked lane is itself the proof the
stall precedes it) are the levers that remain.

