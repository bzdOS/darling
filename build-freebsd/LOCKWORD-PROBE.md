# lockword probe: the park is a property of the chain, not a leftover holder

Question: at the variant-(a) park, what is the rtld bind-lock word and who
holds it — a leftover of an earlier lane (hypothesis-leader), or the chain
itself?

## Order experiment (run A: variant (a) as the FIRST spawned lane)

The probe gained `WL_SKIP_B=1` — variant (b) is skipped entirely, so the
session is built by main alone and variant (a) becomes the first spawned
lane. Measured three times, the probe's own prints:

```
[step 09] variant (a): main at rest, spawned lane runs wl_display_roundtrip
         lane started; main is NOT touching the display
         lane: DID-NOT-RETURN within 8000ms — parked inside wl_display_roundtrip
```

**(a) parks on the virgin session.** Per the order's decision rule this
settles the verdict: the park is NOT the trace of a foreign holder left by
an earlier lane — it is a property of the chain/session itself. A
contrasting run with (b) first adds nothing decisive once (a) parks in the
first-lane order, so the B arm was not spent on this kernel.

A refinement the ordering forces: main's own roundtrips on the same
session COMPLETE (the xdg-configure receipt at step 8 returns), so the
bind lock is not "held forever by a stuck thread" — main acquires it
freely while the first spawned lane's acquire parks. The primitive is
therefore thread-relative: either the spawned thread's acquire fast path
never succeeds (umtx/atomic semantics for a DARLING-created thread), or a
holder exists whose timing main never collides with. The stop-state dump
below is what discriminates these.

## The word: where it lives, and why the probe cannot read it

`do_dlsym` locks `rtld_bind_lock` — a pointer variable in ld-elf's BSS at
link offset **0x1fe20** (nm on the rtld debug file; the disassembly at the
call site: `lea 0x1fe20(%rip),%r13; mov 0x0(%r13),%rdi; call
rlock_acquire`). The lock word is the uint32 at the object the pointer
holds. `def_lock_acquire` semantics: 0 free, write-lock = cmpxchg to 1,
read-lock = add 2; the contended path is a umtx wait. (The previously
cited 0x20920 is NOT the lock — it is the acquire statistics counter.)

The probe gained `dump_bind_lock(tag)` — read `/proc/self/maps`, find
ld-elf, dereference the slot, print `word`. Measured negative: the guest
cannot read ANY absolute path outside the overlay — the trap vchroot
rewrites it (`/proc/self/maps` and a pid file under `/tmp` both fail with
ENOENT, printed as `[bindlock] ... maps-unreadable errno=2` in every run).
A word read from inside the guest is structurally impossible on this
path.

## Host-side word read (designed, scripted, not yet fired)

The watcher resolves the real mldr pid after `[step 01]`, and at the
`DID-NOT-RETURN` moment can read the word without any ptrace or dtrace:

```
base = first ld-elf.so.1 mapping start from /proc/<pid>/maps
slot = base + 0x1fe20
lockobj = 8 bytes at slot  (/proc/<pid>/mem, root)
word  = 4 bytes at lockobj
```

`procstat -kk <pid>` at the same moment names each thread's kernel stack —
a thread parked in the umtx wait shows it, and the word value (1 vs 2k)
tells write-locked vs N readers. The scripted watcher did not fire in
these runs: the whole probe run lasts ~25 s with hot caches, and the
watcher's ps-scan/gate chain never resolved a live pid in that window
(the dump file stayed empty while the run completed). The recipe is
correct; the trigger needs to run INSIDE the window — e.g. a tight
`while :` ps-scan with no sleeps, or resolving the pid the moment the
harness prints `Running:` (the mldr appears milliseconds later).

## Verdict

- Order: **(a) parks as the first lane — the holder is not a leftover;
  the property belongs to the chain/session** (three measured runs).
- Word value at the park: not captured; the read recipe and the stop-state
  discriminator are named above, with the measured reason the guest-side
  read is impossible.
- Control (c): untouched — no library instrumentation changed in this
  slice; the probe changes are diagnostics only.
