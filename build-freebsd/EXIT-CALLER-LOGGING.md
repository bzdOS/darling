# exit-caller-logging: what the five spots said

The instrument (committed): write(2) markers at the five bridge/trap
spots, env-gated `DARLING_TRAP_LOG=1`, default off (see
`src/startup/mldr/trap_log.h` and the commit). Two measured runs of the
(a)-order probe (WL_SKIP_B=1) with the gate on; both logs:
`wl-body-exitcall.log` / `wl-body-exitcall2.log`.

## Spot 5 — times(99) caller: NAMED

`linux-unhandled a=99` fired ×4 per run — **on the startup/main tid**
(101319 / 101089), never on a spawned lane, argument = a `tms *` userspace
pointer. So `times(2)` is called by the guest's own LINUX-built libc on
the main thread (its bootstrap/allocation path), not by the parked lane.
The ×4 ENOSYS cluster that sat at the (a) window in the earlier logs was
this main-thread activity all along; it is noise, not park evidence.

## Spot 3 — altstack status of spawned threads: OK, no WARNING path

`altstack OK tid=...` for every spawned lane (two per run), both with a
freshly mmap'ed stack. The :3063 WARNING path (no-altstack thread)
never fired in these runs — the lane wedges are not altstack-missing
events.

## Spot 2 — handler entry/exit: clean pairs, plus a NEW fatal event

The bootstrap SIGILL traps come in clean ENTER/LEAVE pairs on the main
tid (ud2-patched dyld raw syscalls: numbers 14/39/46/47/186 handled,
99/267 unhandled-and-left). But in BOTH instrumented runs the process
dies before the variant window with:

```
[darling-mldr] FATAL signal 10 (code=3) at addr=0x822cc2674
```

signal 10 = SIGUSR1, `si_code=3` = SI_QUEUE — a QUEUED signal sent
explicitly (pthread_kill/sigqueue), caught by mldr's crash handler and
treated as fatal. Uninstrumented runs of the same order do not show this
death before the window; whether the marker writes merely perturb the
timing that exposes a pre-existing hazard, or participate in it, is not
settled — the sender of SIGUSR1 is unnamed.

## Spot 1 — the exit(3) caller: the elfcalls slot is NOT it

`elf-exit CALL` — zero occurrences in both runs. The `P_WEXIT` evidence
from the previous slice therefore does NOT come from a guest-reachable
exit through the elfcalls slot (at least not before the window these
runs reach). The host-side `exit(3)` caller stays open; next suspects
are mldr's own error paths and the crash machinery, and the
signal-10 death above is itself a candidate mechanism for the earlier
P_WINDOW flip (a signal-driven teardown runs host destructors).

## Spot 4 — dlsym status on the parked lane: NOT REACHED

The main tid's `elf-dlsym ENTER/RETURN` pairs complete normally during
display init (native libwayland resolved). The (a) park itself was not
reached in the instrumented runs (they die at the signal-10 event first),
so the parked lane's dlsym state remains unmeasured with this instrument.

## Control (c) and default cleanliness

The gate is off by default: every run of the evening before this slice
is the default-off control, and the binary carries the markers without
emitting them. A dedicated (c) re-run under the gate was not spent —
the slice's budget went to the two (a) runs that produced the four
answers above.

## Next

1. Name the SIGUSR1/SI_QUEUE sender (who pthread_kills with 10 in the
   session build) — it gates every further window measurement.
2. Re-run the (a) order with ONLY the elfcalls-side markers (the
   handler-side writes disabled) to separate marker perturbation from
   the underlying hazard.
3. The exit(3) caller: instrument mldr's own exit-adjacent paths
   (crash_dump) — the slot-based hypothesis is refuted for the window
   these runs reach.
