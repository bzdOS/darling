# INIT-HANG-CLASS - "hang at the Chrome framework initializer" is a dead guest, a waiting server

Control #142 step 7. Repeats the Control #59-2 / #60 probe recipe (as in
STAGED-KERNEL-SWAP.md) and snapshots ps + procstat -k while the run is
quiescent at the initializer. It answers the step-6 question: the run does not
hang IN the initializer -- the guest process (mldr) dies there, and the outer
run looks hung because darlingserver waits on the dead guest forever.

## Runs (two; both logs are NEW names, old logs untouched)

  Run A  /tmp/foundation-probe-158-init-hang.log   (1292690 lines, 99860040 B)
         Overlay state at start: FAT kernel (UUID ADA05CF3-ACFD-380B-8C1A-3444BAB7B1A4,
         x86_64+i386) -- the previous cycle restored the fat backup after step 6.
         Deviation from the recipe, but informative: see "(1a)".
  Run B  /tmp/foundation-probe-158b-init-hang.log  (1292537 lines, 99851064 B)
         Thin x86_64 kernel planted per recipe (Control #25 route), UUID
         E172B900-59DC-36AE-92BE-22DF5FB63A93 == the lx artifact, loaded:
         confirmed in the log ("dyld: loaded: <E172B900-...>").
         This is the faithful recipe-157 repeat.

  Recipe: STAGED-KERNEL-SWAP.md "Repro", two changes for the snapshot window:
  timeout 900 instead of 180, and the run backgrounded with the launcher pid in
  /tmp/foundation-probe-158{,b}-init-hang.marker. In both runs the Chrome
  framework initializer line appeared (25 s / 55 s after start), then the log
  size was flat over 30 s before any snapshot or kill was taken.

## (0) pids + procstat -k at the quiescent moment (run B, thin kernel)

  $ ps -p 467,646 -o pid,ppid,stat,wchan,args
    PID  PPID STAT WCHAN  COMMAND
    467 98999   IN select darlingserver /tmp/darling-dynamic-smoke 0 0 4 0
    646   467   ZN -      <defunct>

mldr (646) is a ZOMBIE (ZN, defunct): the guest has already exited; the kernel
stack is empty (procstat -k prints only the header -- a zombie has no threads).
Run A is the same shape: 67047 mldr ZN, 67023 darlingserver IN select.

  $ sudo procstat -k 467    # darlingserver, 3 threads
  467 101124 darlingserver - mi_switch sleepq_catch_signals sleepq_wait_sig _cv_wait_sig seltdwait kern_poll_kfds kern_poll sys_ppoll amd64_syscall fast_syscall_common
  467 197760 darlingserver - mi_switch sleepq_catch_signals sleepq_wait_sig _sleep umtxq_sleep do_wait __umtx_op_wait_uint_private sys__umtx_op amd64_syscall fast_syscall_common
  467 197761 darlingserver - mi_switch sleepq_catch_signals sleepq_wait_sig _sleep umtxq_sleep do_wait __umtx_op_wait_uint_private sys__umtx_op amd64_syscall fast_syscall_common

  $ sudo procstat -f 467    # what ppoll waits on
  fd 0 /dev/null; fd 1,2 /tmp/foundation-probe-158b-init-hang.log;
  fd 3 UDD /tmp/darling-dynamic-smoke/.darlingserver.sock;
  fd 5,6 unnamed UDS; fd 8,9 kqueue.

Server-side userspace frame not captured (lldb attach is closed by owner
decision); the kernel stacks above are the measured wait.

## (1) The hang class: guest death by signal; server waits on the dead guest

dmesg (kernel's own record), mldr deaths per run:

  pid 4357  (mldr): exited on signal 11   # run 157, thin kernel
  pid 13061 (mldr): exited on signal 11   # 158-hang (previous cycle), thin kernel
  pid 99004 (mldr): exited on signal 11   # run B, thin kernel
  pid 66439 (mldr): exited on signal 4    # run A, FAT kernel

With the thin kernel the log ends at the initializer line with NO crash dump
(grep -c 'genuine SIGILL|FATAL signal' -> 0 in 157, 158-hang and 158b), yet
dmesg says the guest was killed by SIGSEGV. HYPO: with the rebuilt kernel the
death happens where mldr's own signal handler cannot report it (a SEGV inside
the handler or before it is armed); not measured.

(1a) Run A (fat kernel) DID produce the full dump
(/tmp/foundation-probe-158-init-hang.log:1292564-1292605):

  [darling-mldr] genuine SIGILL (ud2, not a patched trampoline) at libsystem_platform.dylib+0x8237 rip=0xc5dfc8c3237 rax=0x1
  [darling-mldr] lock word @0xc5e0cc9a7a0: 0000000100000307 ... (ok=1111 rdi=0x307)
  [darling-mldr] reentry: host_tid=103260 guest_pid=66439 rsp=0x7fffffdfd680
  [darling-mldr] FATAL signal 4 (code=5) at addr=0xc5dfc8c3237
  backtrace: #00 crash_debug_handler+0xc3 (mldr) #01 sigill_handler+0x3f6 (mldr)
             #02 _pthread_sigmask+0x50a (libthr) #03 pthread_signals_unblock_np+0x5bb (libthr)

Offset resolution against the overlay libsystem_platform.dylib (nm -n):

  0x2480 t __os_unfair_lock_lock_slow         -> crash +0x257a = +0xfa
  0x2750 T _os_unfair_lock_lock_with_options  -> crash +0x2814 = +0xc4
  0x8230 T __os_unfair_lock_recursive_abort   -> rip +0x8237 = +7 = the ud2

i.e. the same thread re-locks an os_unfair_lock whose owner field (0x307)
already equals rdi: __os_unfair_lock_recursive_abort fires its ud2.

Class, one line: guest death by signal at the initializer (thin kernel:
SIGSEGV, dmesg, no dump; fat kernel: SIGILL/ud2 in
__os_unfair_lock_recursive_abort, dump quoted above); outer "hang" =
darlingserver main thread in sys_ppoll on kqueue + .darlingserver.sock, two
threads in __umtx_op_wait_uint_private (umtx wait), mldr left as an unreaped
zombie.

## Difference from step 6

Step 6 (log 157) called the abort GONE with grep 'os_unfair_lock.*abort|0x64d020|
0x8237|SIGABRT|signal 6|signal 11' -> 0. That grep saw no dump because the thin-
kernel run writes none; the guest nevertheless died the same way (dmesg
"pid 4357 (mldr) exited on signal 11"), and the mldr zombie measured on
2026-10-08 21:04 (pid 4771) is the same end state. Step 7 refutes "hangs in the
initializer": it dies there.

## Cleanup

Leftover servers from both runs killed by exact pid after identity print
(sudo kill 67023, sudo kill 467); both gone afterwards (ps -> empty).

## Repro

  cp $DARLING_BUILD_DIR/src/external/xnu/darling/src/libsystem_kernel/libsystem_kernel.dylib.x86_64 \
     $DARLING_OVERLAY/usr/lib/system/libsystem_kernel.dylib
  sh /tmp/init-hang-158b-snap.sh        # recipe 157 + marker + snapshot at quiescence
  dmesg | grep '(mldr)'                 # -> "exited on signal 11"
  ps -axo pid,ppid,stat,wchan,comm | grep -E 'mldr|darlingserver'   # -> ZN / IN select
  sudo procstat -k <darlingserver-pid>  # -> sys_ppoll + 2x __umtx_op_wait_uint_private
