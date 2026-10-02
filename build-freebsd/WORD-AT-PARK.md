# word at park: the process vanishes from kern.proc exactly at the park

Question: what is the rtld bind-lock word at the variant-(a) park, and
does the stop-state discriminate free-fail / write-locked / readers?

## What was measured (event-driven watcher, no timers)

The watcher resolves the real mldr pid from `ps` when the harness prints
`Running:` (mldr appears milliseconds later), and acts at the log's first
`DID-NOT-RETURN`. Two runs:

**Mid-run check (right after mldr exec, guest init): procstat WORKS.**

```
=== mid-run check: pid=337 ===
  337 101284 mldr   mi_switch sleepq_catch_signals sleepq_wait_sig _sleep
                    sbwait uipc_soreceive_dgram soreceive kern_recvit
                    sys_recvmsg amd64_syscall
  337   334 mldr
 6952   337 darlingserver
```

The main thread sits in `recvmsg` on the darlingserver RPC socket — a
clean syscall wait, fully snapshot-visible.

**At the park (first `DID-NOT-RETURN`): procstat FAILS — ESRCH — while
`ps` shows the same pid alive.**

```
=== park check: REAL=337 R2=337 ===
  337   334 mldr          <- alive, not defunct (ps, kvm view)
 6952   337 darlingserver
procstat: sysctl(kern.proc): No such process
ls: /proc/337: No such file or directory
```

Same pid, seconds apart: visible mid-run, invisible at the park, and
still ps-visible. Control: `procstat -kk` on other live processes works
on this kernel (the watcher's own shell returns a full kernel stack).

## The word: four measured dead ends

| route                       | result at the park                          |
|-----------------------------|---------------------------------------------|
| truss (ptrace)              | kills the guest pre-step (earlier slice)     |
| dtrace pid/syscall probes   | stalls the syscall-dense harness (measured) |
| `/proc/<pid>/mem` (+maps)   | `/proc/<pid>` never exists for mldr (procfs shows no per-pid dirs at all here) |
| `procstat` / `kern.proc`    | works mid-run, **ESRCH at the park**        |

The guest-side probe read is separately impossible: the trap vchroot
rewrites every absolute path into the overlay (`/proc/self/maps` ENOENT,
measured in every run). The only remaining observer of the word in the
window is the mldr process itself, which the boundaries forbid touching.

## What the hiding itself says (refinement of the primitive)

A thread parked in a plain lock wait blocks in a syscall (`_umtx_op`) and
keeps the process snapshot-visible — the mid-run `recvmsg` wait above is
exactly that shape. The (a) park instead makes the whole process vanish
from `kern.proc` while remaining ps-visible. That is not the signature of
an rtld lock-word wait: it points at a thread wedged in a NON-SYSCALL
state — inside the SIGSYS trap machinery or the raw-syscall `ud2` patch
region, mid-signal (sigfastblock) — where the kernel's process snapshot
cannot complete. The rtld lock-word hypothesis from the elimination
reading is therefore weakened; the trap-side wedge is the candidate the
measurement now favors.

## Verdict

- Word at the park: **not captured** — every external route measured
  dead, with the sharpest fact being the mid-run-vs-park sysctl
  contrast.
- Discriminator: the hiding behavior itself separates the candidates —
  a clean lock wait would be visible; the observed ESRCH favors a
  trap/signal-context wedge over the rtld lock.
- Next instrument that can see it: none external on this kernel within
  the boundaries; the in-process observer (mldr's own logging around
  the elfcalls dlopen path) is loader territory and out of scope here.
- Control (c): untouched.
