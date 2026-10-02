# trap-wedge-read: the park is consistent with an in-process exit teardown

Static reading of the trap path (no builds), per the order. Files:
`src/startup/mldr/freebsd_syscall_trap.c` (3151 lines),
`src/startup/mldr/elfcalls/elfcalls.c`, kernel `kern_proc.c` (present
under `/usr/src/sys` on this machine).

## The trap path, mapped

- `setup_macos_syscall_trap` (:3088) installs SIGSYS with
  `SA_SIGINFO|SA_ONSTACK` on ONE static 64 KiB altstack (:318-319) — the
  comment at :305-316 states it covers the INITIAL thread only;
  guest-created threads must self-register via
  `mldr_setup_thread_signal_stack` (:3056), whose failure prints a
  WARNING that "a raw-syscall trap on it will crash in signal
  delivery" (:3063) and leaves the thread WITHOUT an altstack.
- `sigsys_handler` (:2680-2860): recovers the syscall number by reading
  the faulting context's memory — `rip[-7..]` bytes and, for Mach traps,
  `*(mc->mc_rsp + 8)` (:2693-2695, :2727). Failure paths print via
  fprintf ON THE SHARED STATE and re-raise with SIG_DFL
  (:2697-2711, :2752-2763) — a thread that hits them dies; a thread that
  blocks inside the handler's fprintf (the stderr FILE lock, contended
  by the constant LD_DEBUG flood) waits in userspace-on-umtx.
- `sigill_handler` (:2890+): catches the ud2-patched raw-Linux
  trampolines; dispatches in-handler.
- The fd registries (timerfd :901-903, eventfd :922-927) are documented
  as "written from the syscall dispatch path, which runs inside the
  SIGILL handler" — and `mldr_eventfd_track` (:951-971) mutates the
  count with NO atomics: two threads trapping at once corrupt them.
- Exit paths: the trap maps guest exit to RAW `_exit` —
  `MACOS_SYS_exit` (:561-563) and `LINUX_SYS_exit/exit_group`
  (:2457-2462) call `SYS__exit` directly: instant death, no atexit, no
  `P_WEXIT`. Host `exit(3)` — the only call that sets `P_WEXIT` — is
  exported to the guest through elfcalls (`calls->exit = exit`,
  elfcalls.c).

## Syscall 99

Linux x86-64 table: 98 `getrusage`, **99 `times(2)`**, 100 `ptrace`.
`times` is not in the trap's handled set (:197-226), so it lands in the
raw-Linux fallback (:2742-2751) → `dispatch_linux_syscall` → the
"unhandled" print → ENOSYS. The probe is NOT the caller (its `now_ms`
uses `clock_gettime(CLOCK_MONOTONIC)` = 228, handled). Who calls it is
NOT answerable by reading: the ×4 ENOSYS cluster sits exactly at the
(a)-window start; the leading candidate is the guest's LINUX-built
libSystem/libthr emitting `times()` while the spawned thread starts.
Named for the logging slice.

## The ESRCH fact, explained from kern_proc.c

`pget()` returns ESRCH when `pfind` finds nothing (:533-542), when the
process carries `P_WEXIT` under a NOTWEXIT-style lookup (:555-558), or
when its vmspace is gone (:2425). procstat's lookups are the
NOTWEXIT family. So the measured flip — the process visible mid-run
(clean recvmsg stack) and ESRCH at the park while kvm-ps shows it alive
and non-defunct — means **the process carried `P_WEXIT` at the park**:
someone in-process called host `exit(3)` (the trap's own exit path
would have used raw `_exit` and killed it instantly). The process then
LIVED through variants (d), (c) and the verdict, and the log ends in a
clean `rtld_exit → lm_fini → _exit` teardown — i.e. the exit was IN
PROGRESS and hanging.

## The wedge, named

The park is therefore best explained as **a mutual wait inside an
in-progress process exit**: the exit teardown runs destructors and
tears down rtld structures (holding the very locks and objects the
bridge chain needs), while the (a) lane sits inside
`__lazy → elfcalls->dlsym_fatal → host dlsym` waiting on those
structures. The lane never reaches the native wrapper (the measured
marker silence), the process is hidden from kern.proc (the measured
ESRCH), and everything else keeps running until the teardown finally
completes. A plain rtld lock-word wait is ruled out by the ESRCH
itself: a clean `_umtx_op` wait keeps the process snapshot-visible (the
mid-run recvmsg stack proves that shape is visible).

The trap-side amplifiers that can START this (named, not proven):
a trap on a thread whose altstack registration failed (:3063 WARNING
path — absent from the measured runs), the non-atomic registries
(:951-971) under concurrent traps, the handler's stderr-lock hazard
(:2699, :2754), and the stack dereference at a faulting rsp (:2727).

## Logging-slice spots (permission for logging-only mldr edits to be
requested separately)

1. elfcalls `calls->exit` — log every invocation with tid: this names
   the exit(3) caller the ESRCH evidence requires.
2. `sigsys_handler` / `sigill_handler` entry AND exit with tid — names
   which thread traps, and whether the handler ever returns.
3. `mldr_setup_thread_signal_stack` result per thread (tid).
4. elfcalls `dlsym_fatal` entry/return with tid (elfcalls.c:33-42).
5. `dispatch_linux_syscall` unhandled path with the raw number and tid
   — names syscall 99's caller.
