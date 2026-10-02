# wchan-kvm: process topology of the probe (measured ground truth)

The probe's live guest is the process `launch-dynamic` exec's as mldr.
mldr runs the target **in-process** (it jumps to the target entry; no
fork) and forks darlingserver as its child. Measured at `[step 01]`
with `ps -axo pid,ppid,stat,lstart,comm,args`:

```
<guest> <timeout>  RN  ... mldr          .../mldr-real/mldr .../guest-wl-session-roundtrip-macho
<ds>    <guest>    RN  ... darlingserver darlingserver /tmp/darling-dynamic-smoke 0 0 4 0
<lg>    <ds>       ZN  ... mldr          <defunct>
```

So the fresh mldr that runs the probe and parks the lane is
darlingserver's **parent**, not its child. darlingserver's only child is
the launchd-global (`spawnLaunchd`: `mldr vchroot ... /sbin/launchd`),
which defuncts early in this harness. A ppid-walk
`darlingserver -> children` therefore resolves the zombie, not the
target.

The reliable resolution is the reverse: the fresh darlingserver by
`ps args` + freshest `lstart` -> its **parent** -> the mldr. Validate
with `ps -p PID -o lstart,comm` non-empty at the snapshot moment. The
guest declares its host pid at `start: pid=` (it matches the ps pid).
