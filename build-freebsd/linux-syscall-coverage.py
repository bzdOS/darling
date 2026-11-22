#!/usr/bin/env python3
"""Generate linux-syscall-coverage.xml: which Linux x86_64 syscalls mldr handles.

Usage: linux-syscall-coverage.py [--src FILE] [--logs DIR] [--out FILE]
Handled numbers come from the dispatch switch in freebsd_syscall_trap.c;
enosys-seen from "unhandled Linux syscall N" lines in logs; chrome="yes"
when such a line is in a log whose name contains "chrome".
"""
import argparse, collections, glob, os, re
from xml.sax.saxutils import quoteattr

NAMES = """read write open close stat fstat lstat poll lseek mmap mprotect munmap brk rt_sigaction
rt_sigprocmask rt_sigreturn ioctl pread64 pwrite64 readv writev access pipe select sched_yield mremap
msync mincore madvise shmget shmat shmctl dup dup2 pause nanosleep getitimer alarm setitimer getpid
sendfile socket connect accept sendto recvfrom sendmsg recvmsg shutdown bind listen getsockname
getpeername socketpair setsockopt getsockopt clone fork vfork execve exit wait4 kill uname semget
semop semctl shmdt msgget msgsnd msgrcv msgctl fcntl flock fsync fdatasync truncate ftruncate
getdents getcwd chdir fchdir rename mkdir rmdir creat link unlink symlink readlink chmod fchmod chown
fchown lchown umask gettimeofday getrlimit getrusage sysinfo times ptrace getuid syslog getgid setuid
setgid geteuid getegid setpgid getppid getpgrp setsid setreuid setregid getgroups setgroups setresuid
getresuid setresgid getresgid getpgid setfsuid setfsgid getsid capget capset rt_sigpending
rt_sigtimedwait rt_sigqueueinfo rt_sigsuspend sigaltstack utime mknod uselib personality ustat statfs
fstatfs sysfs getpriority setpriority sched_setparam sched_getparam sched_setscheduler
sched_getscheduler sched_get_priority_max sched_get_priority_min sched_rr_get_interval mlock munlock
mlockall munlockall vhangup modify_ldt pivot_root _sysctl prctl arch_prctl adjtimex setrlimit chroot
sync acct settimeofday mount umount2 swapon swapoff reboot sethostname setdomainname iopl ioperm
create_module init_module delete_module get_kernel_syms query_module quotactl nfsservctl getpmsg
putpmsg afs_syscall tuxcall security gettid readahead setxattr lsetxattr fsetxattr getxattr lgetxattr
fgetxattr listxattr llistxattr flistxattr removexattr lremovexattr fremovexattr tkill time futex
sched_setaffinity sched_getaffinity set_thread_area io_setup io_destroy io_getevents io_submit
io_cancel get_thread_area lookup_dcookie epoll_create epoll_ctl_old epoll_wait_old remap_file_pages
getdents64 set_tid_address restart_syscall semtimedop fadvise64 timer_create timer_settime
timer_gettime timer_getoverrun timer_delete clock_settime clock_gettime clock_getres clock_nanosleep
exit_group epoll_wait epoll_ctl tgkill utimes vserver mbind set_mempolicy get_mempolicy mq_open
mq_unlink mq_timedsend mq_timedreceive mq_notify mq_getsetattr kexec_load waitid add_key request_key
keyctl ioprio_set ioprio_get inotify_init inotify_add_watch inotify_rm_watch migrate_pages openat
mkdirat mknodat fchownat futimesat newfstatat unlinkat renameat linkat symlinkat readlinkat fchmodat
faccessat pselect6 ppoll unshare set_robust_list get_robust_list splice tee sync_file_range vmsplice
move_pages utimensat epoll_pwait signalfd timerfd_create eventfd fallocate timerfd_settime
timerfd_gettime accept4 signalfd4 eventfd2 epoll_create1 dup3 pipe2 inotify_init1 preadv pwritev
rt_tgsigqueueinfo perf_event_open recvmmsg fanotify_init fanotify_mark prlimit64 name_to_handle_at
open_by_handle_at clock_adjtime syncfs sendmmsg setns getcpu process_vm_readv process_vm_writev kcmp
finit_module sched_setattr sched_getattr renameat2 seccomp getrandom memfd_create kexec_file_load bpf
execveat userfaultfd membarrier mlock2 copy_file_range preadv2 pwritev2 pkey_mprotect pkey_alloc
pkey_free statx io_pgetevents rseq""".split()
TABLE = {i: n for i, n in enumerate(NAMES)}
# 335..423 are unassigned on x86_64; 424+ are the common numbering.
for i, n in enumerate("""pidfd_send_signal io_uring_setup io_uring_enter io_uring_register open_tree
move_mount fsopen fsconfig fsmount fspick pidfd_open clone3 close_range openat2 pidfd_getfd faccessat2
process_madvise epoll_pwait2 mount_setattr quotactl_fd landlock_create_ruleset landlock_add_rule
landlock_restrict_self memfd_secret process_mrelease futex_waitv set_mempolicy_home_node cachestat
fchmodat2 map_shadow_stack futex_wake futex_wait futex_requeue""".split()):
    TABLE[424 + i] = n

def handled(src):
    t = open(src, errors="replace").read()
    defs = {m.group(1): int(m.group(2)) for m in
            re.finditer(r"#define\s+LINUX_SYS_(\w+)\s+(\d+)", t)}
    a = t.index("dispatch_linux_syscall_host_errno(unsigned")
    b = t.index("\ndispatch_linux_syscall(unsigned", a)
    body = t[a:b]
    nrs = set()
    for m in re.finditer(r"^\s{4}case\s+(LINUX_SYS_\w+|\d+)\s*:", body, re.M):
        v = m.group(1)
        nrs.add(int(v) if v.isdigit() else defs.get(v[10:]))
    nrs.discard(None)
    return nrs, defs

def seen(logdir):
    cnt = collections.Counter(); chrome = set()
    for f in sorted(glob.glob(os.path.join(logdir, "*.log"))):
        ch = "chrome" in os.path.basename(f).lower()
        try:
            for line in open(f, errors="replace"):
                m = re.search(r"unhandled Linux syscall (\d+)", line)
                if m:
                    n = int(m.group(1)); cnt[n] += 1
                    if ch: chrome.add(n)
        except OSError:
            pass
    return cnt, chrome

def main():
    here = os.path.dirname(os.path.abspath(__file__))
    ap = argparse.ArgumentParser()
    ap.add_argument("--src", default=os.path.join(here, "..", "src/startup/mldr/freebsd_syscall_trap.c"))
    ap.add_argument("--logs", default="/opt/darling/build")
    ap.add_argument("--out", default=os.path.join(here, "linux-syscall-coverage.xml"))
    a = ap.parse_args()
    h, defs = handled(a.src)
    for n, v in defs.items(): TABLE.setdefault(v, n)
    cnt, chrome = seen(a.logs)
    allnr = sorted(set(TABLE) | h | set(cnt))
    out = ['<?xml version="1.0" encoding="UTF-8"?>', "<linux-syscall-coverage arch=\"x86_64\">"]
    for n in allnr:
        name = TABLE.get(n, "unknown")
        if n in h:
            st = "handled"
            reason = "ENOSYS seen in older logs, dispatched since" if n in cnt else ""
        elif n in cnt: st, reason = "enosys-seen", "unhandled Linux syscall line in logs"
        else: st, reason = "unhandled", "not dispatched, never seen in logs"
        c = "yes" if n in chrome else ("unknown" if st == "unhandled" else "no")
        out.append(f'  <syscall nr="{n}" name={quoteattr(name)} status="{st}" '
                   f'seen-count="{cnt.get(n,0)}" chrome="{c}" reason={quoteattr(reason)}/>')
    out.append("</linux-syscall-coverage>")
    open(a.out, "w").write("\n".join(out) + "\n")
    s = collections.Counter(re.findall(r'status="([^"]+)"', "\n".join(out)))
    print(dict(s))
main()
