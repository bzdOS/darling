# kernel-esrch: the ESRCH door is thread-side — the lane's process is
# findable, its thread enumeration is not

Lane kernel-esrch. Live naming of the failing kernel branch. The
revoke()-gate arms a host-side dtrace (fbt: `pget`, `p_cansee`,
`sysctl_kern_proc` — all three measured matchable on this kernel) at
the variant (a) window; a watcher fires `procstat -kk` on the parked
target five times and on a foreign live process three times, all
timestamped (kw-esrch-procstat.log).

## What the gate caught (kw-esrch-trace.log, 45 954 lines, ARMED=1)

The ESRCH reproduces live: all five `procstat -kk <target>` calls
print `procstat: sysctl(kern.proc): No such process` at
15:17:50.02–.03, while the foreign control (sway) prints its column
header with no error — the failure is a property of the target, not
of the observer.

On the kernel side, for exactly those five calls:

- `pget ENTER pid=<target> flags=0x2 curpid=<procstat>` — the pidhash
  LOOKUP FIRED for the target: the process is findable;
- the flags argument is `0x2` — neither `P_WEXIT` (0x2000) nor
  `P_INEXEC` (0x04000000), so none of the three doors named in the
  earlier code read (pidhash miss / P_WEXIT / P_INEXEC) fired;
- **zero** failing returns anywhere in the window: no
  `sysctl_kern_proc RET rax=-`, no `p_cansee RET rax=-`, no pget
  failure — the traced handler path never returned an error.

## Verdict (one line, as the order asks)

The failing branch is NOT `pget`'s three doors — the lane's process
is alive and findable at the park; the ESRCH the observer sees comes
from a door below the traced triple, the thread-list variant of the
by-pid walk (`KERN_PROC_INC_THREAD`), i.e. the observer can see the
process but will not enumerate its threads — the same non-enumerability
the kvm snapshot measured from the other side (the lane's lwp absent
from the thread listing).

## Consequence for the park question

The lane is not "invisible to the kernel" — its process is listed and
its flags are normal; what the enumeration refuses is its THREADS.
That narrows the park state: the lane's thread is wedged in a state
the thread-enumeration path skips, while the process itself keeps a
normal face — consistent with a thread trapped in a signal/exception
handling context (the earliest slices' suspect) rather than a process
in teardown.

## Method notes

- The guest's own `revoke(2)` never reached the host kernel (the
  Linux-revoke trap translation returns without issuing the host
  syscall), so the gate was fired host-side by the watcher at the same
  window point — same arm semantics, measured working.
- fbt `:return` arg0 on this kernel is not reliably the return value
  (prints as the leftover arg state); the door verdict rests on the
  ENTER arguments and on the ABSENCE of any negative return, which is
  direction-safe.
