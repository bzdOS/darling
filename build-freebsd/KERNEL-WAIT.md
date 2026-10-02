# kernel-wait: the silent lane has NO kernel-visible thread state —
# its lwp is not enumerable at the park

Lane kernel-wait. What the kernel says about the silent
(a) lane, captured at the park with timestamps in the dumps
(`wl-body-kw{1,2}.txt`).

## The capture, ×2 identical configuration

The watcher resolves the real mldr pid tight once the harness prints
`Running:` and, at the first `DID-NOT-RETURN`, dumps:

- `ps -axo pid,tid,wchan,comm -H` — the kvm thread listing with wait
  channels (the route that works at the park: the per-pid
  `sysctl(kern.proc)` door measured ESRCH there in the earlier slice);
- the lane's row by its host lwp (taken from its own `altstack OK`
  marker);
- `procstat -a -kk` (all-processes route) and a raw `kern.proc`
  listing.

Run 1: lane lwp **194851**; run 2: lane lwp **195029**. In BOTH runs
the lane's row is **ABSENT from the kvm thread listing** — no wchan,
no frame, no thread entry at all — while the process's main-thread row
is present (`74904 105220 - mldr`, wchan `-`, i.e. no kernel wait).
The control threads are present and healthy in the same snapshots:
the darlingserver's threads wait in `select`/`uwait` (normal server
waiters), the unrelated host threads in `kqread`/`uwait`.

## Verdict

**The silent lane sleeps on nothing the kernel will show: its lwp is
not enumerable at the park.** "The stack is empty" in the strongest
measured form — not truncated, not unknown-wchan: the thread is not
in the thread listing at all. A lane blocked in fd-read, a condvar, a
umtx or a mach-bridge would be LISTED with a wchan (the mid-run
control of the earlier slice showed exactly that shape: mldr's main
thread listed in `sys_recvmsg` with a full frame). The measured
absence — invisible to kvm's thread enumeration and (earlier slice)
to `kern.proc` per-pid — is the signature of a thread wedged in a
state the enumeration does not expose: the signal/trap handling
machinery context the earliest slices suspected (the ud2/SIGSYS
patch path on a guest-created thread), not a userspace wait.

The deep park did not fall out in either run (the ×3 distribution
already showed the shallow form dominating this configuration); the
verdict holds for the shallow (dominant) form measured here.

## Next

The remaining discriminator between "off the enumeration" and "in a
trap context" needs the one observer that walks the thread list
without the enumeration gates: dtrace **fbt** on the kernel side
(proven working) tracing the thread-walk functions during the park,
or — with the head's separate authorization — an in-process signal
state dump. Both are named, neither was spent here.
