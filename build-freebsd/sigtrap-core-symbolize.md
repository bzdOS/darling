# r21/r22 SIGTRAP core symbolization

## Scope
- Host-only analysis, no VM, no fix, no sudo, no live mldr attach.
- Core: `$DARLING_BUILD_DIR/cores/mldr.core` (readable: `-rw-r----- root:fleet 81133568`, mtime `2026-11-23 03:44:32 UTC`).
- Logs: `$DARLING_BUILD_DIR/chrome-sr-r1.log`, `chrome-sr-r21.log`, `chrome-sr-r22.log`, `chrome-sr-r23.log`.
- mldr binary for symbols: `$DARLING_BUILD_DIR/dserver/mldr-real/mldr`.

## Step 1 — Attribution

### Core identity
Commands:
```sh
ls -la $DARLING_BUILD_DIR/cores/mldr.core
stat -f 'mode=%Mp%Lp uid=%u gid=%g size=%z mtime=%Sm' -t '%Y-%m-%d %H:%M:%S %Z' $DARLING_BUILD_DIR/cores/mldr.core
file $DARLING_BUILD_DIR/cores/mldr.core
strings -a $DARLING_BUILD_DIR/cores/mldr.core | head -30
```

Output:
```text
-rw-r-----  1 root  fleet  81133568 Nov 23 03:44 $DARLING_BUILD_DIR/cores/mldr.core
mode=0640 uid=0 gid=1002 size=81133568 mtime=2026-11-23 03:44:32 UTC
$DARLING_BUILD_DIR/cores/mldr.core: ELF 64-bit LSB core file, x86-64, version 1 (FreeBSD), FreeBSD-style, from '/Frameworks/Google Chrome for Testing Framework.framework/Versions/154.0.8029.0/', pid=13377
...
/tmp/darling-mldr-13377-647510
...
/tmp/darling-mldr-13377-647513
[traplog] sendmsg fd=647513 callnum=38 pid=13377 tid=249864 len=56
```

### Run/pid correlation
Commands:
```sh
cd $DARLING_BUILD_DIR
grep -n 'GUEST-EXIT\|HANGDUMP' chrome-sr-r21.log chrome-sr-r22.log chrome-sr-r23.log chrome-sr-r1.log
grep -n 'pid=13377\|guest_pid=13377' chrome-sr-r23.log | head
```

Output:
```text
chrome-sr-r21.log:25106:[launch] GUEST-EXIT pid=69205 status=0x5 t=16s
chrome-sr-r21.log:25110:HANGDUMP server=69188 guest=69119 at=260s
chrome-sr-r22.log:25108:[launch] GUEST-EXIT pid=88671 status=0x5 t=9s
chrome-sr-r22.log:25112:HANGDUMP server=88658 guest=88645 at=260s
chrome-sr-r23.log:54144:[launch] GUEST-EXIT pid=13270 status=0x4 t=22s
chrome-sr-r23.log:54147:HANGDUMP server=13257 guest=13152 at=260s
chrome-sr-r1.log:25111:HANGDUMP server=67649 guest=67182 at=261s
chrome-sr-r1.log:25424:[launch] GUEST-EXIT pid=67669 status=0x4 t=301s

chrome-sr-r23.log:54154:[darling-mldr] reentry: host_tid=249864 guest_pid=13377 rsp=0xfc325c4bc70
chrome-sr-r23.log:54159:[darling-mldr] reentry: host_tid=534053 guest_pid=13377 rsp=0xfc32617dc70
chrome-sr-r23.log:54162:  si_pid=13377 si_uid=0 si_value=0x0 sival_ptr=0x0
chrome-sr-r23.log:54432:[darling-mldr] reentry: host_tid=534056 guest_pid=13377 rsp=0xfc326258c70
```

Attribution verdict: core is from `pid=13377`, which appears in `chrome-sr-r23.log` as guest_pid/si_pid in the post-exit/hangdump abort region (`54154-54432`) after `GUEST-EXIT pid=13270 status=0x4 t=22s` (`54144`) and `HANGDUMP server=13257 guest=13152 at=260s` (`54147`). It is **not** r21 early SIGTRAP death (pid 69205, t=16s) and **not** r22 early SIGTRAP death (pid 88671, t=9s). Core mtime `03:44:32 UTC` is consistent with hangdump/post-exit capture, not 9s/16s early deaths.

## Step 2 — What the core contains

### LLDB symbolization
Command:
```sh
/usr/local/bin/lldb -c $DARLING_BUILD_DIR/cores/mldr.core $DARLING_BUILD_DIR/dserver/mldr-real/mldr \
  -o 'image list' -o 'thread list' -o 'bt all' -o 'quit'
```

Key output (excerpt):
```text
Core file '$DARLING_BUILD_DIR/cores/mldr.core' (x86_64) was loaded.
* thread #1: tid = 534053, 0x00000fc325bfcc2a, name = 'mldr', stop reason = signal SIGILL
  thread #2: tid = 249864, ... __sys_mincore ..., stop reason = signal SIGILL
  thread #3: tid = 534056, ... __sys_sigprocmask ..., stop reason = signal SIGILL

* thread #1 ... stop reason = signal SIGILL
  * frame #0: 0x00000fc325bfcc2a                  ; ud2, guest libsystem_kernel.dylib +0x28c2a class
    frame #1: 0x00000fc325bffebb
    frame #2: 0x000000000022359a mldr`mldr_sw_entry(p=<unavailable>) at freebsd_syscall_trap.c:484:5
    frame #3: 0x0000000000223528 mldr`mldr_call_guest_handler [inlined] mldr_run_on_stack(...) at freebsd_syscall_trap.c:495:5
    frame #4: 0x0000000000223517 mldr`mldr_call_guest_handler(..., fn=sat_guest_call_run, ...) at freebsd_syscall_trap.c:532:13
    frame #5: 0x000000000022f5a5 mldr`sat_guest_handler_trampoline(native_signo=19, ...) at sigaction_translate.c:445:5
    ...
    frame #9: 0x0000000000226635 mldr`sigill_handler [inlined] dispatch_linux_syscall(...) at freebsd_syscall_trap.c:3848:14
    frame #11: 0x0000000000226206 mldr`sigill_handler(signo=4, ...) at freebsd_syscall_trap.c:5105:5

  thread #3 ... stop reason = signal SIGILL
  * frame #0: 0x00000008240a93aa libsys.so.7`__sys_sigprocmask at _sigprocmask.S:4
    ...
    frame #14: 0x0000000000227250 mldr`crash_debug_handler(signo=6, info=..., uctx_void=...) at freebsd_syscall_trap.c:5064:9
    ...
    frame #20: 0x0000000000226206 mldr`sigill_handler(signo=4, ...) at freebsd_syscall_trap.c:5105:5
```

Thread 1 register/read:
```text
rdi=0x3441 rsi=0x6 rip=0x00000fc325bfcc2a rsp=rbp=0x00000fc32617dc70
memory 0xfc32617dc70: 0x00000fc32617dfa0 0x00000fc325bffebb ...
```

## Verdict

- **Attribution:** `mldr.core` belongs to r23 hangdump/post-exit guest pid `13377` (`file`/`strings`), correlated to `chrome-sr-r23.log:54144 GUEST-EXIT pid=13270 status=0x4 t=22s`, `54147 HANGDUMP server=13257 guest=13152 at=260s`, `54154/54159/54162/54432 guest_pid=13377`. It does **not** belong to r21 SIGTRAP early death (`chrome-sr-r21.log:25106 pid=69205 status=0x5 t=16s`) or r22 (`chrome-sr-r22.log:25108 pid=88671 status=0x5 t=9s`).
- **Early r21/r22 SIGTRAP death absent in core:** yes. This core cannot symbolize the r21/r22 `SIGTRAP-AUDIT-DELIVER si_code=65537` early-exit class (`chrome-sr-r21.log:25100-25106`, `chrome-sr-r22.log:25101-25108`).
- **What core can give (second front, hangdump 260s state):** it contains post-hangdump abort state for r23 guest pid 13377: thread 1 stopped at guest `ud2` `0xfc325bfcc2a` (libsystem_kernel +0x28c2a class) with mldr host frames through `sigill_handler/dispatch_linux_syscall/sat_guest_handler_trampoline` (`freebsd_syscall_trap.c:3848,484,495,532,5105`, `sigaction_translate.c:445`); thread 3 shows crash_debug_handler on signal 6 then sigill_handler signal 4 (`freebsd_syscall_trap.c:5064,5105`); thread 2 is in crash dump guarded read. Useful for hangdump/post-abort path, not for r21/r22 early SIGTRAP caller.
- **No live attach attempted** (impossible 3/3 per dispatch).

## Privacy greps
```sh
<privacy-grep-diff: canonical pattern, must be empty>
<privacy-grep-log: canonical pattern, must be empty>
```
Both must be empty before push.
