/*
 * freebsd_syscall_trap.c — macOS BSD syscall interception on FreeBSD via SIGSYS.
 *
 * purpose:    Intercept macOS-ABI `syscall` instructions (eax = 0x2000000 | nr)
 *             that the Mach-O binary emits and translate them to FreeBSD native
 *             syscalls, executed in-handler.
 *
 * input:      (none — called once from mldr before start_thread())
 * output:     (none)
 * sideEffects:
 *   - Installs a SIGSYS sigaction(SA_SIGINFO | SA_ONSTACK).
 *   - Allocates and activates an alternate signal stack (64 KiB).
 *   - All macOS BSD syscalls not in the dispatch table return ENOSYS in rax/x0.
 *
 * Architecture: x86_64 + aarch64 supported.
 *
 *   aarch64 macOS syscall ABI (XNU/iOS/macOS):
 *     x16 = 0x2000000 | syscall_number  (BSD class)
 *         = negative 32-bit value         (Mach trap — stored in low 32 bits of x16)
 *     x0..x5 = args 1-6
 *     svc #0x80  — the trap instruction (NOT svc #0 which is Linux)
 *
 *   FreeBSD aarch64 SIGSYS delivery:
 *     mc_gpregs.gp_elr points PAST the svc instruction (already advanced).
 *     mc_gpregs.gp_x[16] holds the original x16 — the macOS syscall class+number.
 *       (FreeBSD does NOT overwrite x16 with ENOSYS before signal delivery,
 *        unlike x86-64 where rax is clobbered.  x16 is intact.)
 *     mc_gpregs.gp_x[0..5] = args 1-6 (intact).
 *     Stack arg7: [sp+0] — no return-address slot on aarch64 (LR in register).
 *
 *   macOS error return (aarch64):
 *     CPSR carry flag (bit 29) set → x0 = positive errno.
 *     CPSR carry flag clear      → x0 = return value.
 *
 * Background:
 *   macOS BSD syscall ABI (x86-64):
 *     eax = 0x2000000 | syscall_number
 *     rdi = arg1, rsi = arg2, rdx = arg3, r10 = arg4, r8 = arg5, r9 = arg6
 *
 *   FreeBSD SIGSYS semantics differ from Linux seccomp SIGSYS:
 *     - On Linux (seccomp): mc_rip points AT the syscall instruction.
 *     - On FreeBSD:         mc_rip points PAST the syscall (already advanced).
 *     - On FreeBSD:         mc_rax is overwritten with ENOSYS before signal
 *                           delivery — the original syscall number is lost.
 *
 *   Recovery strategy (x86-64):
 *     The instruction sequence is always:
 *       b8 xx xx xx xx   (mov eax, imm32)   — 5 bytes
 *       0f 05            (syscall)           — 2 bytes
 *     mc_rip points past the syscall, so:
 *       rip[-2..rip-1] = 0f 05  (confirm it was a syscall)
 *       rip[-7]        = 0xb8   (mov eax, imm32 opcode)
 *       rip[-6..-3]    = imm32  (the original eax value)
 *
 *   Argument registers (rdi, rsi, rdx, r10, r8, r9) are preserved intact.
 *
 * Limitations (MVP):
 *   - Only ~25 common BSD syscalls are translated.
 *   - Mach traps (raw_eax = 0xFFFFFFxx, i.e. int32_t < 0, upper byte = 0xFF)
 *     are now dispatched to darlingserver via dserver_rpc_*() for the five
 *     bootstrap traps (mach_reply_port, thread/task/host_self, mach_msg).
 *   - No errno translation needed: FreeBSD and macOS share POSIX errno values
 *     for the syscalls handled here.
 *   - The instruction-byte scan assumes MOV EAX,imm32 immediately precedes
 *     the SYSCALL instruction.  This holds for all known static Mach-O
 *     binaries compiled with clang/LLVM; hand-crafted asm may differ.
 *   - mach_msg_trap (-31): the 7th argument (rcv_msg) is now read from the
 *     Mach-O caller's stack at mc_rsp+8 and passed correctly to the RPC.
 *   - mach_msg_overwrite (-32): the 7th argument (priority) is read from
 *     mc_rsp+8; the 8th argument (rcv_msg at mc_rsp+16) still defaults to
 *     msg (in-place receive).  Full 9-arg support is a future TODO.
 *
 * Third class (x86-64 only, #198): raw Linux-ABI syscalls from the upstream
 * dyld/libdyld.dylib overlay binary.
 *   The overlay binaries under artefacts/darling-overlay/ (confirmed via
 *   `strings` — paths under /home/runner/work/darling/darling/, symbols like
 *   _oflags_bsd_to_linux) are genuine upstream Darling CI builds compiled to
 *   run ON LINUX.  dyld itself makes raw `syscall` instructions using LINUX
 *   syscall numbers baked into its machine code before any dylib (including
 *   our libsystem_kernel shim) is loaded — this can't be fixed by porting
 *   source, since this specific already-compiled binary isn't rebuilt from
 *   this tree.  x86-64 argument registers (rdi,rsi,rdx,r10,r8,r9) are
 *   byte-identical between Linux and FreeBSD raw syscall ABI, so only the
 *   syscall *number* needs translating.  Detected as a third class: neither
 *   MACOS_BSD_CLASS (0x02xxxxxx) nor a Mach trap (0xFFxxxxxx), but a small
 *   plain integer (<1024) recovered by the same MOV EAX,imm32 byte-scan.
 *   MVP: only the syscalls dyld's early bootstrap plausibly needs are
 *   translated (see dispatch_linux_syscall); arch_prctl(ARCH_SET_FS) is
 *   bridged to FreeBSD's sysarch(AMD64_SET_FSBASE) since dyld needs a working
 *   %fs-relative TLS base before it can do much else.  clone/futex/brk are
 *   deliberately left ENOSYS — they need real semantic redesign (rfork/
 *   pthread, _umtx_op, no direct brk equivalent), not a number swap.
 *   NOT YET DYNAMICALLY VERIFIED: written from Linux x86-64 syscall ABI
 *   reference numbers + FreeBSD <sys/syscall.h>/<machine/sysarch.h>, without
 *   a live build+truss pass on 185 in this session (see hub #198 note).
 */

#ifdef DARLING_FREEBSD

#include "freebsd_syscall_trap.h"

#include <sys/types.h>
#include <stdbool.h>
#include <signal.h>
#include <ucontext.h>
#include <errno.h>
#include <unistd.h>
#include <sys/syscall.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h> /* getenv — MLDR_LOG_MAPPINGS, see mldr_log_exec_mapping */
#include <sys/user.h> /* struct kinfo_file — F_KINFO in mldr_log_exec_mapping */
#include <string.h>
#include <fcntl.h> /* AT_FDCWD / AT_SYMLINK_NOFOLLOW — #198 stat/lstat via fstatat */
#include <sys/stat.h> /* native struct stat — #198 stat/fstat/lstat translation */
#include <time.h> /* CLOCK_MONOTONIC — #198 clock_gettime translation */
#include <sys/mman.h> /* mprotect — #198 raw-syscall trampoline patching */
#include <dirent.h> /* native struct dirent — #198 getdents64 translation */
#include <sys/resource.h> /* struct rlimit, getrlimit/setrlimit — #198 prlimit64 translation */
#include <sys/event.h> /* kqueue — epoll_create1 stand-in, see LINUX_SYS_epoll_create1 */
#include <sys/umtx.h> /* _umtx_op — futex translation, see LINUX_SYS_futex */
#include <sys/mount.h> /* native struct statfs — statfs/fstatfs translation */
#include "sigaction_translate.h" /* mldr_sys_rt_sigaction — see LINUX_SYS_rt_sigaction */
#include "trap_log.h"            /* exit-caller logging slice — write(2) markers */
#include <darlingserver/rpc.h>

/* exit-caller logging slice: gate captured ONCE at trap setup (single
 * threaded, getenv is safe there) so the handlers can read it without
 * touching environ. DARLING_TRAP_LOG=1 enables; default off. */
int mldr_trap_log_enabled = 0;

/* survive-window slice: handler-side sub-gate (see trap_log.h). */
int mldr_trap_log_handlers = 1;

/* ── macOS syscall class constants ─────────────────────────────────────────── */

/* macOS syscall number encoding: class in upper byte, number in low 24 bits.
 * Class 2 = BSD (UNIX) syscalls: eax = 0x02000000 | nr
 * Class 0 = Mach traps: eax = negative 32-bit value (sign-extended from int32_t)
 *   Actual trap numbers: -26 (mach_reply_port) through -31 (mach_msg_trap) etc.
 *   When read from MOV EAX,imm32, these appear as large unsigned 32-bit values
 *   e.g. mach_reply_port = -26 = 0xFFFFFFE6u. */
#define MACOS_BSD_CLASS      0x02000000u
#define MACOS_BSD_CLASS_MASK 0xFF000000u

/* Extract the per-class syscall number */
#define MACOS_BSD_NR(eax)  ((unsigned int)((eax) & 0x00FFFFFFu))

/* ── macOS Mach trap numbers (from osfmk/mach/syscall_sw.h) ──────────────── */
/* These are the raw int32_t values; when stored in uint32_t from MOV EAX,imm32
 * they appear as 0xFFFFFFxx.  We cast raw_eax to int32_t to detect negative. */
#define MACH_TRAP_mach_reply_port      (-26)
#define MACH_TRAP_thread_self_trap     (-27)
#define MACH_TRAP_task_self_trap       (-28)
#define MACH_TRAP_host_self_trap       (-29)
#define MACH_TRAP_mach_msg_trap        (-31)
#define MACH_TRAP_mach_msg_overwrite   (-32)

/* ── macOS BSD syscall numbers (from macOS 13 <sys/syscall.h>) ────────────── */
#define MACOS_SYS_exit           1
#define MACOS_SYS_fork           2
#define MACOS_SYS_read           3
#define MACOS_SYS_write          4
#define MACOS_SYS_open           5
#define MACOS_SYS_close          6
#define MACOS_SYS_getpid        20
#define MACOS_SYS_getuid        24
#define MACOS_SYS_getgid        47
#define MACOS_SYS_access        33
#define MACOS_SYS_ioctl         54
#define MACOS_SYS_readv        120
#define MACOS_SYS_writev       121
#define MACOS_SYS_munmap        73
#define MACOS_SYS_mprotect      74
#define MACOS_SYS_fcntl         92
#define MACOS_SYS_dup           41
#define MACOS_SYS_dup2          90
#define MACOS_SYS_pipe          42
#define MACOS_SYS_lseek        199
#define MACOS_SYS_fstat        339
#define MACOS_SYS_mmap         197
#define MACOS_SYS_getpgrp       81
#define MACOS_SYS_setpgid       82
#define MACOS_SYS_getppid       39
#define MACOS_SYS_sigprocmask   48
#define MACOS_SYS_sysctl       202
#define MACOS_SYS_gettimeofday 116
#define MACOS_SYS_select        93
#define MACOS_SYS_socket        97
#define MACOS_SYS_connect       98
#define MACOS_SYS_send         101
#define MACOS_SYS_recv         102
#define MACOS_SYS_sendto       133
#define MACOS_SYS_bind         104
#define MACOS_SYS_listen       106
#define MACOS_SYS_accept        30

/* ── Linux x86-64 syscall numbers (from Linux's stable syscall ABI table,
 * arch/x86/entry/syscalls/syscall_64.tbl) — used only for #198's third
 * dispatch class (raw Linux-ABI syscalls from the upstream dyld overlay).
 * Kept to the syscalls dyld's early bootstrap plausibly needs; see the file
 * header comment for what's deliberately left untranslated and why. ────── */
#define LINUX_SYS_read           0
#define LINUX_SYS_write          1
#define LINUX_SYS_open           2
#define LINUX_SYS_close          3
#define LINUX_SYS_stat           4
#define LINUX_SYS_fstat          5
#define LINUX_SYS_lstat          6
#define LINUX_SYS_newfstatat   262
#define LINUX_SYS_lseek          8
#define LINUX_SYS_pread64       17
#define LINUX_SYS_pwrite64      18
#define LINUX_SYS_readlink      89
#define LINUX_SYS_clock_gettime 228
#define LINUX_SYS_openat       257
#define LINUX_SYS_mmap           9
#define LINUX_SYS_mprotect      10
#define LINUX_SYS_munmap        11
#define LINUX_SYS_rt_sigaction  13
#define LINUX_SYS_rt_sigprocmask 14
#define LINUX_SYS_ioctl         16
#define LINUX_SYS_access        21
#define LINUX_SYS_dup           32
#define LINUX_SYS_dup2          33
#define LINUX_SYS_getpid        39
/* 7 = poll on x86-64. The guest's poll(395) reaches the overlay's
 * sys_pselect_nocancel, which forwards here; a run showed exactly two
 * "unhandled Linux syscall 7" lines — the probe's main and spawned poll
 * legs. FreeBSD poll(2) has the same argument shape, so this is a plain
 * passthrough and the readiness half of the event path becomes measurable. */
#define LINUX_SYS_poll           7
#define LINUX_SYS_socket        41
#define LINUX_SYS_connect       42
#define LINUX_SYS_accept        43
#define LINUX_SYS_bind          49
#define LINUX_SYS_listen        50
/* 53, NOT 134: the overlay's own linux-x86_64.h says __NR_socketpair 53, and
 * a run proved it — the guest's socketpair drew "unhandled Linux syscall 53"
 * here while 134 never arrived. Mainline x86-64 numbering; the first patch of
 * this case used 134 from a table read and a measurement corrected it. */
#define LINUX_SYS_socketpair    53
#define LINUX_SYS_sendto        44
#define LINUX_SYS_recvfrom      45
#define LINUX_SYS_sendmsg       46
#define LINUX_SYS_recvmsg       47
#define LINUX_SYS_setsockopt    54
#define LINUX_SYS_fcntl         72
#define LINUX_SYS_eventfd      284
/* 213 is epoll_create (the one taking a size hint), NOT epoll_create1 — that
 * is 291. Both are handled: the hint is meaningless to kqueue either way. */
#define LINUX_SYS_epoll_create  213
#define LINUX_SYS_epoll_create1 291
#define LINUX_SYS_epoll_wait    232
#define LINUX_SYS_epoll_ctl     233
#define LINUX_SYS_fsync          74
#define LINUX_SYS_fdatasync      75
#define LINUX_SYS_ftruncate      77
#define LINUX_SYS_mkdirat       258
#define LINUX_SYS_fchmodat      268
#define LINUX_SYS_statfs        137
#define LINUX_SYS_fstatfs       138
#define LINUX_SYS_timerfd_create 283
#define LINUX_SYS_timerfd_settime 286
#define LINUX_SYS_timerfd_gettime 287
#define LINUX_SYS_futex        202
#define LINUX_SYS_exit          60
#define LINUX_SYS_wait4         61
#define LINUX_SYS_kill          62
#define LINUX_SYS_sigaltstack  131
#define LINUX_SYS_arch_prctl   158
#define LINUX_SYS_gettid       186
#define LINUX_SYS_exit_group   231
#define LINUX_SYS_getdents64   217
#define LINUX_SYS_gettimeofday 96
#define LINUX_SYS_prlimit64    302
#define LINUX_SYS_faccessat    269
#define LINUX_SYS_getcpu       309
#define LINUX_SYS_getrandom    318

/* Linux sigaltstack(2) ss_flags bit values (asm-generic/signal.h) — differ
 * from FreeBSD's SS_ONSTACK=0x1/SS_DISABLE=0x4, and Linux's struct
 * sigaltstack field ORDER (ss_sp, ss_flags, ss_size) also differs from
 * FreeBSD's stack_t (ss_sp, ss_size, ss_flags) — see LINUX_SYS_sigaltstack. */
#define LINUX_SS_ONSTACK 1
#define LINUX_SS_DISABLE 2

/* Linux open(2)/openat(2) flag bits that differ in VALUE from FreeBSD's
 * (uapi/asm-generic/fcntl.h) — access-mode bits (O_RDONLY/WRONLY/RDWR = 0/1/2)
 * are identical on both OSes and need no translation. */
#define LINUX_O_CREAT      0x040
#define LINUX_O_EXCL       0x080
#define LINUX_O_TRUNC      0x200
#define LINUX_O_APPEND     0x400
#define LINUX_O_NONBLOCK   0x800
#define LINUX_O_DIRECTORY 0x10000
#define LINUX_O_CLOEXEC   0x80000

/* Linux clockid_t values (uapi/linux/time.h) that differ from FreeBSD's
 * (sys/_clock_id.h) — CLOCK_REALTIME=0 coincides on both. */
#define LINUX_CLOCK_MONOTONIC 1

/* Linux arch_prctl(2) op codes (uapi/asm/prctl.h) */
#define LINUX_ARCH_SET_GS 0x1001
#define LINUX_ARCH_SET_FS 0x1002
#define LINUX_ARCH_GET_FS 0x1003
#define LINUX_ARCH_GET_GS 0x1004

/* ── alternate signal stack ────────────────────────────────────────────────── */

/*
 * The alternate signal stack registered by setup_macos_syscall_trap() covers
 * the initial thread only: sigaltstack(2) is per-thread state, and this single
 * static buffer could not be shared even if it were registered everywhere —
 * two threads taking a trap at once would run their handlers on the same
 * memory.
 *
 * Every SIGSYS/SIGILL trap mldr relies on is raised by ordinary guest code
 * (macOS `syscall` instructions and the ud2-patched raw-syscall trampolines),
 * so it can be raised on any thread. Threads that libpthread/libdispatch
 * create inside the guest go through darling_thread_entry, which must register
 * a stack of its own — see mldr_setup_thread_signal_stack.
 */
#define SIGSYS_ALTSTACK_SIZE (64 * 1024)
static char _sigsys_altstack[SIGSYS_ALTSTACK_SIZE] __attribute__((aligned(16)));

/* ── open(2) flag translation ──────────────────────────────────────────────── */

/*
 * purpose:  Convert Linux open(2) flag bits to their FreeBSD equivalents.
 *
 *           Only the access mode (O_RDONLY/O_WRONLY/O_RDWR, the low two bits)
 *           is shared between the two; every other bit sits somewhere else.
 *           Passing them through does not fail loudly — it silently requests
 *           something different. Linux O_CREAT (0x40) reads as FreeBSD O_ASYNC,
 *           so the file is simply not created and the open fails with ENOENT;
 *           Linux O_APPEND (0x400) reads as FreeBSD O_TRUNC, which quietly
 *           empties a file the caller meant to append to.
 * input:    linux_flags — the guest's flag word.
 * output:   the corresponding FreeBSD flag word.
 * sideEffects: none.
 */
static long
mldr_open_flags_linux_to_freebsd(long linux_flags)
{
    long freebsd_flags = linux_flags & 0x3; /* O_RDONLY/O_WRONLY/O_RDWR */

    if (linux_flags & LINUX_O_CREAT)     freebsd_flags |= O_CREAT;
    if (linux_flags & LINUX_O_EXCL)      freebsd_flags |= O_EXCL;
    if (linux_flags & LINUX_O_TRUNC)     freebsd_flags |= O_TRUNC;
    if (linux_flags & LINUX_O_APPEND)    freebsd_flags |= O_APPEND;
    if (linux_flags & LINUX_O_NONBLOCK)  freebsd_flags |= O_NONBLOCK;
    if (linux_flags & LINUX_O_DIRECTORY) freebsd_flags |= O_DIRECTORY;
    if (linux_flags & LINUX_O_CLOEXEC)   freebsd_flags |= O_CLOEXEC;

    return freebsd_flags;
}

/* ── signal number translation ─────────────────────────────────────────────── */

/*
 * purpose:  Map a Linux signal number to its FreeBSD equivalent.
 *
 *           The two agree only up to 15 and then diverge sharply: Linux
 *           SIGUSR1 is 10 where FreeBSD has SIGBUS, Linux SIGCHLD is 17 where
 *           FreeBSD has SIGSTOP, and so on. Anything that copies a signal
 *           number or a signal *mask* across the boundary without this ends up
 *           naming a different signal entirely.
 * input:    linux_signo — 1..64.
 * output:   the FreeBSD signal number, or 0 when there is no counterpart
 *           (Linux SIGSTKFLT and SIGPWR have none, and the realtime signals
 *           from 32 up do not exist on FreeBSD at all).
 * sideEffects: none.
 */
static int
mldr_linux_signo_to_freebsd(int linux_signo)
{
    /* Indexed by Linux signal number; 0 = no FreeBSD equivalent. */
    static const unsigned char map[32] = {
        [1]  = SIGHUP,  [2]  = SIGINT,  [3]  = SIGQUIT, [4]  = SIGILL,
        [5]  = SIGTRAP, [6]  = SIGABRT, [7]  = SIGBUS,  [8]  = SIGFPE,
        [9]  = SIGKILL, [10] = SIGUSR1, [11] = SIGSEGV, [12] = SIGUSR2,
        [13] = SIGPIPE, [14] = SIGALRM, [15] = SIGTERM,
        [16] = 0,            /* SIGSTKFLT — no FreeBSD counterpart */
        [17] = SIGCHLD, [18] = SIGCONT, [19] = SIGSTOP, [20] = SIGTSTP,
        [21] = SIGTTIN, [22] = SIGTTOU, [23] = SIGURG,  [24] = SIGXCPU,
        [25] = SIGXFSZ, [26] = SIGVTALRM, [27] = SIGPROF, [28] = SIGWINCH,
        [29] = SIGIO,
        [30] = 0,            /* SIGPWR — no FreeBSD counterpart */
        [31] = SIGSYS,
    };

    if (linux_signo < 1 || linux_signo >= (int)(sizeof(map) / sizeof(map[0])))
        return 0;

    return (int)map[linux_signo];
}

/* ── raw FreeBSD syscall helper ────────────────────────────────────────────── */

/*
 * purpose:  Execute a FreeBSD syscall with up to 6 arguments.
 *           Returns the raw kernel value on success (>= 0) or -errno on error.
 * input:    nr — FreeBSD syscall number; a1..a6 — arguments
 * output:   kernel retval or -errno
 * sideEffects: performs the requested kernel operation
 */
static long
freebsd_raw_syscall(long nr, long a1, long a2, long a3,
                    long a4, long a5, long a6)
{
    long ret;
#if defined(__x86_64__)
    /* FreeBSD x86-64 ABI: carry flag set on error, rax = errno.
     * We negate on carry so caller sees -errno consistently. */
    register long r10 __asm__("r10") = a4;
    register long r8  __asm__("r8")  = a5;
    register long r9  __asm__("r9")  = a6;
    __asm__ volatile (
        "syscall\n\t"
        "jnc 1f\n\t"
        "neg %0\n\t"
        "1:"
        : "=a"(ret)
        : "0"(nr), "D"(a1), "S"(a2), "d"(a3), "r"(r10), "r"(r8), "r"(r9)
        : "rcx", "r11", "memory"
    );
#elif defined(__aarch64__)
    /* FreeBSD aarch64 syscall ABI:
     *   x8 = syscall number, svc #0
     *   x0..x5 = args; carry flag set on error (x0 = errno).
     * We negate on error so caller always sees -errno consistently. */
    register long _nr  __asm__("x8")  = nr;
    register long _a1  __asm__("x0")  = a1;
    register long _a2  __asm__("x1")  = a2;
    register long _a3  __asm__("x2")  = a3;
    register long _a4  __asm__("x3")  = a4;
    register long _a5  __asm__("x4")  = a5;
    register long _a6  __asm__("x5")  = a6;
    __asm__ volatile (
        "svc #0\n\t"
        "b.cc 1f\n\t"   /* branch if carry clear = success */
        "neg %0, %0\n\t" /* carry set = error: negate to -errno */
        "1:"
        : "=r"(_a1)
        : "r"(_nr), "0"(_a1), "r"(_a2), "r"(_a3), "r"(_a4), "r"(_a5), "r"(_a6)
        : "memory"
    );
    ret = _a1;
#else
    #error freebsd_syscall_trap.c: unsupported architecture
#endif
    return ret;
}

/* ── instruction-byte recovery of original eax (x86-64) ───────────────────── */

#if defined(__x86_64__)
/*
 * purpose:  Recover the original eax value (macOS syscall number) from the
 *           instruction bytes immediately before the syscall opcode.
 *
 *           On FreeBSD, mc_rip points past the `syscall` instruction when
 *           SIGSYS is delivered, and mc_rax has been overwritten with ENOSYS.
 *           We scan backward from rip for:
 *             rip[-7-N] = 0xb8          (MOV EAX, imm32)
 *             rip[-6-N..rip-3-N] = imm32 (the macOS syscall class+number)
 *             rip[-2-N..rip-1-N] = padding (N bytes; NOP/alignment/REX)
 *             rip[-2..-1] = 0f 05       (SYSCALL opcode)
 *           where N is 0..32 (handles NOP alignment pads in dyld stubs).
 *           The recovered imm32 must have class byte 0x02 (BSD) or 0xFF (Mach)
 *           to distinguish from false 0xB8 hits in instruction operands.
 *
 * input:    rip_ptr — mc_rip as a byte pointer (points past SYSCALL)
 * output:   recovered eax value, or 0 if the pattern is not found
 * sideEffects: none (read-only access to faulting instruction bytes)
 */
static uint32_t
recover_macos_syscall_nr(const uint8_t *rip_ptr, const uint8_t *rsp_ptr)
{
    /* Confirm bytes[-2..-1] are the SYSCALL opcode (0F 05) */
    if (rip_ptr[-2] != 0x0f || rip_ptr[-1] != 0x05) {
        return 0;
    }

#define SCAN_MAX 32

    /* Scan backward up to 32 bytes for MOV EAX, imm32 (0xB8 followed by 4
     * immediate bytes) with N bytes of padding between the end of MOV and the
     * start of SYSCALL.  For N=0 this is the standard 7-byte stub pattern.
     *
     * Layout for given N:
     *   rip[-7-N] = 0xB8           (MOV EAX opcode)
     *   rip[-6-N..-3-N] = imm32    (macOS syscall number)
     *   rip[-2-N..-1-N] = padding  (N bytes; NOP/alignment/REX prefixes)
     *   rip[-2..-1] = 0F 05        (SYSCALL — already confirmed above)
     *
     * We gate on the class byte (imm32 >> 24) being 0x02 (BSD) or 0xFF (Mach)
     * to reject false 0xB8 hits from unrelated instruction operands. */
    for (int n = 0; n <= SCAN_MAX; n++) {
        const uint8_t *p = rip_ptr - 7 - n;
        if (*p == 0xb8) {
            uint32_t nr;
            memcpy(&nr, p + 1, 4);
            uint8_t cls = (uint8_t)(nr >> 24);
            /* #198: also accept a plain small integer (no class prefix) —
             * a raw Linux-ABI syscall number from the upstream dyld overlay.
             * nr!=0 excludes zero-byte padding false-hits. */
            if (cls == 0x02 || cls == 0xFF || (cls == 0x00 && nr != 0 && nr < 1024)) {
                return nr;
            }
        }
    }

    /* Fallback: generic/variadic syscall wrappers (e.g. libSystem's
     * `syscall(2)`, which dyld's own bootstrap uses) take the macOS syscall
     * number as a caller-supplied RUNTIME value, not a compile-time constant,
     * so they cannot use MOV EAX,imm32. The observed pattern instead spills
     * the number to a stack slot in the prologue and reloads it right before
     * the trap:
     *   8B 44 24 disp8   MOV EAX, [RSP+disp8]   (reload syscall nr)
     *   ...                                      (e.g. MOV R10, RCX — syscall-ABI reg fixup)
     *   0F 05            SYSCALL                 (already confirmed above)
     * RSP is intact at signal-delivery time (only RAX is clobbered by the
     * kernel's ENOSYS), so [RSP+disp8] still holds the real value — read it
     * from the saved context instead of decoding an immediate. */
    for (int n = 0; n <= SCAN_MAX; n++) {
        const uint8_t *p = rip_ptr - 4 - n;
        if (p[0] == 0x8b && p[1] == 0x44 && p[2] == 0x24) {
            int8_t disp8 = (int8_t)p[3];
            uint32_t nr;
            memcpy(&nr, rsp_ptr + disp8, 4);
            uint8_t cls = (uint8_t)(nr >> 24);
            /* #198: also accept a plain small integer (no class prefix) —
             * a raw Linux-ABI syscall number from the upstream dyld overlay.
             * nr!=0 excludes zero-byte padding false-hits. */
            if (cls == 0x02 || cls == 0xFF || (cls == 0x00 && nr != 0 && nr < 1024)) {
                return nr;
            }
        }
    }

#undef SCAN_MAX

    return 0;
}
#endif /* __x86_64__ */

/* ── macOS → FreeBSD syscall dispatcher ───────────────────────────────────── */

/*
 * purpose:  Translate a macOS BSD syscall + arguments to a FreeBSD equivalent
 *           and execute it.
 * input:    macos_nr — macOS BSD syscall number (class prefix already stripped)
 *           a1..a6  — syscall arguments from the saved mcontext
 * output:   FreeBSD kernel retval (negative = -errno) or 0/positive on success
 * sideEffects: performs the requested kernel operation
 */
static long
dispatch_macos_bsd_syscall(unsigned int macos_nr,
                           long a1, long a2, long a3,
                           long a4, long a5, long a6)
{
    switch (macos_nr) {

    /* ── process lifecycle ──────────────────────────────────────────── */
    case MACOS_SYS_exit:
        freebsd_raw_syscall(SYS__exit, a1, 0, 0, 0, 0, 0);
        __builtin_unreachable();

    case MACOS_SYS_fork:
        return freebsd_raw_syscall(SYS_fork, 0, 0, 0, 0, 0, 0);

    case MACOS_SYS_getpid:
        return freebsd_raw_syscall(SYS_getpid, 0, 0, 0, 0, 0, 0);

    case MACOS_SYS_getppid:
        return freebsd_raw_syscall(SYS_getppid, 0, 0, 0, 0, 0, 0);

    case MACOS_SYS_getuid:
        return freebsd_raw_syscall(SYS_getuid, 0, 0, 0, 0, 0, 0);

    case MACOS_SYS_getgid:
        return freebsd_raw_syscall(SYS_getgid, 0, 0, 0, 0, 0, 0);

    case MACOS_SYS_getpgrp:
        return freebsd_raw_syscall(SYS_getpgrp, 0, 0, 0, 0, 0, 0);

    case MACOS_SYS_setpgid:
        return freebsd_raw_syscall(SYS_setpgid, a1, a2, 0, 0, 0, 0);

    /* ── file descriptors ───────────────────────────────────────────── */
    case MACOS_SYS_read:
        return freebsd_raw_syscall(SYS_read, a1, a2, a3, 0, 0, 0);

    case MACOS_SYS_write:
        return freebsd_raw_syscall(SYS_write, a1, a2, a3, 0, 0, 0);

    case MACOS_SYS_readv:
        return freebsd_raw_syscall(SYS_readv, a1, a2, a3, 0, 0, 0);

    case MACOS_SYS_writev:
        return freebsd_raw_syscall(SYS_writev, a1, a2, a3, 0, 0, 0);

    case MACOS_SYS_open:
        return freebsd_raw_syscall(SYS_open, a1, a2, a3, 0, 0, 0);

    case MACOS_SYS_close:
        return freebsd_raw_syscall(SYS_close, a1, 0, 0, 0, 0, 0);

    case MACOS_SYS_access:
        return freebsd_raw_syscall(SYS_access, a1, a2, 0, 0, 0, 0);

    case MACOS_SYS_dup:
        return freebsd_raw_syscall(SYS_dup, a1, 0, 0, 0, 0, 0);

    case MACOS_SYS_dup2:
        return freebsd_raw_syscall(SYS_dup2, a1, a2, 0, 0, 0, 0);

    case MACOS_SYS_pipe:
        /* FreeBSD 15 dropped SYS_pipe in favour of SYS_pipe2; flags=0 */
        return freebsd_raw_syscall(SYS_pipe2, a1, 0, 0, 0, 0, 0);

    case MACOS_SYS_lseek:
        return freebsd_raw_syscall(SYS_lseek, a1, a2, a3, 0, 0, 0);

    case MACOS_SYS_ioctl:
        return freebsd_raw_syscall(SYS_ioctl, a1, a2, a3, 0, 0, 0);

    case MACOS_SYS_fcntl:
        return freebsd_raw_syscall(SYS_fcntl, a1, a2, a3, 0, 0, 0);

    case MACOS_SYS_fstat:
        /* FreeBSD 15: SYS_fstat = 551 (ino64 variant) */
        return freebsd_raw_syscall(SYS_fstat, a1, a2, 0, 0, 0, 0);

    /* ── memory management ──────────────────────────────────────────── */
    case MACOS_SYS_mmap:
        /* macOS mmap args: addr, len, prot, flags, fd, offset — same layout */
        return freebsd_raw_syscall(SYS_mmap, a1, a2, a3, a4, a5, a6);

    case MACOS_SYS_munmap:
        return freebsd_raw_syscall(SYS_munmap, a1, a2, 0, 0, 0, 0);

    case MACOS_SYS_mprotect:
        return freebsd_raw_syscall(SYS_mprotect, a1, a2, a3, 0, 0, 0);

    /* ── networking ─────────────────────────────────────────────────── */
    case MACOS_SYS_socket:
        return freebsd_raw_syscall(SYS_socket, a1, a2, a3, 0, 0, 0);

    case MACOS_SYS_connect:
        return freebsd_raw_syscall(SYS_connect, a1, a2, a3, 0, 0, 0);

    case MACOS_SYS_bind:
        return freebsd_raw_syscall(SYS_bind, a1, a2, a3, 0, 0, 0);

    case MACOS_SYS_listen:
        return freebsd_raw_syscall(SYS_listen, a1, a2, 0, 0, 0, 0);

    case MACOS_SYS_accept:
        return freebsd_raw_syscall(SYS_accept, a1, a2, a3, 0, 0, 0);

    case MACOS_SYS_select:
        return freebsd_raw_syscall(SYS_select, a1, a2, a3, a4, a5, 0);

    /* ── time ───────────────────────────────────────────────────────── */
    case MACOS_SYS_gettimeofday:
        return freebsd_raw_syscall(SYS_gettimeofday, a1, a2, 0, 0, 0, 0);

    /* ── sysctl ─────────────────────────────────────────────────────── */
    case MACOS_SYS_sysctl:
        /* FreeBSD 15: __sysctl = SYS___sysctl (202) */
        return freebsd_raw_syscall(SYS___sysctl, a1, a2, a3, a4, a5, a6);

    /* ── signals ────────────────────────────────────────────────────── */
    case MACOS_SYS_sigprocmask:
        /* Signal numbers differ between macOS and FreeBSD; pass through
         * for now — libsystem_kernel handles translation in real use */
        return freebsd_raw_syscall(SYS_sigprocmask, a1, a2, a3, 0, 0, 0);

    default:
        fprintf(stderr,
            "[darling-mldr] unhandled macOS BSD syscall %u (0x%x) — ENOSYS\n",
            macos_nr, macos_nr);
        return -ENOSYS;
    }
}

/* ── macOS Mach trap dispatcher ────────────────────────────────────────────── */

/*
 * purpose:  Dispatch a macOS Mach trap to darlingserver via RPC.
 *           The five early bootstrap traps (mach_reply_port, *_self_trap, and
 *           mach_msg_trap) are the only ones dyld calls before libSystem is
 *           initialised.  All others are deferred to a future expansion.
 *
 * input:    trap_nr    — raw int32_t Mach trap number (negative, e.g. -28)
 *           a1..a6     — argument registers from the saved mcontext (intact)
 *           stack_arg1 — first stack argument from the Mach-O caller's stack
 *                        (read from mc_rsp+8, past the return address).
 *                        For mach_msg_trap (-31): rcv_msg (7th argument).
 *                        For mach_msg_overwrite (-32): priority (7th argument);
 *                        rcv_msg (8th) would need a separate stack read.
 * output:   port name (uint32_t) on success, or -errno on error
 * sideEffects: issues a synchronous RPC to darlingserver; may log to stderr.
 */
static long
dispatch_mach_trap(int trap_nr,
                   long a1, long a2, long a3,
                   long a4, long a5, long a6,
                   uintptr_t stack_arg1)
{
    uint32_t port_name = 0;
    int ret;

    switch (trap_nr) {

    /* ── bootstrap port traps (no-argument, return a send right) ─────── */
    case MACH_TRAP_task_self_trap:
        ret = dserver_rpc_task_self_trap(&port_name);
        if (ret < 0) {
            fprintf(stderr,
                "[darling-mldr] mach_trap: task_self_trap RPC failed: %d\n", ret);
            return (long)ret; /* negative errno */
        }
        return (long)(uint64_t)port_name;

    case MACH_TRAP_host_self_trap:
        ret = dserver_rpc_host_self_trap(&port_name);
        if (ret < 0) {
            fprintf(stderr,
                "[darling-mldr] mach_trap: host_self_trap RPC failed: %d\n", ret);
            return (long)ret;
        }
        return (long)(uint64_t)port_name;

    case MACH_TRAP_thread_self_trap:
        ret = dserver_rpc_thread_self_trap(&port_name);
        if (ret < 0) {
            fprintf(stderr,
                "[darling-mldr] mach_trap: thread_self_trap RPC failed: %d\n", ret);
            return (long)ret;
        }
        return (long)(uint64_t)port_name;

    case MACH_TRAP_mach_reply_port:
        ret = dserver_rpc_mach_reply_port(&port_name);
        if (ret < 0) {
            fprintf(stderr,
                "[darling-mldr] mach_trap: mach_reply_port RPC failed: %d\n", ret);
            return (long)ret;
        }
        return (long)(uint64_t)port_name;

    /* ── mach_msg_trap: full IPC send/receive ────────────────────────── */
    /* macOS mach_msg_trap (-31) args:
     *   rdi=msg, rsi=option, rdx=send_size, r10=rcv_size, r8=rcv_name,
     *   r9=timeout, [rsp+8]=rcv_msg  (7th arg — first stack slot).
     * We pass rcv_msg from the Mach-O caller's stack (stack_arg1) rather
     * than duplicating msg, which is only correct for in-place receive.
     * priority defaults to 0 (not present in the 7-argument trap form). */
    case MACH_TRAP_mach_msg_trap:
        ret = dserver_rpc_mach_msg_overwrite(
            (void*)a1,               /* msg */
            (int32_t)a2,             /* option */
            (uint32_t)a3,            /* send_size */
            (uint32_t)a4,            /* rcv_size */
            (uint32_t)a5,            /* rcv_name */
            (uint32_t)a6,            /* timeout */
            0,                       /* priority (absent in 7-arg form) */
            (void*)stack_arg1        /* rcv_msg from caller's stack */
        );
        if (ret < 0) {
            fprintf(stderr,
                "[darling-mldr] mach_trap: mach_msg_trap RPC failed: %d\n", ret);
            return (long)ret;
        }
        return 0; /* MACH_MSG_SUCCESS */

    case MACH_TRAP_mach_msg_overwrite:
        /* 9-argument form: a1=msg, a2=option, a3=send_sz, a4=rcv_sz,
         * a5=rcv_name, a6=timeout; [rsp+8]=priority (7th, stack_arg1),
         * [rsp+16]=rcv_msg (8th — not yet read; default to msg). */
        ret = dserver_rpc_mach_msg_overwrite(
            (void*)a1,               /* msg */
            (int32_t)a2,             /* option */
            (uint32_t)a3,            /* send_size */
            (uint32_t)a4,            /* rcv_size */
            (uint32_t)a5,            /* rcv_name */
            (uint32_t)a6,            /* timeout */
            (uint32_t)stack_arg1,    /* priority from caller's stack */
            (void*)a1                /* rcv_msg: default to msg (stack+16 TODO) */
        );
        if (ret < 0) {
            fprintf(stderr,
                "[darling-mldr] mach_trap: mach_msg_overwrite RPC failed: %d\n", ret);
            return (long)ret;
        }
        return 0;

    default:
        fprintf(stderr,
            "[darling-mldr] mach_trap: unhandled Mach trap %d — ENOSYS\n",
            trap_nr);
        return -ENOSYS;
    }

    /* suppress unused-parameter warnings for architectures where a1..a6
     * are not read by all paths */
    (void)a1; (void)a2; (void)a3; (void)a4; (void)a5; (void)a6;
    (void)stack_arg1;
}

#if defined(__x86_64__)
/* ── Linux struct stat translation (x86-64 only, #198) ────────────────────── */

/* Linux x86-64 struct stat (asm/stat.h) — 144 bytes, completely different
 * field order/sizes/count from FreeBSD's ino64 struct stat (extra
 * st_bsdflags/st_padding1/birthtime/st_flags/st_gen fields, different
 * nlink/mode widths, no nsec-adjacent-to-sec pairing). A naive pointer
 * passthrough of FreeBSD's native stat(2)/fstat(2) buffer — what this code
 * used to do — hands dyld a buffer it then reads at LINUX field offsets,
 * silently returning garbage (this is the same class of bug as the
 * sockaddr_un/sigaltstack layout mismatches above, just not yet caught by
 * a build+truss pass before now). */
struct linux_stat {
    unsigned long l_st_dev;
    unsigned long l_st_ino;
    unsigned long l_st_nlink;
    unsigned int  l_st_mode;
    unsigned int  l_st_uid;
    unsigned int  l_st_gid;
    unsigned int  l_pad0;
    unsigned long l_st_rdev;
    long          l_st_size;
    long          l_st_blksize;
    long          l_st_blocks;
    /* Named l_st_atime (not st_atime) — <sys/stat.h> #defines st_atime/
     * st_mtime/st_ctime to st_atim.tv_sec/etc, which would otherwise mangle
     * these field names via macro expansion. */
    unsigned long l_st_atime;
    unsigned long l_st_atime_nsec;
    unsigned long l_st_mtime;
    unsigned long l_st_mtime_nsec;
    unsigned long l_st_ctime;
    unsigned long l_st_ctime_nsec;
    long          l_unused[3];
};

/*
 * purpose:  Fill a Linux-ABI struct stat from a FreeBSD-native one.
 * input:    nst — a FreeBSD struct stat already populated by a real
 *           stat(2)/fstat(2)/fstatat(2) call.
 * output:   *out is overwritten with the Linux-layout equivalent.
 * sideEffects: none.
 */
static void
translate_native_stat_to_linux(const struct stat *nst, struct linux_stat *out)
{
    memset(out, 0, sizeof(*out));
    out->l_st_dev     = (unsigned long)nst->st_dev;
    out->l_st_ino     = (unsigned long)nst->st_ino;
    out->l_st_nlink   = (unsigned long)nst->st_nlink;
    out->l_st_mode    = (unsigned int)nst->st_mode;
    out->l_st_uid     = (unsigned int)nst->st_uid;
    out->l_st_gid     = (unsigned int)nst->st_gid;
    out->l_st_rdev    = (unsigned long)nst->st_rdev;
    out->l_st_size    = (long)nst->st_size;
    out->l_st_blksize = (long)nst->st_blksize;
    out->l_st_blocks  = (long)nst->st_blocks;
    out->l_st_atime      = (unsigned long)nst->st_atim.tv_sec;
    out->l_st_atime_nsec = (unsigned long)nst->st_atim.tv_nsec;
    out->l_st_mtime      = (unsigned long)nst->st_mtim.tv_sec;
    out->l_st_mtime_nsec = (unsigned long)nst->st_mtim.tv_nsec;
    out->l_st_ctime      = (unsigned long)nst->st_ctim.tv_sec;
    out->l_st_ctime_nsec = (unsigned long)nst->st_ctim.tv_nsec;
}

/* ── Linux-ABI syscall dispatcher (x86-64 only, #198) ─────────────────────── */

#if defined(__x86_64__)

/* ── guest image registry / diagnostic traps ───────────────────────────────── */

/* Page granularity for the mprotect() dance done when rewriting bytes of an
 * already-mapped image — used both by the MLDR_TRAP_AT sites below and by
 * mldr_patch_linux_raw_syscalls further down. */
#define MLDR_PATCH_PAGE_SIZE 4096u

/* ── timerfd registry ──────────────────────────────────────────────────────── */

/*
 * A Linux timerfd is a file descriptor that becomes readable when the timer
 * fires and yields a u64 expiration count when read. FreeBSD's equivalent
 * timer is EVFILT_TIMER, which lives inside a kqueue and is not a readable
 * descriptor at all, so the two halves have to be bridged: timerfd_create
 * hands back a kqueue fd (pollable, and nestable inside the outer kqueue our
 * epoll shim uses), while read() on that fd has to be recognised and answered
 * with the expiration count rather than passed to the kernel, which would
 * fail on a kqueue fd.
 *
 * Recognising it needs this table — an fd carries no marker saying it came
 * from timerfd_create. Fixed-size and lock-free: it is written from the
 * syscall dispatch path, which runs inside the SIGILL handler.
 */
#define MLDR_MAX_TIMERFDS 32
static int _mldr_timerfds[MLDR_MAX_TIMERFDS];
static int _mldr_timerfd_count;

/*
 * eventfd registry.
 *
 * FreeBSD has no eventfd(2), and the property callers actually depend on is a
 * single fd that can be written to wake up whoever is polling it and read to
 * drain that wakeup. A socketpair provides it, but only if BOTH ends stay
 * open and writes are redirected to the peer, so the bytes arrive in the
 * returned fd's own receive queue.
 *
 * Closing the peer instead — as this originally did — produces an fd that is
 * permanently at EOF: EVFILT_READ reports it ready forever with EV_EOF set and
 * zero bytes available. libkqueue registers exactly such an eventfd for
 * self-wakeup, so every epoll_wait returned it as ready, and libkqueue's
 * copyout then ran against a knote with nothing behind it and aborted the
 * process. Hence this table: read/write/close have to know which fds are
 * eventfds and which peer belongs to each.
 */
#define MLDR_MAX_EVENTFDS 32
static struct {
    int fd;
    int peer;
} _mldr_eventfds[MLDR_MAX_EVENTFDS];
static int _mldr_eventfd_count;

/*
 * purpose:  Find the peer socket of an eventfd handed out by the shim.
 * input:    fd — descriptor to look up.
 * output:   the peer fd, or -1 if this is not one of ours.
 * sideEffects: none.
 */
static int
mldr_eventfd_peer(int fd)
{
    for (int i = 0; i < _mldr_eventfd_count; i++) {
        if (_mldr_eventfds[i].fd == fd)
            return _mldr_eventfds[i].peer;
    }
    return -1;
}

/*
 * purpose:  Record / forget an eventfd pair.
 * input:    fd, peer; add — true to record, false to forget (on close).
 * output:   the peer that was forgotten, or -1.
 * sideEffects: mutates the registry.
 */
static int
mldr_eventfd_track(int fd, int peer, bool add)
{
    if (add) {
        if (_mldr_eventfd_count < MLDR_MAX_EVENTFDS) {
            _mldr_eventfds[_mldr_eventfd_count].fd = fd;
            _mldr_eventfds[_mldr_eventfd_count].peer = peer;
            _mldr_eventfd_count++;
        }
        return peer;
    }

    for (int i = 0; i < _mldr_eventfd_count; i++) {
        if (_mldr_eventfds[i].fd == fd) {
            int old = _mldr_eventfds[i].peer;
            _mldr_eventfds[i] = _mldr_eventfds[--_mldr_eventfd_count];
            return old;
        }
    }
    return -1;
}

/*
 * purpose:  Test whether an fd was handed out by the timerfd_create shim.
 * input:    fd — descriptor to test.
 * output:   true if this is one of ours.
 * sideEffects: none.
 */
static bool
mldr_is_timerfd(int fd)
{
    for (int i = 0; i < _mldr_timerfd_count; i++) {
        if (_mldr_timerfds[i] == fd)
            return true;
    }
    return false;
}

/*
 * purpose:  Record / forget an fd as a timerfd.
 * input:    fd; add — true to record, false to forget (on close).
 * output:   none.
 * sideEffects: mutates the registry.
 */
static void
mldr_timerfd_track(int fd, bool add)
{
    if (add) {
        if (!mldr_is_timerfd(fd) && _mldr_timerfd_count < MLDR_MAX_TIMERFDS)
            _mldr_timerfds[_mldr_timerfd_count++] = fd;
        return;
    }

    for (int i = 0; i < _mldr_timerfd_count; i++) {
        if (_mldr_timerfds[i] == fd) {
            _mldr_timerfds[i] = _mldr_timerfds[--_mldr_timerfd_count];
            return;
        }
    }
}

/*
 * purpose:  Answer a read(2) on a timerfd with the u64 expiration count Linux
 *           callers expect, collected from the underlying EVFILT_TIMER.
 * input:    fd — the timerfd; buf/len — caller's buffer.
 * output:   8 on success, or a negative errno.
 * sideEffects: consumes the pending expirations from the kqueue.
 *
 *           Divergence: a Linux timerfd read blocks until the timer fires
 *           unless the fd is non-blocking, whereas this always polls and
 *           reports EAGAIN when nothing has expired. Callers reach here after
 *           their event loop said the fd was readable, so the blocking case
 *           does not arise in practice; making it block would mean sleeping
 *           inside the SIGILL handler.
 */
static long
mldr_timerfd_read(int fd, void *buf, size_t len)
{
    if (buf == NULL)
        return -EFAULT;
    if (len < sizeof(uint64_t))
        return -EINVAL;

    struct kevent kev;
    struct timespec zero = { 0, 0 };
    long n = freebsd_raw_syscall(SYS_kevent, fd, 0, 0,
                                 (long)&kev, 1, (long)&zero);
    if (n < 0)
        return n;
    if (n == 0)
        return -EAGAIN;

    /* EVFILT_TIMER reports in kev.data how many times the timeout elapsed
     * since it was last collected — the same quantity timerfd returns. */
    uint64_t count = (uint64_t)kev.data;
    if (count == 0)
        count = 1;

    memcpy(buf, &count, sizeof(count));
    return (long)sizeof(count);
}

/*
 * mldr executes the guest Mach-O images directly in its own address space, so
 * they are not host dynamic-linker modules: a host debugger sees no symbols for
 * them at all (lldb resolves `break -n objc_exception_throw` to "no locations")
 * and dyld's own image list lives only in guest memory it can't walk. Nothing
 * else records which image an address belongs to, so a crash address or a
 * return address on the stack is unattributable without this table.
 *
 * Fixed-size and never freed on purpose: this is written from the syscall
 * dispatch path, reached from a signal handler.
 */
#define MLDR_MAX_IMAGES 128
#define MLDR_MAX_TRAPS    8

struct mldr_image {
    uintptr_t base;
    size_t    size;
    char      name[64];
};

struct mldr_trap {
    uintptr_t addr;
    char      label[96];
};

static struct mldr_image _mldr_images[MLDR_MAX_IMAGES];
static int _mldr_image_count;
static struct mldr_trap _mldr_traps[MLDR_MAX_TRAPS];
static int _mldr_trap_count;

/*
 * purpose:  Last path component of a path, for matching and reporting.
 * input:    path — NUL-terminated path.
 * output:   pointer into path, never NULL.
 * sideEffects: none.
 */
static const char *
mldr_basename(const char *path)
{
    const char *slash = strrchr(path, '/');
    return slash != NULL ? slash + 1 : path;
}

/*
 * purpose:  Describe an address as "<image>+0x<offset>", the form that can be
 *           handed straight to llvm-nm/llvm-objdump on that image to get a
 *           function name.
 * input:    addr — address to resolve; out/outsz — caller's buffer.
 * output:   out, always NUL-terminated ("0x... <unknown>" if unresolvable).
 * sideEffects: none.
 */
static const char *
mldr_describe_addr(uintptr_t addr, char *out, size_t outsz)
{
    for (int i = 0; i < _mldr_image_count; i++) {
        uintptr_t base = _mldr_images[i].base;

        if (addr >= base && addr < base + _mldr_images[i].size) {
            snprintf(out, outsz, "%s+0x%lx", _mldr_images[i].name,
                     (unsigned long)(addr - base));
            return out;
        }
    }

    snprintf(out, outsz, "0x%lx <unknown>", (unsigned long)addr);
    return out;
}

/*
 * purpose:  Plant a one-shot ud2 breakpoint at <image>+<offset> for every entry
 *           in MLDR_TRAP_AT that names the image just mapped, so guest code
 *           reaching that address reports who called it and dies there.
 *
 *           This exists because the usual way to answer "who threw this
 *           exception" is unavailable: a host debugger cannot set a breakpoint
 *           by symbol in a guest image (see the registry comment above), and
 *           the guest images are Apple binaries that can't be rebuilt with
 *           instrumentation. mldr already maps every image and already fields
 *           #UD via sigill_handler, so it is the one component that can do
 *           this at all. Offsets come from `llvm-nm <dylib>` — for example
 *           MLDR_TRAP_AT='libc++abi.dylib+0x2c670' for __cxa_throw.
 * input:    base/size of the mapping; name — its basename.
 * output:   none.
 * sideEffects: rewrites two bytes of the mapped image and records the site;
 *           the original instruction is destroyed, so a planted trap is fatal
 *           by design rather than resumable.
 */
static void
mldr_plant_traps(uintptr_t base, size_t size, const char *name)
{
    const char *spec = getenv("MLDR_TRAP_AT");

    if (spec == NULL)
        return;

    while (*spec != '\0') {
        const char *end = strchr(spec, ',');
        size_t entry_len = (end != NULL) ? (size_t)(end - spec) : strlen(spec);

        /* Split on the LAST '+', not the first: library names contain '+' of
         * their own — libc++abi.dylib being exactly the one this gets pointed
         * at most often, where splitting on the first '+' yields the image
         * name "libc" and silently matches nothing. */
        const char *plus = NULL;
        for (size_t i = entry_len; i > 0; i--) {
            if (spec[i - 1] == '+') {
                plus = &spec[i - 1];
                break;
            }
        }

        if (plus != NULL) {
            size_t name_len = (size_t)(plus - spec);

            if (strlen(name) == name_len
                && strncmp(spec, name, name_len) == 0) {
                unsigned long off = strtoul(plus + 1, NULL, 0);

                if (off >= size) {
                    fprintf(stderr,
                        "[darling-mldr] MLDR_TRAP_AT: offset 0x%lx is past the"
                        " end of %s's 0x%zx-byte executable mapping — ignored\n",
                        off, name, size);
                } else if (_mldr_trap_count >= MLDR_MAX_TRAPS) {
                    fprintf(stderr,
                        "[darling-mldr] MLDR_TRAP_AT: no room for %s+0x%lx"
                        " (max %d traps)\n", name, off, MLDR_MAX_TRAPS);
                } else {
                    uint8_t *at = (uint8_t *)(base + off);
                    uintptr_t page = (uintptr_t)at
                        & ~((uintptr_t)MLDR_PATCH_PAGE_SIZE - 1);

                    if (mprotect((void *)page, MLDR_PATCH_PAGE_SIZE * 2,
                                 PROT_READ | PROT_WRITE | PROT_EXEC) < 0) {
                        fprintf(stderr,
                            "[darling-mldr] MLDR_TRAP_AT: mprotect for %s+0x%lx"
                            " failed: %s\n", name, off, strerror(errno));
                    } else {
                        at[0] = 0x0f;
                        at[1] = 0x0b;   /* ud2 */
                        mprotect((void *)page, MLDR_PATCH_PAGE_SIZE * 2,
                                 PROT_READ | PROT_EXEC);

                        struct mldr_trap *t = &_mldr_traps[_mldr_trap_count++];
                        t->addr = (uintptr_t)at;
                        snprintf(t->label, sizeof(t->label), "%s+0x%lx",
                                 name, off);
                        fprintf(stderr,
                            "[darling-mldr] MLDR_TRAP_AT: armed %s at %p\n",
                            t->label, (void *)at);
                    }
                }
            }
        }

        if (end == NULL)
            break;
        spec = end + 1;
    }
}

/*
 * purpose:  Report a planted trap being hit: which site, the argument registers,
 *           the immediate caller, and every stack slot that looks like a return
 *           address into a known image — a usable backtrace where a debugger
 *           can produce none.
 *
 *           For an __cxa_throw site rsi is the std::type_info*, whose name
 *           string sits one pointer in; printing it identifies the exception
 *           type, which is the thing dyld's bare "dyld std::__terminate()"
 *           never says.
 * input:    trap — the site that was hit; mc — faulting thread context.
 * output:   none.
 * sideEffects: writes the report to stderr.
 */
static void
mldr_report_trap(const struct mldr_trap *trap, const mcontext_t *mc)
{
    char buf[160];

    fprintf(stderr, "\n[darling-mldr] === MLDR_TRAP_AT hit: %s ===\n",
            trap->label);
    fprintf(stderr, "  rdi=0x%llx rsi=0x%llx rdx=0x%llx rcx=0x%llx\n",
            (unsigned long long)mc->mc_rdi, (unsigned long long)mc->mc_rsi,
            (unsigned long long)mc->mc_rdx, (unsigned long long)mc->mc_rcx);

    /* __cxa_throw(void *exc, std::type_info *tinfo, void (*dest)(void*)) —
     * tinfo->__type_name is the second pointer of the type_info object. */
    if (strstr(trap->label, "c++abi") != NULL && mc->mc_rsi != 0) {
        const char *const *namep =
            (const char *const *)(uintptr_t)(mc->mc_rsi + 8);

        if (*namep != NULL)
            fprintf(stderr, "  exception type: %s\n", *namep);
    }

    /* rip is AT the ud2, so nothing has been pushed yet: [rsp] is still the
     * caller's return address. */
    const uintptr_t *sp = (const uintptr_t *)(uintptr_t)mc->mc_rsp;

    fprintf(stderr, "  called from %s\n",
            mldr_describe_addr(sp[0], buf, sizeof(buf)));

    fprintf(stderr, "  stack slots resolving into known images:\n");
    for (int i = 0; i < 48; i++) {
        uintptr_t slot = sp[i];

        for (int j = 0; j < _mldr_image_count; j++) {
            if (slot >= _mldr_images[j].base
                && slot < _mldr_images[j].base + _mldr_images[j].size) {
                fprintf(stderr, "    [rsp+%3d] %s\n", i * 8,
                        mldr_describe_addr(slot, buf, sizeof(buf)));
                break;
            }
        }
    }
    fflush(stderr);
}

/*
 * purpose:  Record a file-backed mapping in the image registry, log it when
 *           asked, and — for executable ones — arm any MLDR_TRAP_AT site that
 *           names it.
 *
 *           Non-executable segments are registered too, not just code. A guest
 *           pointer often refers to one: a format string, a constant, a vtable
 *           all live in __DATA_CONST or __TEXT,__cstring, which are mapped
 *           separately from the executable segment. Registering only the
 *           executable ones left every such pointer unresolvable, which is
 *           exactly what stalled reading a diagnostic string out of the guest.
 * input:    base/size of the mapping just created, fd it was mapped from,
 *           executable — whether the mapping carries PROT_EXEC.
 * output:   none.
 * sideEffects: appends to the image registry; writes one line per mapping to
 *           stderr when MLDR_LOG_MAPPINGS is set; may rewrite two bytes of an
 *           executable image (see mldr_plant_traps).
 */
static void
mldr_note_mapping(void *base, size_t size, int fd, bool executable)
{
    static int log_enabled = -1;
    struct kinfo_file kf;
    const char *path;

    /*
     * F_KINFO, not macOS's F_GETPATH — FreeBSD has no F_GETPATH at all, and
     * kf_path is where it puts the same information. kf_structsize must be
     * filled in by the caller; the kernel rejects the request otherwise.
     *
     * Raw syscall rather than libc fcntl(): this runs from the SIGILL handler's
     * dispatch path, same reasoning as every other freebsd_raw_syscall here.
     * The path resolves through the namecache, so it can legitimately come back
     * empty for an unlinked or uncached vnode — report the mapping either way
     * instead of dropping the line, since the base address is the useful half.
     */
    kf.kf_structsize = sizeof(kf);
    if (freebsd_raw_syscall(SYS_fcntl, fd, F_KINFO, (long)&kf, 0, 0, 0) == 0
        && kf.kf_path[0] != '\0')
        path = kf.kf_path;
    else
        path = "<path unavailable>";

    if (log_enabled < 0)
        log_enabled = (getenv("MLDR_LOG_MAPPINGS") != NULL);
    if (log_enabled)
        fprintf(stderr, "[darling-mldr] %s-mapping %p+0x%zx fd=%d %s\n",
                executable ? "exec" : "data", base, size, fd, path);

    const char *name = mldr_basename(path);

    if (_mldr_image_count < MLDR_MAX_IMAGES) {
        struct mldr_image *img = &_mldr_images[_mldr_image_count++];

        img->base = (uintptr_t)base;
        img->size = size;
        /* Tag data segments so an address resolving into one is not mistaken
         * for a code offset that could be fed to llvm-nm. */
        snprintf(img->name, sizeof(img->name), "%s%s",
                 name, executable ? "" : " (data)");
    }

    if (executable)
        mldr_plant_traps((uintptr_t)base, size, name);
}
#endif /* __x86_64__ */

/*
 * purpose:  Translate a raw Linux x86-64 syscall (as made directly by the
 *           upstream dyld/libdyld.dylib overlay binary, which was compiled
 *           to run on Linux) to a FreeBSD equivalent and execute it.
 * input:    linux_nr — Linux syscall number; a1..a6 — arguments (already in
 *           the right registers: Linux and FreeBSD raw syscall ABI share the
 *           same argument-register convention on x86-64); mc — the caller's
 *           mcontext_t*, needed only by arch_prctl (see its case) to patch
 *           mc_fsbase/mc_gsbase directly. May be NULL for call sites that
 *           can't reach arch_prctl (sigill_handler's two known ud2 sites).
 * output:   FreeBSD kernel retval (negative = -errno) or 0/positive on success
 * sideEffects: performs the requested kernel operation.
 */
static long
dispatch_linux_syscall(unsigned int linux_nr,
                       long a1, long a2, long a3,
                       long a4, long a5, long a6,
                       mcontext_t *mc)
{
    switch (linux_nr) {

    case LINUX_SYS_read:
        /* A timerfd is a kqueue fd here (see timerfd_create), and read(2) on
         * a kqueue fails — so reads of one have to be answered from the
         * timer itself rather than passed to the kernel. */
        if (mldr_is_timerfd((int)a1))
            return mldr_timerfd_read((int)a1, (void *)a2, (size_t)a3);
        return freebsd_raw_syscall(SYS_read, a1, a2, a3, 0, 0, 0);
    case LINUX_SYS_write: {
        /* An eventfd is written to wake up whoever polls it. Writing to the
         * fd itself would send the bytes AWAY from it (a socketpair end is
         * full-duplex), leaving it unreadable; writing to the peer puts them
         * in this fd's receive queue, which is the wakeup callers expect. */
        int peer = mldr_eventfd_peer((int)a1);
        if (peer >= 0)
            return freebsd_raw_syscall(SYS_write, peer, a2, a3, 0, 0, 0);
        return freebsd_raw_syscall(SYS_write, a1, a2, a3, 0, 0, 0);
    }
    case LINUX_SYS_open:
        /* openat below translated its flags; this one did not, though the two
         * take the same flag word. Any guest open() asking for O_CREAT got
         * O_ASYNC instead and failed with ENOENT, and one asking for O_APPEND
         * got O_TRUNC — losing the contents of a file it meant to extend. */
        return freebsd_raw_syscall(SYS_open, a1,
                                   mldr_open_flags_linux_to_freebsd(a2),
                                   a3, 0, 0, 0);
    case LINUX_SYS_close:
        /* Drop any timerfd/eventfd registration first: fd numbers get reused,
         * and a stale entry would make an unrelated later fd behave as one. */
        mldr_timerfd_track((int)a1, false);
        {
            int peer = mldr_eventfd_track((int)a1, -1, false);
            if (peer >= 0)
                freebsd_raw_syscall(SYS_close, peer, 0, 0, 0, 0, 0);
        }
        return freebsd_raw_syscall(SYS_close, a1, 0, 0, 0, 0, 0);
    case LINUX_SYS_stat: {
        /* FreeBSD 15 dropped SYS_stat entirely (11-compat only, old ABI
         * struct); fstatat(AT_FDCWD, path, buf, 0) is the modern ino64
         * equivalent (mirrors the MACOS_SYS_fstat comment above). The
         * result then needs translate_native_stat_to_linux — see its
         * comment for why a raw buffer passthrough is wrong. */
        struct stat nst;
        long r = freebsd_raw_syscall(SYS_fstatat, AT_FDCWD, a1, (long)&nst, 0, 0, 0);
        if (r < 0) return r;
        translate_native_stat_to_linux(&nst, (struct linux_stat *)a2);
        return r;
    }
    case LINUX_SYS_fstat: {
        struct stat nst;
        long r = freebsd_raw_syscall(SYS_fstat, a1, (long)&nst, 0, 0, 0, 0);
        if (r < 0) return r;
        translate_native_stat_to_linux(&nst, (struct linux_stat *)a2);
        return r;
    }
    case LINUX_SYS_lstat: {
        /* Same as LINUX_SYS_stat but AT_SYMLINK_NOFOLLOW (don't follow the
         * final symlink component) — that's the only difference between
         * stat(2) and lstat(2). */
        struct stat nst;
        long r = freebsd_raw_syscall(SYS_fstatat, AT_FDCWD, a1, (long)&nst,
                                     AT_SYMLINK_NOFOLLOW, 0, 0);
        if (r < 0) return r;
        translate_native_stat_to_linux(&nst, (struct linux_stat *)a2);
        return r;
    }
    case LINUX_SYS_newfstatat: {
        /* newfstatat(dirfd, path, buf, flags) — modern glibc/musl route
         * stat/lstat/fstatat all through this one syscall. AT_FDCWD (-100)
         * happens to coincide between the two OSes, so dirfd passes
         * through as-is; AT_SYMLINK_NOFOLLOW/AT_EMPTY_PATH do NOT (Linux
         * 0x100/0x1000 vs FreeBSD 0x200/0x4000) and need bit translation. */
        long freebsd_flags = 0;
        if (a4 & 0x100)  freebsd_flags |= AT_SYMLINK_NOFOLLOW;
        if (a4 & 0x1000) freebsd_flags |= AT_EMPTY_PATH;
        struct stat nst;
        long r = freebsd_raw_syscall(SYS_fstatat, a1, a2, (long)&nst,
                                     freebsd_flags, 0, 0);
        if (r < 0) return r;
        translate_native_stat_to_linux(&nst, (struct linux_stat *)a3);
        return r;
    }
    case LINUX_SYS_lseek:
        return freebsd_raw_syscall(SYS_lseek, a1, a2, a3, 0, 0, 0);
    case LINUX_SYS_pread64:
        /* pread(fd, buf, count, offset) — identical signature on both OSes
         * (FreeBSD's amd64 ABI takes the 64-bit offset in a single register,
         * same as Linux). dyld reads the first 4K of every candidate dylib
         * through this call, so a missing case shows up as
         * "pread of first 4K failed: 78" (ENOSYS). */
        return freebsd_raw_syscall(SYS_pread, a1, a2, a3, a4, 0, 0);
    case LINUX_SYS_pwrite64:
        return freebsd_raw_syscall(SYS_pwrite, a1, a2, a3, a4, 0, 0);
    case LINUX_SYS_readlink:
        /* readlink(path, buf, bufsiz) — identical signature/semantics on
         * both OSes. */
        return freebsd_raw_syscall(SYS_readlink, a1, a2, a3, 0, 0, 0);
    case LINUX_SYS_clock_gettime: {
        /* clock_gettime(clockid, *tp) — struct timespec is the same layout
         * on both OSes; only the clockid VALUE needs translating for the
         * ids we've actually seen requested so far. */
        long clockid = a1;
        if (clockid == LINUX_CLOCK_MONOTONIC) {
            clockid = CLOCK_MONOTONIC;
        }
        return freebsd_raw_syscall(SYS_clock_gettime, clockid, a2, 0, 0, 0, 0);
    }
    case LINUX_SYS_gettimeofday:
        /* gettimeofday(tv, tz) — struct timeval is the same layout on both
         * OSes, and the tz argument has been unused/ignored by both kernels
         * for decades (glibc/libc long ago hardcoded NULL for it) — pure
         * passthrough. Surfaced by real SQLite (#198): it timestamps its
         * B-tree/journal operations with this rather than clock_gettime. */
        return freebsd_raw_syscall(SYS_gettimeofday, a1, a2, 0, 0, 0, 0);
    case LINUX_SYS_openat: {
        /* openat(dirfd, path, flags, mode) — dirfd's AT_FDCWD (-100)
         * coincides between the two OSes and access-mode bits (RDONLY/
         * WRONLY/RDWR) are identical; the rest of the flag bits are NOT
         * (see LINUX_O_* above) and must be translated explicitly. */
        long freebsd_oflags = mldr_open_flags_linux_to_freebsd(a3);
        return freebsd_raw_syscall(SYS_openat, a1, a2, freebsd_oflags, a4, 0, 0);
    }
    case LINUX_SYS_mmap: {
        /* Linux mmap(addr,len,prot,flags,fd,off) — same arg layout as
         * FreeBSD, and MAP_SHARED(0x1)/MAP_PRIVATE(0x2)/MAP_FIXED(0x10) DO
         * coincide — but MAP_ANONYMOUS very much does NOT (Linux 0x20 vs
         * FreeBSD MAP_ANON 0x1000). Confirmed live on 185 (#198): dyld's
         * own allocator (_simple_salloc) mmaps anonymous+private memory;
         * passing Linux's raw flags through left FreeBSD's ANON bit unset,
         * so it tried to map fd=-1 as a real file-backed mapping, failed,
         * and _simple_salloc silently returned NULL — surfacing many calls
         * later as dyld's generic "mkstringf, out of memory error". */
        long freebsd_flags = a4 & 0x3;      /* MAP_SHARED | MAP_PRIVATE */
        if (a4 & 0x10)    freebsd_flags |= MAP_FIXED;
        if (a4 & 0x20)    freebsd_flags |= MAP_ANON;     /* MAP_ANONYMOUS */
        if (a4 & 0x20000) freebsd_flags |= MAP_STACK;
        long mr = freebsd_raw_syscall(SYS_mmap, a1, a2, a3, freebsd_flags, a5, a6);
#if defined(__x86_64__)
        /*
         * Patch raw-syscall trampolines in every executable image dyld maps,
         * not just in dyld itself.
         *
         * mldr can only patch the dylinker it loads by hand (see loader.c);
         * libSystem.B.dylib and everything under it are mapped later, by dyld,
         * and Darling's statically-linked libsystem_kernel inside them carries
         * the same raw `syscall` thunks. Left unpatched they run FreeBSD
         * syscalls under Linux numbers — verified live via ktrace: Linux
         * open(2) executed FreeBSD fork(2), after which the parent _exit()ed
         * with the child's pid as its status.
         *
         * Every file-backed executable mapping goes through here, so this is
         * the one chokepoint that covers all of them.
         */
        if (mr >= 0 && (int)a5 >= 0) {
            bool executable = (a3 & PROT_EXEC) != 0;

            mldr_note_mapping((void *)mr, (size_t)a2, (int)a5, executable);
            if (executable)
                mldr_patch_linux_raw_syscalls((void *)mr, (size_t)a2);
        }
#endif
        return mr;
    }
    case LINUX_SYS_mprotect:
        return freebsd_raw_syscall(SYS_mprotect, a1, a2, a3, 0, 0, 0);
    case LINUX_SYS_munmap:
        return freebsd_raw_syscall(SYS_munmap, a1, a2, 0, 0, 0, 0);
    case LINUX_SYS_ioctl:
        return freebsd_raw_syscall(SYS_ioctl, a1, a2, a3, 0, 0, 0);
    case LINUX_SYS_access:
        return freebsd_raw_syscall(SYS_access, a1, a2, 0, 0, 0, 0);
    case LINUX_SYS_dup:
        return freebsd_raw_syscall(SYS_dup, a1, 0, 0, 0, 0, 0);
    case LINUX_SYS_dup2:
        return freebsd_raw_syscall(SYS_dup2, a1, a2, 0, 0, 0, 0);
    case LINUX_SYS_getpid:
        return freebsd_raw_syscall(SYS_getpid, 0, 0, 0, 0, 0, 0);
    case LINUX_SYS_sendmsg: {
        /* struct msghdr's msg_name/msg_namelen sit at the same byte offsets
         * in both OSes' layouts (verified: msg_iov/msg_control do too, by
         * coincidence of padding — only msg_flags' offset differs, and
         * sendmsg never reads that field), so patching them in place via a
         * native struct msghdr * is safe. What's NOT safe to pass through
         * is the sockaddr_un *contents* dyld built (see below); leave
         * iov/control untouched. cmsg (SCM_RIGHTS fd-passing) framing also
         * differs between the two OSes — revisit if an fd-passing sendmsg
         * ever misbehaves. */
        struct msghdr *mh = (struct msghdr *)a2;
        void *orig_name = mh->msg_name;
        socklen_t orig_namelen = mh->msg_namelen;
        struct sockaddr_un fixed_addr;

        /* Every msg_name this codebase ever sends to is the darlingserver
         * AF_UNIX RPC socket, so the family byte(s) are never actually
         * ambiguous in practice — no need to gate on their exact value
         * (which, empirically, isn't Linux's plain little-endian encoding
         * either; not worth chasing exactly what it is). The one thing
         * that DOES matter is msg_namelen: Linux's struct sockaddr_un is
         * 110 bytes (2 + 108), FreeBSD's is 106 (1 + 1 + 104), and
         * FreeBSD's getsockaddr() rejects an AF_UNIX address whose length
         * exceeds its own sizeof(struct sockaddr_un) with EINVAL — this is
         * the actual, confirmed cause (namelen=106 succeeds, 110 fails,
         * same destination path, same socket). Re-pack unconditionally
         * into a correctly-sized native struct. */
        if (mh->msg_name != NULL && mh->msg_namelen > 2) {
            size_t path_len = mh->msg_namelen - 2;
            if (path_len > sizeof(fixed_addr.sun_path) - 1) {
                path_len = sizeof(fixed_addr.sun_path) - 1;
            }
            memset(&fixed_addr, 0, sizeof(fixed_addr));
            memcpy(fixed_addr.sun_path, (char *)mh->msg_name + 2, path_len);
            fixed_addr.sun_family = AF_UNIX;
            fixed_addr.sun_len = (unsigned char)
                (offsetof(struct sockaddr_un, sun_path) + path_len);
            mh->msg_name = &fixed_addr;
            mh->msg_namelen = fixed_addr.sun_len;
        }

        long r = freebsd_raw_syscall(SYS_sendmsg, a1, a2, a3, 0, 0, 0);

        mh->msg_name = orig_name;
        mh->msg_namelen = orig_namelen;
        return r;
    }
    case LINUX_SYS_recvmsg: {
        /* Mirrors LINUX_SYS_sendmsg's msg_name handling: msg_iov/msg_control
         * share byte offsets between the two OSes so they're left alone,
         * but struct sockaddr_un's CONTENT layout differs (Linux: 2-byte
         * sun_family then sun_path; FreeBSD: 1-byte sun_len, 1-byte
         * sun_family, then sun_path), so a peer address FreeBSD writes
         * natively would be misread by the Linux caller. Substitute a
         * native-sized scratch buffer for the call, then translate whatever
         * FreeBSD actually wrote back into Linux's layout (truncated to
         * the caller's original buffer size) before returning. NULL
         * msg_name (the common case for an already-connected socket, which
         * is all this RPC transport ever uses) is untouched passthrough. */
        struct msghdr *mh = (struct msghdr *)a2;
        void *orig_name = mh->msg_name;
        socklen_t orig_namelen = mh->msg_namelen;
        struct sockaddr_un native_addr;

        if (orig_name != NULL && orig_namelen > 0) {
            memset(&native_addr, 0, sizeof(native_addr));
            mh->msg_name = &native_addr;
            mh->msg_namelen = sizeof(native_addr);
        }

        long r = freebsd_raw_syscall(SYS_recvmsg, a1, a2, a3, 0, 0, 0);

        if (orig_name != NULL && orig_namelen > 0) {
            if (r >= 0 && mh->msg_namelen >= offsetof(struct sockaddr_un, sun_path)) {
                size_t path_len = mh->msg_namelen - offsetof(struct sockaddr_un, sun_path);
                size_t avail = (orig_namelen > 2) ? (size_t)orig_namelen - 2 : 0;
                if (path_len > avail) path_len = avail;

                uint8_t *out = (uint8_t *)orig_name;
                if (orig_namelen >= 2) {
                    out[0] = AF_UNIX;
                    out[1] = 0;
                }
                if (path_len > 0) {
                    memmove(out + 2, native_addr.sun_path, path_len);
                }
                mh->msg_namelen = (socklen_t)(2 + path_len);
            } else {
                mh->msg_namelen = 0;
            }
            mh->msg_name = orig_name;
        }

        return r;
    }
    case LINUX_SYS_poll:
        /* poll(fds, nfds, timeout) — same argument shape on both sides.
         * The guest's poll(395) lands in the overlay's sys_pselect_nocancel
         * which forwards here; a run showed exactly two "unhandled Linux
         * syscall 7" lines — the probe's main and spawned poll legs. */
        return freebsd_raw_syscall(SYS_poll, a1, a2, a3, 0, 0, 0);
    case LINUX_SYS_socket:
        /* AF_UNIX/AF_INET/AF_INET6 and SOCK_STREAM/SOCK_DGRAM share the same
         * numeric values on Linux and FreeBSD (both trace back to 4.4BSD),
         * same as MACOS_SYS_socket above — plain passthrough. */
        return freebsd_raw_syscall(SYS_socket, a1, a2, a3, 0, 0, 0);
    case LINUX_SYS_setsockopt:
        /* SOL_SOCKET and the common SO_ / IPPROTO_TCP option numbers this
         * codebase actually uses also share values with FreeBSD, same
         * BSD lineage as socket() above. */
        return freebsd_raw_syscall(SYS_setsockopt, a1, a2, a3, a4, a5, 0);
    case LINUX_SYS_epoll_create:
    case LINUX_SYS_epoll_create1:
        /* FreeBSD has no epoll — kqueue is the native readiness-notification
         * primitive, and the epoll fd is a kqueue fd throughout (see
         * epoll_ctl and epoll_wait below). epoll_create's size hint and
         * epoll_create1's flags are both dropped: kqueue sizes itself, and
         * the only defined flag, EPOLL_CLOEXEC, has no kqueue equivalent to
         * set at creation. */
        return freebsd_raw_syscall(SYS_kqueue, 0, 0, 0, 0, 0, 0);
    case LINUX_SYS_epoll_ctl: {
        /* Translate onto the kqueue fd LINUX_SYS_epoll_create1 handed back
         * above. Only EPOLLIN/EPOLLOUT are translated (the only readiness
         * bits this codebase's socket/notification setup registers) —
         * anything else is silently dropped rather than rejected, matching
         * this file's general stance of covering the calls actually seen
         * in practice over a from-scratch semantic redesign. */
        struct linux_epoll_event {
            uint32_t events;
            uint64_t data;
        } __attribute__((packed));

        enum { LINUX_EPOLL_CTL_ADD = 1, LINUX_EPOLL_CTL_DEL = 2, LINUX_EPOLL_CTL_MOD = 3 };
        enum { LINUX_EPOLLIN = 0x001, LINUX_EPOLLOUT = 0x004 };

        int epfd = (int)a1;
        int op = (int)a2;
        int fd = (int)a3;
        struct linux_epoll_event *ev = (struct linux_epoll_event *)a4;
        struct kevent kev[2];
        int n = 0;

        if (op == LINUX_EPOLL_CTL_DEL) {
            EV_SET(&kev[n++], fd, EVFILT_READ, EV_DELETE, 0, 0, NULL);
            EV_SET(&kev[n++], fd, EVFILT_WRITE, EV_DELETE, 0, 0, NULL);
        } else {
            if (ev == NULL)
                return -EFAULT;
            /* ADD and MOD both map to EV_ADD — kevent(2) upserts registrations. */
            if (ev->events & LINUX_EPOLLIN)
                EV_SET(&kev[n++], fd, EVFILT_READ, EV_ADD, 0, 0, (void *)(uintptr_t)ev->data);
            if (ev->events & LINUX_EPOLLOUT)
                EV_SET(&kev[n++], fd, EVFILT_WRITE, EV_ADD, 0, 0, (void *)(uintptr_t)ev->data);
        }

        if (n == 0)
            return 0;

        long r = freebsd_raw_syscall(SYS_kevent, epfd, (long)kev, n, 0, 0, 0);
        if (r < 0 && op == LINUX_EPOLL_CTL_DEL)
            return 0; /* deleting a filter that was never added is a no-op */
        return (r < 0) ? r : 0;
    }
    case LINUX_SYS_epoll_wait: {
        /*
         * The missing half of the epoll-over-kqueue shim: epoll_create and
         * epoll_ctl were translated but the actual wait was not, so callers
         * registered their interest and then got ENOSYS forever. libdispatch
         * spins on this — `defaults` produced an unbroken stream of
         * "unhandled Linux syscall 232" once it got as far as its event loop.
         *
         * The epoll fd IS the kqueue fd (see epoll_create above), so this is
         * a plain kevent() with no changelist, plus a translation of the
         * returned events back into Linux's layout.
         */
        struct linux_epoll_event {
            uint32_t events;
            uint64_t data;
        } __attribute__((packed));
        enum {
            LINUX_EPOLLIN  = 0x001,
            LINUX_EPOLLOUT = 0x004,
            LINUX_EPOLLERR = 0x008,
            LINUX_EPOLLHUP = 0x010,
        };

        int epfd = (int)a1;
        struct linux_epoll_event *out = (struct linux_epoll_event *)a2;
        int maxevents = (int)a3;
        int timeout_ms = (int)a4;

        if (out == NULL)
            return -EFAULT;
        if (maxevents <= 0)
            return -EINVAL;

        /* Bounded so the changelist stays on the stack — this runs in the
         * SIGILL handler. Returning fewer events than maxevents is explicitly
         * allowed by epoll_wait's contract (the rest are reported on the next
         * call), so this caps the batch, not the results. */
        struct kevent kev[64];
        if (maxevents > (int)(sizeof(kev) / sizeof(kev[0])))
            maxevents = (int)(sizeof(kev) / sizeof(kev[0]));

        /* Linux: negative timeout blocks indefinitely, 0 returns immediately.
         * kevent expresses "block indefinitely" as a NULL timespec. */
        struct timespec ts;
        struct timespec *tsp = NULL;
        if (timeout_ms >= 0) {
            ts.tv_sec  = timeout_ms / 1000;
            ts.tv_nsec = (long)(timeout_ms % 1000) * 1000000L;
            tsp = &ts;
        }

        long n = freebsd_raw_syscall(SYS_kevent, epfd, 0, 0,
                                     (long)kev, maxevents, (long)tsp);
        if (n < 0)
            return n;

        /*
         * MLDR_LOG_EPOLL exists because the guest's own logging cannot be used
         * to debug this: libkqueue's KQUEUE_DEBUG output travels to the host
         * through darlingserver's Kprintf RPC, and two of those from one thread
         * trip Thread::setPendingCall's "pending call overwritten while active"
         * throw, killing darlingserver before the thing under investigation
         * happens. Logging from this side has no such feedback loop.
         */
        static int log_epoll = -1;
        if (log_epoll < 0)
            log_epoll = (getenv("MLDR_LOG_EPOLL") != NULL);
        if (log_epoll) {
            fprintf(stderr, "[darling-mldr] epoll_wait(kq=%d) -> %ld event(s)\n",
                    epfd, n);
            for (long i = 0; i < n; i++) {
                fprintf(stderr,
                    "    ident=%ld filter=%d flags=0x%x fflags=0x%x"
                    " data=%ld udata=%p\n",
                    (long)kev[i].ident, (int)kev[i].filter,
                    (unsigned)kev[i].flags, (unsigned)kev[i].fflags,
                    (long)kev[i].data, kev[i].udata);
            }
            fflush(stderr);
        }

        for (long i = 0; i < n; i++) {
            uint32_t events = 0;

            if (kev[i].filter == EVFILT_READ)
                events |= LINUX_EPOLLIN;
            else if (kev[i].filter == EVFILT_WRITE)
                events |= LINUX_EPOLLOUT;

            /* EV_EOF is a state flag on a live event, not an error; EV_ERROR
             * carries the failure in kev.data. Both have direct epoll
             * counterparts that callers already handle. */
            if (kev[i].flags & EV_EOF)
                events |= LINUX_EPOLLHUP;
            if (kev[i].flags & EV_ERROR)
                events |= LINUX_EPOLLERR;

            out[i].events = events;
            out[i].data   = (uint64_t)(uintptr_t)kev[i].udata;
        }

        return n;
    }
    case LINUX_SYS_fsync:
        return freebsd_raw_syscall(SYS_fsync, a1, 0, 0, 0, 0, 0);
    case LINUX_SYS_fdatasync:
        /* FreeBSD has fdatasync(2) proper, so this is not the usual
         * "fall back to fsync" approximation. */
        return freebsd_raw_syscall(SYS_fdatasync, a1, 0, 0, 0, 0, 0);
    case LINUX_SYS_ftruncate:
        /* ftruncate(fd, length) — same call, same order, same width on both
         * OSes. Without this case a guest's ftruncate fell through to the
         * ENOSYS arm, which a window probe reads as "shm pool allocation
         * failed: Invalid argument" — a shm problem that is really a missing
         * line in this list. 76 is unused on x86_64, so 75 and 77 are
         * neighbours here. */
        return freebsd_raw_syscall(SYS_ftruncate, a1, a2, 0, 0, 0, 0);
    case LINUX_SYS_mkdirat:
        /* Same call on both, same argument order, and the mode bits for the
         * permission half coincide — `defaults` needs this to create its
         * preferences directory. */
        return freebsd_raw_syscall(SYS_mkdirat, a1, a2, a3, 0, 0, 0);
    case LINUX_SYS_fchmodat: {
        /* The AT_* flag values do NOT coincide: Linux AT_SYMLINK_NOFOLLOW is
         * 0x100 (which is FreeBSD's AT_EACCESS) and FreeBSD's is 0x200 — the
         * same mismatch already handled for newfstatat and faccessat. Passing
         * the flags through would silently mean something else. */
        long freebsd_flags = 0;
        if (a4 & 0x100)
            freebsd_flags |= AT_SYMLINK_NOFOLLOW;
        return freebsd_raw_syscall(SYS_fchmodat, a1, a2, a3, freebsd_flags, 0, 0);
    }
    case LINUX_SYS_statfs:
    case LINUX_SYS_fstatfs: {
        /*
         * Both OSes have the call, but the structures share neither layout nor
         * field widths (Linux: long-sized counters, f_namelen/f_frsize;
         * FreeBSD: explicitly-sized 64-bit counters plus mount metadata), so
         * this has to be copied field by field rather than passed through —
         * the same reasoning as the stat translation above.
         */
        struct linux_statfs {
            int64_t  f_type;
            int64_t  f_bsize;
            uint64_t f_blocks;
            uint64_t f_bfree;
            uint64_t f_bavail;
            uint64_t f_files;
            uint64_t f_ffree;
            int32_t  f_fsid[2];
            int64_t  f_namelen;
            int64_t  f_frsize;
            int64_t  f_flags;
            int64_t  f_spare[4];
        };

        struct linux_statfs *out = (struct linux_statfs *)a2;
        if (out == NULL)
            return -EFAULT;

        struct statfs nsb;
        long r = (linux_nr == LINUX_SYS_fstatfs)
            ? freebsd_raw_syscall(SYS_fstatfs, a1, (long)&nsb, 0, 0, 0, 0)
            : freebsd_raw_syscall(SYS_statfs, a1, (long)&nsb, 0, 0, 0, 0);
        if (r < 0)
            return r;

        memset(out, 0, sizeof(*out));
        /* f_type is a filesystem magic number on Linux and a small enum on
         * FreeBSD; there is no meaningful mapping, and callers here use it
         * only to special-case particular filesystems. Passing FreeBSD's
         * value through is more honest than inventing a Linux magic. */
        out->f_type    = (int64_t)nsb.f_type;
        out->f_bsize   = (int64_t)nsb.f_bsize;
        out->f_blocks  = nsb.f_blocks;
        out->f_bfree   = nsb.f_bfree;
        out->f_bavail  = (uint64_t)nsb.f_bavail;
        out->f_files   = nsb.f_files;
        out->f_ffree   = (uint64_t)nsb.f_ffree;
        out->f_fsid[0] = nsb.f_fsid.val[0];
        out->f_fsid[1] = nsb.f_fsid.val[1];
        out->f_namelen = (int64_t)nsb.f_namemax;
        out->f_frsize  = (int64_t)nsb.f_bsize;
        /* f_flags mount-flag bits differ between the two OSes; leaving it
         * zero says "no special flags" rather than asserting the wrong ones. */
        return 0;
    }
    case LINUX_SYS_timerfd_create:
        /*
         * libdispatch builds every timer on a timerfd, so without this it
         * takes ENOSYS on its first scheduled work item and aborts — the last
         * thing standing between `defaults` and running to completion.
         *
         * A kqueue fd is the closest thing FreeBSD has: it holds the timer
         * (EVFILT_TIMER, armed by timerfd_settime below), it is pollable, and
         * it nests inside the outer kqueue the epoll shim uses, so an event
         * loop watching this "timerfd" for readability behaves as it would on
         * Linux. read() on it is intercepted (see LINUX_SYS_read) because the
         * kernel cannot serve a read from a kqueue.
         *
         * clockid is dropped: EVFILT_TIMER runs on the monotonic clock, so a
         * CLOCK_REALTIME timerfd will not observe wall-clock steps. Nothing
         * here asks for that, and the alternative is failing the call.
         */
        {
            long fd = freebsd_raw_syscall(SYS_kqueue, 0, 0, 0, 0, 0, 0);
            if (fd >= 0)
                mldr_timerfd_track((int)fd, true);
            return fd;
        }
    case LINUX_SYS_timerfd_settime: {
        enum { LINUX_TFD_TIMER_ABSTIME = 1 };
        struct linux_itimerspec {
            struct timespec it_interval;
            struct timespec it_value;
        };

        int fd = (int)a1;
        int flags = (int)a2;
        const struct linux_itimerspec *nv = (const struct linux_itimerspec *)a3;
        struct linux_itimerspec *ov = (struct linux_itimerspec *)a4;

        if (!mldr_is_timerfd(fd))
            return -EINVAL;
        if (nv == NULL)
            return -EFAULT;

        /* Reporting the previous setting would need per-fd bookkeeping this
         * shim does not keep; zero it rather than hand back a stale stack
         * value, and document that old_value always reads as disarmed. */
        if (ov != NULL)
            memset(ov, 0, sizeof(*ov));

        struct kevent kev;

        /* it_value == 0 disarms the timer, in Linux and here alike. */
        if (nv->it_value.tv_sec == 0 && nv->it_value.tv_nsec == 0) {
            EV_SET(&kev, 1, EVFILT_TIMER, EV_DELETE, 0, 0, NULL);
            long r = freebsd_raw_syscall(SYS_kevent, fd, (long)&kev, 1,
                                         0, 0, 0);
            /* Disarming a timer that was never armed is a no-op, not an
             * error — same reasoning as EPOLL_CTL_DEL above. */
            return (r < 0) ? 0 : 0;
        }

        int64_t first_ns = (int64_t)nv->it_value.tv_sec * 1000000000LL
                         + (int64_t)nv->it_value.tv_nsec;

        if (flags & LINUX_TFD_TIMER_ABSTIME) {
            /* This sys/event.h has no NOTE_ABSTIME, so an absolute deadline
             * has to be turned into a delay from now. */
            struct timespec now;
            long r = freebsd_raw_syscall(SYS_clock_gettime, CLOCK_REALTIME,
                                         (long)&now, 0, 0, 0, 0);
            if (r < 0)
                return r;

            first_ns -= (int64_t)now.tv_sec * 1000000000LL
                      + (int64_t)now.tv_nsec;
        }

        /* A deadline already past must still fire; kqueue treats 0 as "as
         * soon as possible" only for some filters, so clamp to 1ns. */
        if (first_ns < 1)
            first_ns = 1;

        int64_t interval_ns = (int64_t)nv->it_interval.tv_sec * 1000000000LL
                            + (int64_t)nv->it_interval.tv_nsec;

        if (interval_ns > 0) {
            /*
             * EVFILT_TIMER repeats on a single period and cannot express
             * Linux's "first at it_value, then every it_interval" when those
             * differ. The period wins, so a repeating timer's FIRST fire
             * happens at it_interval instead of it_value; every fire after
             * that is correct. libdispatch arms one-shot timers and re-arms
             * them itself, so this path is the uncommon one — but the skew is
             * real, and a caller depending on the first interval would see it.
             */
            EV_SET(&kev, 1, EVFILT_TIMER, EV_ADD | EV_ENABLE,
                   NOTE_NSECONDS, (int64_t)interval_ns, NULL);
        } else {
            EV_SET(&kev, 1, EVFILT_TIMER, EV_ADD | EV_ENABLE | EV_ONESHOT,
                   NOTE_NSECONDS, (int64_t)first_ns, NULL);
        }

        long r = freebsd_raw_syscall(SYS_kevent, fd, (long)&kev, 1, 0, 0, 0);
        return (r < 0) ? r : 0;
    }
    case LINUX_SYS_timerfd_gettime:
        /*
         * Reporting the time remaining would need the per-fd bookkeeping this
         * shim deliberately avoids — kqueue does not expose a timer's residual
         * delay. Fail loudly rather than return a plausible-looking zero that
         * a caller would read as "timer disarmed".
         */
        fprintf(stderr,
            "[darling-mldr] timerfd_gettime is not implemented (kqueue exposes"
            " no residual delay) — ENOSYS\n");
        return -ENOSYS;
    case LINUX_SYS_futex: {
        /*
         * futex(2) -> _umtx_op(2). This is what libpthread and libdispatch
         * block on inside the guest, so nothing multi-threaded gets past its
         * first contended lock without it: `defaults` reached Foundation's
         * initializer, libdispatch started a worker, and every futex came back
         * ENOSYS from here.
         *
         * FreeBSD's UMTX_OP_WAIT_UINT_PRIVATE has the same "compare the word,
         * then sleep if it still matches" contract as FUTEX_WAIT, which is the
         * part that has to be atomic and therefore the part that can't be
         * emulated from userspace.
         *
         * Deliberate, documented divergences:
         *  - A mismatched value makes FreeBSD return success-without-sleeping
         *    where Linux returns EAGAIN. Callers treat that as a spurious
         *    wakeup and re-check their own condition, which is behaviour a
         *    futex caller must tolerate anyway.
         *  - FUTEX_WAKE returns the number of threads woken on Linux;
         *    _umtx_op only reports success/failure, so this returns 0 rather
         *    than inventing a count.
         *  - The *_BITSET variants map onto the plain wait/wake ops with the
         *    bitset ignored, which is exact for FUTEX_BITSET_MATCH_ANY (what
         *    callers here pass) and over-broad for anything else: a wake could
         *    reach a waiter whose bits didn't match. Still a legal spurious
         *    wakeup, and the alternative is failing the call outright.
         *  - PRIVATE vs shared is not distinguished: the _PRIVATE ops are used
         *    for both. Cross-process futexes on a shared mapping would not be
         *    woken; nothing in this process's guest stack uses one.
         */
        enum {
            LINUX_FUTEX_WAIT           = 0,
            LINUX_FUTEX_WAKE           = 1,
            LINUX_FUTEX_WAIT_BITSET    = 9,
            LINUX_FUTEX_WAKE_BITSET    = 10,
            LINUX_FUTEX_PRIVATE_FLAG   = 128,
            LINUX_FUTEX_CLOCK_REALTIME = 256,
        };

        int futex_op = (int)a2
            & ~(LINUX_FUTEX_PRIVATE_FLAG | LINUX_FUTEX_CLOCK_REALTIME);
        bool want_realtime = ((int)a2 & LINUX_FUTEX_CLOCK_REALTIME) != 0;

        switch (futex_op) {
        case LINUX_FUTEX_WAIT:
        case LINUX_FUTEX_WAIT_BITSET: {
            const struct timespec *lin_ts = (const struct timespec *)a4;
            struct _umtx_time ut;
            long ut_size = 0;
            void *ut_ptr = NULL;

            if (lin_ts != NULL) {
                /* Plain FUTEX_WAIT's timeout is relative; FUTEX_WAIT_BITSET's
                 * is absolute. UMTX_ABSTIME carries exactly that distinction,
                 * and getting it backwards would turn a short sleep into one
                 * that either never expires or expires immediately. */
                ut._timeout = *lin_ts;
                ut._flags   = (futex_op == LINUX_FUTEX_WAIT_BITSET)
                              ? UMTX_ABSTIME : 0;
                ut._clockid = want_realtime ? CLOCK_REALTIME : CLOCK_MONOTONIC;
                ut_size = (long)sizeof(ut);
                ut_ptr  = &ut;
            }

            return freebsd_raw_syscall(SYS__umtx_op, a1,
                UMTX_OP_WAIT_UINT_PRIVATE, (long)(uint32_t)a3,
                ut_size, (long)ut_ptr, 0);
        }
        case LINUX_FUTEX_WAKE:
        case LINUX_FUTEX_WAKE_BITSET: {
            long r = freebsd_raw_syscall(SYS__umtx_op, a1,
                UMTX_OP_WAKE_PRIVATE, (long)(uint32_t)a3, 0, 0, 0);
            return (r < 0) ? r : 0;
        }
        default:
            /* REQUEUE/CMP_REQUEUE/WAKE_OP/priority-inheritance ops have no
             * _umtx_op equivalent that preserves their semantics. Fail loudly
             * rather than silently mistranslating a lock handoff. */
            fprintf(stderr,
                "[darling-mldr] futex op %d (raw 0x%lx) unimplemented"
                " — ENOSYS\n", futex_op, (unsigned long)a2);
            return -ENOSYS;
        }
    }
    case LINUX_SYS_sendto:
    case LINUX_SYS_recvfrom:
        /*
         * The guest answers a server-to-client call with __NR_sendto (see the
         * S2C handling in its dserver-rpc-defs.h receive hook), and without
         * these it would get ENOSYS there and return a negative out of the
         * receive path — turning a serviceable S2C request into a failed RPC.
         * No S2C call has appeared in a trace yet, so this is completing the
         * socket family rather than fixing an observed failure; the six-arg
         * form is BSD-lineage and identical on both sides, unlike the flag and
         * struct translations elsewhere in this file.
         */
        return freebsd_raw_syscall(
            (linux_nr == LINUX_SYS_sendto) ? SYS_sendto : SYS_recvfrom,
            a1, a2, a3, a4, a5, a6);
    case LINUX_SYS_connect: {
        /* gsw-bisect: the guest's connect() draws EAFNOSUPPORT at the
         * host-facing call while the overlay's sockaddr_fixup_from_bsd
         * looks layout-correct on paper (bsd_family at offset 1, sun_path
         * at 2). A run must decide which side lies, so print the sockaddr
         * bytes this trap actually receives: garbage here means the break
         * is upstream of the trap (overlay fixup / vchroot_expand /
         * per-thread wd), clean bytes mean it is at or below the host
         * call. */
        const unsigned char *sb = (const unsigned char *)(long)a2;
        fprintf(stderr,
                "[gsw-bisect] connect fd=%ld len=%ld bytes=%02x %02x %02x %02x\n",
                (long)a1, (long)a3,
                ((long)a3 >= 1 && sb != NULL) ? sb[0] : 0,
                ((long)a3 >= 2 && sb != NULL) ? sb[1] : 0,
                ((long)a3 >= 3 && sb != NULL) ? sb[2] : 0,
                ((long)a3 >= 4 && sb != NULL) ? sb[3] : 0);
        return freebsd_raw_syscall(SYS_connect, a1, a2, a3, 0, 0, 0);
    }
    case LINUX_SYS_bind: {
        /* The guest's overlay sys_bind forwards here (LINUX 49); without
         * this case it drew ENOSYS, which is why the guest could not open
         * a listening port itself. Same BSD-lineage shape as connect. The
         * byte print stays until the EAFNOSUPPORT this call used to draw
         * at the host is attributed: the overlay's fixup looks clean on
         * paper (linux_family = 2 for AF_INET), so a run must show what
         * actually arrives. */
        const unsigned char *sb = (const unsigned char *)(long)a2;
        fprintf(stderr,
                "[gsw-bisect] bind fd=%ld len=%ld bytes=%02x %02x %02x %02x\n",
                (long)a1, (long)a3,
                ((long)a3 >= 1 && sb != NULL) ? sb[0] : 0,
                ((long)a3 >= 2 && sb != NULL) ? sb[1] : 0,
                ((long)a3 >= 3 && sb != NULL) ? sb[2] : 0,
                ((long)a3 >= 4 && sb != NULL) ? sb[3] : 0);
        return freebsd_raw_syscall(SYS_bind, a1, a2, a3, 0, 0, 0);
    }
    case LINUX_SYS_listen:
        /* Completes the self-listener alongside bind: listen(2) and
         * accept(2) are shape-identical BSD-lineage calls. */
        return freebsd_raw_syscall(SYS_listen, a1, a2, 0, 0, 0, 0);
    case LINUX_SYS_accept:
        return freebsd_raw_syscall(SYS_accept, a1, a2, a3, 0, 0, 0);
    case LINUX_SYS_socketpair:
        /* The guest's overlay sys_socketpair forwards here (LINUX 53) and
         * drew ENOSYS before this case existed — the reason the guest had
         * no in-process AF_UNIX source at all. FreeBSD socketpair(2) takes
         * the same four arguments and writes the two fds straight into the
         * caller's array, which is the same address space. This is the
         * source guest-socket-wait's socketpair leg needs. */
        return freebsd_raw_syscall(SYS_socketpair, a1, a2, a3, a4, 0, 0);
    case LINUX_SYS_fcntl:
        /* Same BSD-lineage passthrough as MACOS_SYS_fcntl above. */
        return freebsd_raw_syscall(SYS_fcntl, a1, a2, a3, 0, 0, 0);
    case LINUX_SYS_eventfd: {
        /* FreeBSD has no eventfd(2) at all — no native counter-semantics fd
         * exists to translate this to. socketpair(2) gives back a real,
         * valid fd that (unlike a pipe's two one-directional ends) supports
         * both read() and write() on the SAME fd, which is the one property
         * callers actually depend on when they only use an eventfd as a
         * generic self-wakeup handle rather than for its exact add-to-counter
         * semantics. initval (a1) is dropped — nothing here recreates the
         * counter behavior, just a working bidirectional fd. */
        int fds[2];
        long r = freebsd_raw_syscall(SYS_socketpair, AF_UNIX, SOCK_STREAM, 0, (long)fds, 0, 0);
        if (r < 0)
            return r;

        /* Both ends stay open — see the eventfd registry comment. Writes to
         * the returned fd are redirected to the peer (LINUX_SYS_write) so the
         * bytes land in this fd's own receive queue and it becomes readable,
         * which is the wakeup an eventfd exists to deliver. */
        mldr_eventfd_track(fds[0], fds[1], true);
        return fds[0];
    }
    case LINUX_SYS_sigaltstack: {
        /* Linux struct sigaltstack: {void*ss_sp; int ss_flags; size_t ss_size;}
         * FreeBSD stack_t:           {void*ss_sp; size_t ss_size; int ss_flags;}
         * Field ORDER differs, and the ss_flags BIT VALUES differ too
         * (LINUX_SS_ONSTACK/LINUX_SS_DISABLE vs FreeBSD's SS_ONSTACK/
         * SS_DISABLE) — a blind pass-through would corrupt both the size
         * and the on-stack/disable flags. */
        struct linux_sigaltstack {
            void *ss_sp;
            int ss_flags;
            unsigned long ss_size;
        };
        struct linux_sigaltstack *lin_ss  = (struct linux_sigaltstack *)a1;
        struct linux_sigaltstack *lin_oss = (struct linux_sigaltstack *)a2;
        stack_t native_ss, native_oss;
        stack_t *pss = NULL, *poss = NULL;

        if (lin_ss != NULL) {
            native_ss.ss_sp = lin_ss->ss_sp;
            native_ss.ss_size = lin_ss->ss_size;
            native_ss.ss_flags = 0;
            if (lin_ss->ss_flags & LINUX_SS_ONSTACK) native_ss.ss_flags |= SS_ONSTACK;
            if (lin_ss->ss_flags & LINUX_SS_DISABLE) native_ss.ss_flags |= SS_DISABLE;
            pss = &native_ss;
        }
        if (lin_oss != NULL) {
            poss = &native_oss;
        }

        long r = freebsd_raw_syscall(SYS_sigaltstack, (long)pss, (long)poss, 0, 0, 0, 0);

        if (r >= 0 && lin_oss != NULL) {
            lin_oss->ss_sp = native_oss.ss_sp;
            lin_oss->ss_size = native_oss.ss_size;
            lin_oss->ss_flags = 0;
            if (native_oss.ss_flags & SS_ONSTACK) lin_oss->ss_flags |= LINUX_SS_ONSTACK;
            if (native_oss.ss_flags & SS_DISABLE) lin_oss->ss_flags |= LINUX_SS_DISABLE;
        }
        return r;
    }
    case LINUX_SYS_getrandom:
        /* Identical signature (buf, buflen, flags) AND identical GRND_*
         * bit values on both OSes — pure passthrough. */
        return freebsd_raw_syscall(SYS_getrandom, a1, a2, a3, 0, 0, 0);
    case LINUX_SYS_prlimit64: {
        /*
         * prlimit64(pid, resource, new_limit, old_limit). struct rlimit64 is
         * binary-identical on both OSes (two 8-byte {rlim_cur, rlim_max}
         * fields), but the RESOURCE NUMBERS diverge from index 5 onward
         * (Linux RSS=5,NPROC=6,NOFILE=7,MEMLOCK=8,AS=9 vs FreeBSD
         * RSS=5,MEMLOCK=6,NPROC=7,NOFILE=8,...,AS=10) — passing the number
         * through would silently target the wrong limit. Only self (pid==0)
         * is handled: that's the only case dyld/libSystem's startup path
         * exercises (querying/adjusting the calling thread's own stack
         * limit), and FreeBSD's getrlimit/setrlimit have no "target another
         * pid" form to translate the general case into anyway.
         */
        if (a1 != 0) {
            return -ESRCH;
        }
        static const int resource_map[] = {
            /* Linux index -> FreeBSD RLIMIT_* */
            RLIMIT_CPU,      /* 0 */
            RLIMIT_FSIZE,    /* 1 */
            RLIMIT_DATA,     /* 2 */
            RLIMIT_STACK,    /* 3 */
            RLIMIT_CORE,     /* 4 */
            RLIMIT_RSS,      /* 5 */
            RLIMIT_NPROC,    /* 6 */
            RLIMIT_NOFILE,   /* 7 */
            RLIMIT_MEMLOCK,  /* 8 */
            RLIMIT_AS,       /* 9 */
        };
        if (a2 >= (long)(sizeof(resource_map) / sizeof(resource_map[0]))) {
            return -EINVAL;
        }
        int freebsd_resource = resource_map[a2];
        struct rlimit rl;
        if (a4 != 0) {
            if (getrlimit(freebsd_resource, &rl) < 0) {
                return -errno;
            }
            memcpy((void *)a4, &rl, sizeof(rl));
        }
        if (a3 != 0) {
            memcpy(&rl, (const void *)a3, sizeof(rl));
            if (setrlimit(freebsd_resource, &rl) < 0) {
                return -errno;
            }
        }
        return 0;
    }
    case LINUX_SYS_faccessat: {
        /* faccessat(dirfd, path, mode, flags). R_OK/W_OK/X_OK/F_OK coincide,
         * but the AT_* flag VALUES are swapped between the two OSes:
         * Linux AT_SYMLINK_NOFOLLOW=0x100 / AT_EACCESS=0x200, FreeBSD
         * AT_EACCESS=0x100 / AT_SYMLINK_NOFOLLOW=0x200. Passing them through
         * would silently turn one into the other. */
        long freebsd_atflags = 0;
        if (a4 & 0x100) freebsd_atflags |= AT_SYMLINK_NOFOLLOW;
        if (a4 & 0x200) freebsd_atflags |= AT_EACCESS;
        return freebsd_raw_syscall(SYS_faccessat, a1, a2, a3, freebsd_atflags, 0, 0);
    }
    case LINUX_SYS_getdents64: {
        /* Linux struct linux_dirent64 { u64 d_ino; s64 d_off; u16 d_reclen;
         * u8 d_type; char d_name[]; } puts the name at offset 19; FreeBSD's
         * ino64 struct dirent carries an extra d_namlen (plus padding) and
         * puts it at 24. Layouts differ, so entries must be repacked rather
         * than passed through.
         *
         * A Linux record is never larger than the FreeBSD record it came
         * from, so reading at most the caller's buffer size guarantees the
         * repacked result fits and no directory entry is ever consumed from
         * the fd without being handed back. */
        char nbuf[4096];
        size_t want = (size_t)a3;
        if (want > sizeof(nbuf))
            want = sizeof(nbuf);
        long r = freebsd_raw_syscall(SYS_getdirentries, a1, (long)nbuf,
                                     (long)want, 0, 0, 0);
        if (r <= 0)
            return r;

        uint8_t *out = (uint8_t *)a2;
        size_t in_off = 0, out_off = 0;
        while (in_off + sizeof(struct dirent) <= (size_t)r + offsetof(struct dirent, d_name)) {
            const struct dirent *nd = (const struct dirent *)(nbuf + in_off);
            if (in_off >= (size_t)r || nd->d_reclen == 0)
                break;
            size_t namelen = nd->d_namlen;
            size_t reclen = (19 + namelen + 1 + 7) & ~(size_t)7;
            if (out_off + reclen > (size_t)a3)
                break;
            uint8_t *e = out + out_off;
            uint64_t d_ino = nd->d_fileno;
            int64_t  d_off = nd->d_off;
            uint16_t d_rec = (uint16_t)reclen;
            memcpy(e +  0, &d_ino, sizeof(d_ino));
            memcpy(e +  8, &d_off, sizeof(d_off));
            memcpy(e + 16, &d_rec, sizeof(d_rec));
            e[18] = nd->d_type;
            memcpy(e + 19, nd->d_name, namelen);
            e[19 + namelen] = '\0';
            out_off += reclen;
            in_off  += nd->d_reclen;
        }
        /* Returning 0 means end-of-directory, so never report it while native
         * entries are still pending — that would silently truncate the listing. */
        if (out_off == 0)
            return -EINVAL;
        return (long)out_off;
    }
    case LINUX_SYS_getcpu:
        /* getcpu(cpu, node, tcache) — FreeBSD has no equivalent syscall, and
         * callers use it only as a sharding/affinity hint, so reporting CPU 0
         * / NUMA node 0 is always a correct (if pessimal) answer. The third
         * argument has been ignored by Linux itself since 2.6.24. */
        if (a1 != 0)
            *(unsigned int *)a1 = 0;
        if (a2 != 0)
            *(unsigned int *)a2 = 0;
        return 0;
    case LINUX_SYS_gettid:
        /* FreeBSD has no 1:1 gettid; thr_self(2) returns the equivalent
         * lightweight-thread id via an out-pointer. */
        {
            long tid = 0;
            long r = freebsd_raw_syscall(SYS_thr_self, (long)&tid, 0, 0, 0, 0, 0);
            return (r < 0) ? r : tid;
        }
    case LINUX_SYS_wait4:
        return freebsd_raw_syscall(SYS_wait4, a1, a2, a3, a4, 0, 0);
    case LINUX_SYS_kill:
        return freebsd_raw_syscall(SYS_kill, a1, a2, 0, 0, 0, 0);
    case LINUX_SYS_rt_sigaction:
        /* Was a passthrough, which cannot work: the two struct sigaction
         * layouts differ (Linux carries sa_restorer and an 8-byte mask,
         * FreeBSD has no restorer and a 16-byte mask), no sa_flags bit value
         * coincides, and signal numbers diverge above 15. It failed with
         * EINVAL, which is why the guest's sigexc machinery — the path that
         * turns hardware faults into Mach exceptions — never installed a
         * handler. See sigaction_translate.c and docs/SPEC-signal-abi-bridge.md. */
        return mldr_sys_rt_sigaction((int)a1, (const void *)a2,
                                     (void *)a3, (size_t)a4);
    case LINUX_SYS_rt_sigprocmask: {
        /*
         * Passing this through was wrong in two ways at once, and the
         * consequences were not "signals behave oddly" but silent RPC
         * corruption.
         *
         * First, `how`: Linux numbers SIG_BLOCK/UNBLOCK/SETMASK 0/1/2, FreeBSD
         * numbers them 1/2/3. Linux's SIG_BLOCK therefore arrived as 0, which
         * FreeBSD rejects outright — so every attempt by the guest to block
         * signals failed with EINVAL and the mask was never applied. The
         * generated RPC layer wraps each call in exactly such a block
         * (dserver_rpc_hooks_atomic_begin/end) to keep a request and its reply
         * atomic. With the block silently failing, a signal could land
         * mid-call, its handler would issue its own RPC on the SAME per-thread
         * socket, and the outer call would then read the inner one's reply.
         * That surfaces as the caller getting -ECOMM (-70, a Linux errno: the
         * guest's RPC layer is a Linux build) while darlingserver's log shows
         * it answered that very call successfully — seen here on
         * psynch_mutexwait and semaphore_signal, and intermittent because it
         * depends on whether a signal fell inside the window.
         *
         * Second, the mask itself: Linux's sigset_t is 8 bytes, FreeBSD's is
         * 16. Handing the guest's pointer straight to the kernel means reading
         * 8 bytes of adjacent memory as part of the mask and, for oldset,
         * writing 16 bytes into the guest's 8-byte object.
         *
         * Signal NUMBERS also differ above 15 (Linux SIGUSR1=10 vs FreeBSD 30,
         * SIGCHLD 17 vs 20, SIGSTOP 19 vs 17, ...), so the bits are remapped
         * per signal rather than copied as a word.
         */
        enum { LINUX_SIG_BLOCK = 0, LINUX_SIG_UNBLOCK = 1, LINUX_SIG_SETMASK = 2 };

        int linux_how = (int)a1;
        const uint64_t *lin_set = (const uint64_t *)a2;
        uint64_t *lin_oldset = (uint64_t *)a3;
        int freebsd_how;

        switch (linux_how) {
        case LINUX_SIG_BLOCK:   freebsd_how = SIG_BLOCK;   break;
        case LINUX_SIG_UNBLOCK: freebsd_how = SIG_UNBLOCK; break;
        case LINUX_SIG_SETMASK: freebsd_how = SIG_SETMASK; break;
        default:
            /* Only meaningful when there is no set to apply; Linux allows a
             * query-only call with set == NULL and any how. */
            if (lin_set != NULL)
                return -EINVAL;
            freebsd_how = SIG_BLOCK;
            break;
        }

        sigset_t nset, oset;
        sigemptyset(&nset);
        sigemptyset(&oset);

        if (lin_set != NULL) {
            uint64_t bits = *lin_set;
            for (int linux_signo = 1; linux_signo <= 64; linux_signo++) {
                if ((bits & (1ULL << (linux_signo - 1))) == 0)
                    continue;
                int native = mldr_linux_signo_to_freebsd(linux_signo);
                if (native > 0)
                    sigaddset(&nset, native);
            }

            /*
             * SIGILL and SIGSYS are not the guest's to block: they are how
             * every guest syscall reaches mldr (the ud2-patched raw-syscall
             * trampolines and the macOS syscall trap respectively). The guest
             * asks to block "all signals" around each RPC, which before this
             * translation existed always failed with EINVAL and so was
             * harmless — now that the mask is really applied, honouring it
             * literally would disarm the syscall mechanism the guest is
             * itself using, in the middle of using it. Both are synchronous,
             * thread-generated signals, so blocking them does not defer
             * anything anyway: the kernel forces the default action and kills
             * the process.
             */
            sigdelset(&nset, SIGILL);
            sigdelset(&nset, SIGSYS);
        }

        long r = freebsd_raw_syscall(SYS_sigprocmask, freebsd_how,
                                     (lin_set != NULL) ? (long)&nset : 0,
                                     (long)&oset, 0, 0, 0);
        if (r < 0)
            return r;

        if (lin_oldset != NULL) {
            uint64_t bits = 0;
            for (int linux_signo = 1; linux_signo <= 64; linux_signo++) {
                int native = mldr_linux_signo_to_freebsd(linux_signo);
                if (native > 0 && sigismember(&oset, native))
                    bits |= 1ULL << (linux_signo - 1);
            }
            *lin_oldset = bits;
        }

        return 0;
    }

    case LINUX_SYS_exit:
        freebsd_raw_syscall(SYS__exit, a1, 0, 0, 0, 0, 0);
        __builtin_unreachable();
    case LINUX_SYS_exit_group:
        /* FreeBSD has no process-group exit_group; _exit(2) is the closest
         * single-thread-process equivalent dyld's bootstrap needs. */
        freebsd_raw_syscall(SYS__exit, a1, 0, 0, 0, 0, 0);
        __builtin_unreachable();

    case LINUX_SYS_arch_prctl:
        /* Linux arch_prctl(ARCH_SET_FS/GS, addr) sets the %fs/%gs segment
         * base for the current thread. sysarch(AMD64_SET_FSBASE/GSBASE)
         * looks like the obvious FreeBSD bridge, and it DOES work — but
         * only for the instant it's called: this dispatch runs inside a
         * signal handler (SIGSYS for arch_prctl's MOV EAX,imm32 site), and
         * FreeBSD's sigreturn(2) unconditionally restores mc_fsbase/
         * mc_gsbase from the mcontext_t handed to the handler — captured
         * at signal ENTRY, i.e. *before* any in-handler sysarch call — so
         * the base silently reverts to its pre-signal value (0, since it's
         * never been set yet) the instant the handler returns. Confirmed
         * empirically with a standalone SIGUSR1/SA_SIGINFO test on 185:
         * an in-handler sysarch(SET_GSBASE) round-trips correctly via an
         * immediate readback but is gone by the time the caller resumes.
         * The actual fix is to skip sysarch entirely and patch mc_fsbase/
         * mc_gsbase directly — sigreturn then restores THAT value, and it
         * sticks. (Also verified empirically: no sysarch call is needed at
         * all, the mcontext patch alone is sufficient.) */
        if (mc == NULL) {
            /* Can only happen if a future ud2-patched signature ever routes
             * arch_prctl through sigill_handler, which doesn't have a
             * matching mcontext-patch story yet. */
            fprintf(stderr,
                "[darling-mldr] arch_prctl: no mcontext available for op"
                " 0x%lx — ENOSYS\n", a1);
            return -ENOSYS;
        }
        if (a1 == LINUX_ARCH_SET_FS) {
            mc->mc_fsbase = a2;
            return 0;
        } else if (a1 == LINUX_ARCH_GET_FS) {
            *(long *)a2 = mc->mc_fsbase;
            return 0;
        } else if (a1 == LINUX_ARCH_SET_GS) {
            mc->mc_gsbase = a2;
            return 0;
        } else if (a1 == LINUX_ARCH_GET_GS) {
            *(long *)a2 = mc->mc_gsbase;
            return 0;
        }
        fprintf(stderr,
            "[darling-mldr] arch_prctl: unhandled op 0x%lx — ENOSYS\n", a1);
        return -ENOSYS;

    /* Deliberately NOT translated (need real semantic redesign, not a
     * number swap — see file header comment):
     *   clone   — thread/process creation semantics differ fundamentally
     *             from FreeBSD rfork(2)/thr_new(2); no 1:1 mapping.
     *   futex   — FreeBSD's equivalent is _umtx_op(2), different op encoding.
     *   brk     — FreeBSD has no program-break syscall in the modern ABI;
     *             callers need to be steered to mmap(MAP_ANON) instead. */
    default:
        fprintf(stderr,
            "[darling-mldr] unhandled Linux syscall %u — ENOSYS\n", linux_nr);
#if defined(__x86_64__)
        /* Control #64: name the guest site of each unhandled raw syscall, so
         * patch_linux_raw_syscalls can be judged against real sites. Capped so
         * a syscall loop cannot flood the log. */
        {
            static int mldr_raw_site_logged = 0;
            if (mc != NULL && mldr_raw_site_logged < 32) {
                char site[256];
                mldr_describe_addr((uintptr_t)mc->mc_rip, site, sizeof(site));
                fprintf(stderr,
                    "[darling-mldr]   at %s rip=0x%llx rax=0x%llx\n",
                    site, (unsigned long long)mc->mc_rip,
                    (unsigned long long)mc->mc_rax);
                mldr_raw_site_logged++;
            }
        }
#endif
        /* exit-caller logging slice: name the caller of the unhandled
         * raw-Linux syscall (times(99) in the (a) window is the target) */
        if (mldr_trap_log_enabled && mldr_trap_log_handlers)
            mldr_tlog("linux-unhandled", (long)linux_nr, (long)a1);
        return -ENOSYS;
    }
}
/* ── raw-Linux-syscall trampoline patching (dyld load-time, #198) ─────────── */

/*
 * Every genuine `syscall` instruction found in the upstream dyld overlay
 * binary (confirmed by scanning artefacts/darling-overlay/usr/lib/dyld's
 * x86_64 slice section-by-section — the only two hits inside an EXEC
 * section that aren't just a CALL rel32's displacement bytes) belongs to
 * one of exactly two fixed byte sequences. SIGSYS cannot reliably intercept
 * either: most low Linux syscall numbers collide with real, implemented
 * FreeBSD syscalls at the same number (e.g. Linux rt_sigprocmask=14 vs.
 * FreeBSD's COMPAT_FREEBSD11 mknod=14; Linux rt_sigreturn=15 vs. FreeBSD
 * chmod=15) — the FreeBSD kernel recognises the number and just runs its
 * own syscall with garbage arguments, and SIGSYS never fires at all.
 *
 * Both sequences are 2-byte-aligned around their `syscall` (0F 05) opcode,
 * which is the same length as `ud2` (0F 0B) — an in-place, no-relocation
 * rewrite that unconditionally raises #UD/SIGILL instead, regardless of
 * whether FreeBSD thinks the number is valid.
 */
static const uint8_t SIG_GENERIC_THUNK[] = {
    0x8b, 0x44, 0x24, 0x08,   /* mov eax, [rsp+8]  (reload syscall nr) */
    0x49, 0x89, 0xca,         /* mov r10, rcx      (syscall-ABI reg fixup) */
    0x0f, 0x05,               /* syscall */
    0xc3                      /* ret */
};
#define SIG_GENERIC_THUNK_SYSCALL_OFF 7

static const uint8_t SIG_SIGRETURN_TRAMP[] = {
    0xb8, 0x0f, 0x00, 0x00, 0x00, /* mov eax, 15  (Linux __NR_rt_sigreturn) */
    0x0f, 0x05                    /* syscall */
};
#define SIG_SIGRETURN_TRAMP_SYSCALL_OFF 5

/*
 * Control #65: the addresses at which patch_one_signature planted its own
 * `ud2`. sigill_handler uses this to tell a ud2 THIS loader planted (a
 * raw-syscall site to dispatch) from a ud2 that was already in the image — a
 * genuine `__builtin_trap()`, e.g. libsystem_platform's os_unfair_lock /
 * os_once abort routines, which must crash honestly rather than be dispatched
 * as a raw syscall with a garbage rax.
 */
#define MLDR_MAX_UD2_SITES 64
static uintptr_t _mldr_ud2_sites[MLDR_MAX_UD2_SITES];
static int _mldr_ud2_site_count = 0;


/*
 * purpose:  Find and rewrite one fixed byte signature's `syscall` opcode to
 *           `ud2` within [base, base+size).
 * input:    base/size — byte range to scan; sig/sig_len — exact bytes to
 *           match; syscall_off — offset of the 0F 05 within sig;
 *           name — used only for the "matched more than once" warning.
 * output:   number of sites patched (0, 1, or more)
 * sideEffects: writes 2 bytes per match; logs to stderr if match count > 1
 *              (a single dyld build is expected to contain each trampoline
 *              exactly once — more suggests the signature now also matches
 *              unrelated code, which needs a human look, not silent action).
 */
static size_t
patch_one_signature(uint8_t *base, size_t size,
                     const uint8_t *sig, size_t sig_len, size_t syscall_off,
                     const char *name)
{
    size_t found = 0;

    if (size < sig_len) {
        return 0;
    }

    for (size_t i = 0; i + sig_len <= size; i++) {
        if (memcmp(base + i, sig, sig_len) == 0) {
            base[i + syscall_off]     = 0x0f;
            base[i + syscall_off + 1] = 0x0b; /* ud2 */
            if (_mldr_ud2_site_count < MLDR_MAX_UD2_SITES)
                _mldr_ud2_sites[_mldr_ud2_site_count++] =
                    (uintptr_t)(base + i + syscall_off);
            found++;
        }
    }

    if (found > 1) {
        fprintf(stderr,
            "[darling-mldr] patch_linux_raw_syscalls: signature '%s' matched"
            " %zu times (expected 0 or 1) — this dyld build may need a new"
            " signature, or something unrelated is being patched\n",
            name, found);
    }

    return found;
}

void
mldr_patch_linux_raw_syscalls(void *base, size_t size)
{
    uintptr_t page_start = (uintptr_t)base & ~((uintptr_t)MLDR_PATCH_PAGE_SIZE - 1);
    uintptr_t page_end   = ((uintptr_t)base + size + MLDR_PATCH_PAGE_SIZE - 1)
                           & ~((uintptr_t)MLDR_PATCH_PAGE_SIZE - 1);
    size_t map_len = (size_t)(page_end - page_start);

    if (mprotect((void *)page_start, map_len,
                  PROT_READ | PROT_WRITE | PROT_EXEC) < 0) {
        fprintf(stderr,
            "[darling-mldr] patch_linux_raw_syscalls: mprotect(RWX) failed: %s"
            " — raw Linux syscalls in this segment stay on the broken"
            " SIGSYS-collision path\n", strerror(errno));
        return;
    }

    size_t n1 = patch_one_signature((uint8_t *)base, size,
        SIG_GENERIC_THUNK, sizeof(SIG_GENERIC_THUNK),
        SIG_GENERIC_THUNK_SYSCALL_OFF, "generic-thunk");
    size_t n2 = patch_one_signature((uint8_t *)base, size,
        SIG_SIGRETURN_TRAMP, sizeof(SIG_SIGRETURN_TRAMP),
        SIG_SIGRETURN_TRAMP_SYSCALL_OFF, "sigreturn-trampoline");

    if (n1 || n2) {
        fprintf(stderr,
            "[darling-mldr] patched raw-syscall trampolines to ud2:"
            " generic-thunk=%zu sigreturn-tramp=%zu\n", n1, n2);
    } else if (size > MLDR_PATCH_PAGE_SIZE) {
        /*
         * Both signatures are exact byte matches against ONE specific dyld
         * build (see #198's finding: a naive scan for bare `0F 05` bytes
         * false-positives constantly on unrelated code, e.g. the trailing
         * bytes of `CALL rel32`, so this only ever checks the two known
         * fixed signatures — never a heuristic byte scan).
         *
         * A different dyld build can legitimately compile these trampolines
         * with different register allocation or instruction order, in which
         * case neither signature matches and this image's raw Linux-ABI
         * syscalls silently fall back to the broken SIGSYS-collision path
         * (see this file's header comment) instead of failing loudly. Log it
         * so that failure mode is visible instead of silent — this is a
         * multi-page executable mapping (i.e. a real dylib, not some small
         * anonymous helper stub) that mldr expected to find at least one
         * trampoline in and found none.
         */
        fprintf(stderr,
            "[darling-mldr] patch_linux_raw_syscalls: no known raw-syscall"
            " trampoline signature matched in a %zu-byte executable mapping"
            " at %p — if this dylib makes raw Linux-ABI syscalls, they will"
            " silently run as FreeBSD syscalls under Linux numbers instead"
            " of failing (see freebsd_syscall_trap.c's file header)\n",
            size, base);
    }

    if (mprotect((void *)page_start, map_len, PROT_READ | PROT_EXEC) < 0) {
        fprintf(stderr,
            "[darling-mldr] patch_linux_raw_syscalls: mprotect(restore R-X)"
            " failed: %s\n", strerror(errno));
    }
}
#endif /* __x86_64__ */

/* ── SIGSYS handler ────────────────────────────────────────────────────────── */

/*
 * purpose:  SA_SIGINFO SIGSYS handler — intercepts macOS `syscall` instructions.
 *
 *           On FreeBSD:
 *             - mc_rip is already advanced past the syscall instruction.
 *             - mc_rax has been overwritten with ENOSYS by the kernel.
 *             - Argument registers (rdi, rsi, rdx, r10, r8, r9) are intact.
 *             - The original eax is recovered by scanning backward from rip
 *               for B8 (MOV EAX, imm32) with a valid macOS class byte.
 *
 * input:    signo — SIGSYS
 *           info  — siginfo_t
 *           uctx  — ucontext_t* with saved register state
 * output:   (none — modifies *uctx in place)
 * sideEffects: executes the translated syscall or Mach RPC; may write to
 *              stderr; terminates process only for truly unrecognised classes.
 */
static void
sigsys_handler(int signo, siginfo_t *info, void *uctx_void)
{
    (void)signo;
    (void)info;

    ucontext_t *uctx = (ucontext_t *)uctx_void;

    /* exit-caller logging slice: handler entry — did this thread ever get
     * here, and does the handler ever return? */
    if (mldr_trap_log_enabled && mldr_trap_log_handlers)
        mldr_tlog("SIGSYS ENTER", (long)(uintptr_t)uctx, 0);

#if defined(__x86_64__)
    mcontext_t *mc = &uctx->uc_mcontext;

    /* Recover the original macOS syscall number from instruction bytes
     * (or, for variadic wrappers, from the saved stack — see the function). */
    const uint8_t *rip = (const uint8_t *)(uintptr_t)mc->mc_rip;
    const uint8_t *rsp = (const uint8_t *)(uintptr_t)mc->mc_rsp;
    uint32_t raw_eax = recover_macos_syscall_nr(rip, rsp);

    if (raw_eax == 0) {
        /* Could not recover — dump 16 bytes before rip for diagnosis */
        fprintf(stderr,
            "[darling-mldr] SIGSYS: cannot recover syscall nr at rip=0x%llx\n"
            "  bytes[-16..+1]:",
            (unsigned long long)mc->mc_rip);
        for (int i = -16; i <= 1; i++) {
            fprintf(stderr, " %02x", (unsigned)rip[i]);
        }
        fprintf(stderr, "\n");
        if (mldr_trap_log_enabled && mldr_trap_log_handlers)
            mldr_tlog("SIGSYS RE-RAISE(recover-fail)", (long)mc->mc_rip, 0);
        struct sigaction sa_dfl = { .sa_handler = SIG_DFL };
        sigaction(SIGSYS, &sa_dfl, NULL);
        raise(SIGSYS);
        return;
    }

    /* Argument registers are always intact (rdi, rsi, rdx, r10, r8, r9) */
    long a1 = (long)mc->mc_rdi;
    long a2 = (long)mc->mc_rsi;
    long a3 = (long)mc->mc_rdx;
    long a4 = (long)mc->mc_r10;
    long a5 = (long)mc->mc_r8;
    long a6 = (long)mc->mc_r9;

    /* Read the first stack argument from the Mach-O caller's stack.
     * Under System V AMD64 ABI, [rsp+0] holds the return address pushed by
     * the `call` instruction, so the first stack-passed argument lives at
     * [rsp+8].  This is used by Mach traps that have more than 6 arguments:
     *   mach_msg_trap (-31):      arg7 = rcv_msg   at [rsp+8]
     *   mach_msg_overwrite (-32): arg7 = priority  at [rsp+8] */
    uintptr_t stack_arg1 = *((const uintptr_t *)(uintptr_t)(mc->mc_rsp + 8));

    long ret;
    bool linux_abi_retval = false;

    if ((raw_eax & MACOS_BSD_CLASS_MASK) == MACOS_BSD_CLASS) {
        /* ── macOS BSD syscall (class 0x02000000) ──────────────────── */
        unsigned int macos_nr = MACOS_BSD_NR(raw_eax);
        ret = dispatch_macos_bsd_syscall(macos_nr, a1, a2, a3, a4, a5, a6);
    } else if ((int32_t)raw_eax < 0 && (raw_eax & MACOS_BSD_CLASS_MASK) == 0xFF000000u) {
        /* ── macOS Mach trap (negative eax, upper byte = 0xFF) ────────
         *   e.g. task_self_trap = -28 = 0xFFFFFFE4, stored in uint32_t.
         *   We interpret as int32_t to pass the negative trap number. */
        int trap_nr = (int32_t)raw_eax;
        ret = dispatch_mach_trap(trap_nr, a1, a2, a3, a4, a5, a6, stack_arg1);
    } else if (raw_eax != 0 && raw_eax < 1024) {
        /* ── raw Linux-ABI syscall (#198, upstream dyld overlay binary) ──
         * Only reachable for raw-Linux syscall sites NOT covered by
         * mldr_patch_linux_raw_syscalls's two known signatures (patched
         * sites raise SIGILL instead, handled by sigill_handler below).
         * Kept as a fallback since it still works correctly for any
         * MOV EAX,imm32 site whose immediate happens not to collide with a
         * real FreeBSD syscall number. */
        ret = dispatch_linux_syscall(raw_eax, a1, a2, a3, a4, a5, a6, mc);
        linux_abi_retval = true;
    } else {
        /* Unknown syscall class — log and re-raise as default signal */
        fprintf(stderr,
            "[darling-mldr] SIGSYS: unhandled syscall class 0x%02x (raw_eax=0x%08x)"
            " at rip=0x%llx\n",
            (raw_eax >> 24) & 0xff, raw_eax,
            (unsigned long long)mc->mc_rip);
        if (mldr_trap_log_enabled && mldr_trap_log_handlers)
            mldr_tlog("SIGSYS RE-RAISE(unknown-class)", (long)raw_eax,
                      (long)mc->mc_rip);
        struct sigaction sa_dfl = { .sa_handler = SIG_DFL };
        sigaction(SIGSYS, &sa_dfl, NULL);
        raise(SIGSYS);
        return;
    }

    /* Write result back.  FreeBSD has already advanced rip past syscall;
     * we must NOT adjust rip further.
     *
     * macOS ABI (BSD syscalls + Mach traps) on error: carry flag set,
     * rax = errno (positive). On success: carry cleared, rax = retval.
     *
     * Linux raw-syscall ABI (dispatch_linux_syscall's own contract): rax =
     * retval directly, negative rax IS -errno already — no carry flag.
     * Mixing the two up here was a latent, never-exercised bug: this branch
     * used to always apply the macOS carry-flag encoding, which is wrong
     * for the Linux class. */
    if (linux_abi_retval) {
        mc->mc_rax = (uint64_t)ret;
    } else if (ret < 0) {
        mc->mc_rax = (uint64_t)(long)(-ret); /* positive errno */
        mc->mc_rflags |= 0x1ULL;             /* set carry flag */
    } else {
        mc->mc_rax = (uint64_t)ret;
        mc->mc_rflags &= ~0x1ULL;            /* clear carry flag */
    }
    /* rip is already past the syscall — no mc_rip adjustment needed */
    if (mldr_trap_log_enabled && mldr_trap_log_handlers)
        mldr_tlog("SIGSYS LEAVE", (long)mc->mc_rax, 0);

#elif defined(__aarch64__)
    /*
     * aarch64 macOS syscall ABI (XNU):
     *   x16 = 0x2000000 | bsd_nr  (BSD class), or negative Mach trap number.
     *   x0..x5 = args 1-6.
     *   svc #0x80 — the trap instruction.
     *
     * FreeBSD SIGSYS delivery on aarch64:
     *   mc_gpregs.gp_elr is already advanced past the svc instruction.
     *   mc_gpregs.gp_x[16] is intact — NOT overwritten by the kernel.
     *   mc_gpregs.gp_x[0..5] hold the argument registers (intact).
     *   Stack arg7: [sp+0] — no return address slot (LR lives in gp_lr).
     *
     * Error return (macOS ABI):
     *   CPSR carry (bit 29 of gp_spsr) set → x0 = positive errno.
     *   Carry clear → x0 = return value.
     */
    mcontext_t *mc = &uctx->uc_mcontext;

    /* x16 holds the intact macOS syscall class+number */
    uint32_t raw_x16 = (uint32_t)mc->mc_gpregs.gp_x[16];

    long a1 = (long)mc->mc_gpregs.gp_x[0];
    long a2 = (long)mc->mc_gpregs.gp_x[1];
    long a3 = (long)mc->mc_gpregs.gp_x[2];
    long a4 = (long)mc->mc_gpregs.gp_x[3];
    long a5 = (long)mc->mc_gpregs.gp_x[4];
    long a6 = (long)mc->mc_gpregs.gp_x[5];

    /* Stack arg7: [sp+0] — no return-address slot on aarch64 */
    uintptr_t stack_arg1 = *((const uintptr_t *)(uintptr_t)mc->mc_gpregs.gp_sp);

    long ret;

    if ((raw_x16 & MACOS_BSD_CLASS_MASK) == MACOS_BSD_CLASS) {
        /* ── macOS BSD syscall (class 0x02000000) ──────────────────── */
        unsigned int macos_nr = MACOS_BSD_NR(raw_x16);
        ret = dispatch_macos_bsd_syscall(macos_nr, a1, a2, a3, a4, a5, a6);
    } else if ((int32_t)raw_x16 < 0 && (raw_x16 & MACOS_BSD_CLASS_MASK) == 0xFF000000u) {
        /* ── macOS Mach trap (negative x16, upper byte = 0xFF) ────────
         *   e.g. task_self_trap = -28 = 0xFFFFFFE4 in low 32 bits of x16. */
        int trap_nr = (int32_t)raw_x16;
        ret = dispatch_mach_trap(trap_nr, a1, a2, a3, a4, a5, a6, stack_arg1);
    } else {
        /* Unknown syscall class — log and re-raise as default signal */
        fprintf(stderr,
            "[darling-mldr] SIGSYS(aarch64): unhandled class 0x%02x"
            " (x16=0x%08x) at elr=0x%llx\n",
            (raw_x16 >> 24) & 0xff, raw_x16,
            (unsigned long long)mc->mc_gpregs.gp_elr);
        struct sigaction sa_dfl = { .sa_handler = SIG_DFL };
        sigaction(SIGSYS, &sa_dfl, NULL);
        raise(SIGSYS);
        return;
    }

    /* Write result back.
     * macOS ABI on error: carry flag (bit 29 of CPSR/SPSR) set, x0 = errno.
     * On success: carry cleared, x0 = return value.
     * gp_elr already advanced past svc — no adjustment needed. */
    if (ret < 0) {
        mc->mc_gpregs.gp_x[0] = (uint64_t)(long)(-ret); /* positive errno */
        mc->mc_gpregs.gp_spsr |= (1u << 29);              /* set CPSR carry */
    } else {
        mc->mc_gpregs.gp_x[0] = (uint64_t)ret;
        mc->mc_gpregs.gp_spsr &= ~(1u << 29);             /* clear CPSR carry */
    }
#else
    (void)uctx;
    struct sigaction sa_dfl = { .sa_handler = SIG_DFL };
    sigaction(SIGSYS, &sa_dfl, NULL);
    raise(SIGSYS);
#endif
}

#if defined(__x86_64__)
/* ── SIGILL handler (patched raw-Linux-syscall trampolines, #198) ─────────── */

/* Control #65: the honest crash path, defined further down; sigill_handler
 * hands a genuine (non-patched) ud2 to it. */
static void crash_debug_handler(int signo, siginfo_t *info, void *uctx_void);

/* Control #67: mldr_dump_read_guarded, for the lock-word dump below. */
#include "crash_dump.h"

/*
 * purpose:  SA_SIGINFO SIGILL handler — catches the `ud2` we wrote into
 *           dyld's raw-Linux-syscall trampolines (see
 *           mldr_patch_linux_raw_syscalls) and dispatches them, since the
 *           corresponding SIGSYS path can't be trusted: most low Linux
 *           syscall numbers collide with real, implemented FreeBSD
 *           syscalls, so the kernel runs its own syscall instead of ever
 *           raising SIGSYS for them.
 *
 *           Unlike SIGSYS, FreeBSD delivers #UD with mc_rip pointing AT the
 *           faulting instruction (the CPU never executed it), and nothing
 *           is clobbered — this is a real illegal-instruction fault, not a
 *           syscall trap. By construction (see the two signatures in
 *           mldr_patch_linux_raw_syscalls), rax already holds the intended
 *           Linux syscall number and rdi/rsi/rdx/r10/r8/r9 already hold its
 *           arguments — exactly what the trampoline's own preceding
 *           instructions set up before reaching the (now-patched) opcode.
 *
 * input:    signo — SIGILL; info — siginfo_t; uctx — ucontext_t*
 * output:   (none — modifies *uctx in place)
 * sideEffects: executes the translated syscall; advances rip past the
 *              2-byte ud2 (which, unlike SIGSYS's syscall, was never
 *              actually executed by the CPU); re-raises as the default
 *              action for any #UD that isn't one of our own patches.
 */
static void
sigill_handler(int signo, siginfo_t *info, void *uctx_void)
{
    ucontext_t *uctx = (ucontext_t *)uctx_void;
    mcontext_t *mc = &uctx->uc_mcontext;
    const uint8_t *pc = (const uint8_t *)(uintptr_t)mc->mc_rip;

    /* exit-caller logging slice: handler entry with the faulting pc */
    if (mldr_trap_log_enabled && mldr_trap_log_handlers)
        mldr_tlogx("SIGILL ENTER", pc, (long)mc->mc_rax);

    if (pc[0] != 0x0f || pc[1] != 0x0b) {
        /* Not one of ours — a genuine illegal instruction elsewhere.
         * Restore default disposition and re-raise so the process gets the
         * normal SIGILL termination/core dump. */
        struct sigaction sa_dfl = { .sa_handler = SIG_DFL };
        if (mldr_trap_log_enabled && mldr_trap_log_handlers)
            mldr_tlogx("SIGILL RE-RAISE(not-ours)", pc, (long)mc->mc_rax);
        sigaction(signo, &sa_dfl, NULL);
        raise(signo);
        return;
    }

    /*
     * A diagnostic trap planted by MLDR_TRAP_AT is also a ud2, so it has to be
     * recognised before the raw-syscall path below tries to read a syscall
     * number out of rax. Report and die: the instruction the trap overwrote is
     * gone, so there is nothing to resume to.
     */
    for (int i = 0; i < _mldr_trap_count; i++) {
        if ((uintptr_t)mc->mc_rip == _mldr_traps[i].addr) {
            mldr_report_trap(&_mldr_traps[i], mc);

            struct sigaction sa_dfl = { .sa_handler = SIG_DFL };
            if (mldr_trap_log_enabled && mldr_trap_log_handlers)
                mldr_tlogx("SIGILL RE-RAISE(trap-site)", pc, (long)i);
            sigaction(signo, &sa_dfl, NULL);
            raise(signo);
            return;
        }
    }

    /*
     * Control #65: only a ud2 THIS loader planted (patch_linux_raw_syscalls)
     * is a raw-syscall site. A ud2 that was already in the image is a genuine
     * `__builtin_trap()` — e.g. libsystem_platform's os_unfair_lock / os_once
     * abort routines — and must crash honestly instead of being dispatched
     * with a garbage rax (the #64 finding).
     */
    {
        int is_ours = 0;
        for (int i = 0; i < _mldr_ud2_site_count; i++) {
            if ((uintptr_t)mc->mc_rip == _mldr_ud2_sites[i]) {
                is_ours = 1;
                break;
            }
        }
        if (!is_ours) {
            char site[256];
            mldr_describe_addr((uintptr_t)mc->mc_rip, site, sizeof(site));
            fprintf(stderr,
                "[darling-mldr] genuine SIGILL (ud2, not a patched trampoline)"
                " at %s rip=0x%llx rax=0x%llx\n",
                site, (unsigned long long)mc->mc_rip,
                (unsigned long long)mc->mc_rax);
            /* Control #67: dump 32 bytes of guest memory at rbx — the live
             * lock register. Step 0 showed rdi is NOT the lock at the ud2:
             * the abort routine is entered with edi=0x307 and only does
             * `movl %edi,-4(%rbp); ud2`, so rdi holds the abort's small
             * argument, while the caller's lock object is rbx (callee-saved,
             * untouched by the abort). Guarded: a bad pointer must not fault
             * inside the handler. */
            {
                uintptr_t lp = (uintptr_t)mc->mc_rbx;
                uint64_t w[4] = {0, 0, 0, 0};
                int ok[4];
                for (int i = 0; i < 4; i++)
                    ok[i] = mldr_dump_read_guarded(lp + (uintptr_t)(8 * i), &w[i]);
                fprintf(stderr,
                    "[darling-mldr] lock word @0x%llx:"
                    " %016llx %016llx %016llx %016llx (ok=%d%d%d%d rdi=0x%llx)\n",
                    (unsigned long long)lp,
                    (unsigned long long)w[0], (unsigned long long)w[1],
                    (unsigned long long)w[2], (unsigned long long)w[3],
                    ok[0], ok[1], ok[2], ok[3],
                    (unsigned long long)mc->mc_rdi);
            }
            fflush(stderr);
            /* Control #71: name the re-entry. Print the thread id and walk the
             * guest stack, resolving each word to image+offset, so the FIRST
             * acquire's return address (a frame below the second one, #65's
             * 0x8eeeeb) is visible in the log. */
            fprintf(stderr, "[darling-mldr] reentry: tid=%ld rsp=0x%llx\n",
                    (long)getpid(), (unsigned long long)mc->mc_rsp);
            {
                char ws[256];
                for (int i = 0; i < 128; i++) {
                    uint64_t w = 0;
                    uintptr_t a = (uintptr_t)mc->mc_rsp + (uintptr_t)(8 * i);
                    if (!mldr_dump_read_guarded(a, &w))
                        break;
                    if (w < 0x1000000ULL)
                        continue;
                    mldr_describe_addr((uintptr_t)w, ws, sizeof(ws));
                    if (strstr(ws, "<unknown>") == NULL)
                        fprintf(stderr, "[darling-mldr]   stack[%d] 0x%llx %s\n",
                                i, (unsigned long long)w, ws);
                }
            }
            fflush(stderr);
            crash_debug_handler(signo, info, uctx);
            return;
        }
    }

    unsigned int linux_nr = (unsigned int)mc->mc_rax;
    long a1 = (long)mc->mc_rdi;
    long a2 = (long)mc->mc_rsi;
    long a3 = (long)mc->mc_rdx;
    long a4 = (long)mc->mc_r10;
    long a5 = (long)mc->mc_r8;
    long a6 = (long)mc->mc_r9;

    long ret = dispatch_linux_syscall(linux_nr, a1, a2, a3, a4, a5, a6, mc);

    /* Linux raw-syscall ABI: rax = retval; negative rax IS -errno already
     * (dispatch_linux_syscall's own contract) — no carry-flag encoding,
     * that convention belongs to the macOS BSD-syscall class only. */
    mc->mc_rax = (uint64_t)ret;
    mc->mc_rip += 2; /* skip the ud2 the CPU never actually executed */
    if (mldr_trap_log_enabled && mldr_trap_log_handlers)
        mldr_tlog("SIGILL LEAVE", (long)mc->mc_rax, (long)linux_nr);
}
#endif /* __x86_64__ */

/* ── public API ────────────────────────────────────────────────────────────── */

/*
 * purpose:  Install macOS syscall interception via SIGSYS.  Must be called
 *           after darlingserver checkin and before start_thread().
 * input:    none
 * output:   none
 * sideEffects:
 *   - Allocates and activates an alternate signal stack.
 *   - Installs SIGSYS handler with SA_SIGINFO | SA_ONSTACK | SA_RESTART.
 */
/* ── SIGBUS/SIGSEGV debug handler ─────────────────────────────────────────── */

#include <execinfo.h>

#if defined(__x86_64__)
static void
crash_debug_handler(int signo, siginfo_t *info, void *uctx_void)
{
    ucontext_t *uctx = (ucontext_t *)uctx_void;
    mcontext_t *mc   = &uctx->uc_mcontext;

    fprintf(stderr,
        "[darling-mldr] FATAL signal %d (code=%d) at addr=%p\n"
        /* survive-window slice: name the sender — si_pid is the killer for
         * SI_QUEUE/SI_USER signals, si_value carries the queued payload. */
        "  si_pid=%d si_uid=%d si_value=0x%lx sival_ptr=%p\n"
        "  rip=0x%016llx  rax=0x%016llx  rbx=0x%016llx\n"
        "  rcx=0x%016llx  rdx=0x%016llx  rsi=0x%016llx\n"
        "  rdi=0x%016llx  rbp=0x%016llx  rsp=0x%016llx\n"
        "  r8 =0x%016llx  r9 =0x%016llx  r10=0x%016llx\n"
        "  r11=0x%016llx  r12=0x%016llx  r13=0x%016llx\n"
        "  r14=0x%016llx  r15=0x%016llx\n",
        signo, info->si_code, info->si_addr,
        (int)info->si_pid, (int)info->si_uid,
        (unsigned long)info->si_value.sival_int, info->si_value.sival_ptr,
        (unsigned long long)mc->mc_rip,
        (unsigned long long)mc->mc_rax, (unsigned long long)mc->mc_rbx,
        (unsigned long long)mc->mc_rcx, (unsigned long long)mc->mc_rdx,
        (unsigned long long)mc->mc_rsi, (unsigned long long)mc->mc_rdi,
        (unsigned long long)mc->mc_rbp, (unsigned long long)mc->mc_rsp,
        (unsigned long long)mc->mc_r8,  (unsigned long long)mc->mc_r9,
        (unsigned long long)mc->mc_r10, (unsigned long long)mc->mc_r11,
        (unsigned long long)mc->mc_r12, (unsigned long long)mc->mc_r13,
        (unsigned long long)mc->mc_r14, (unsigned long long)mc->mc_r15);
    fflush(stderr);

    /* DIAG (Chrome dyld-init crash hunt): backtrace_symbols on the live stack
     * identifies WHICH library the bad-jump frame lives in. */
    {
        void *frames[32];
        int n = backtrace(frames, 32);
        char **names = backtrace_symbols(frames, n);
        fprintf(stderr, "  backtrace (%d frames):\n", n);
        for (int i = 0; i < n; i++) {
            fprintf(stderr, "    #%02d %p  %s\n", i, frames[i],
                    (names && names[i]) ? names[i] : "?");
        }
        fflush(stderr);
        if (names) free(names);
    }

    /* DEBUG: dump the GUEST stack at rsp (backtrace(3) on FreeBSD only walks
     * the host signal frame, not the guest code).
     *
     * The walk is delegated to crash_dump.c because it must not be able to
     * fault: this handler runs BEFORE the re-raise below, so a fault inside
     * the dump replaces the real crash — and its signal — with a crash inside
     * the diagnostic. That is exactly what the unguarded loop this replaced
     * did; see crash_dump.c's header for the log that shows it. The dead
     * `__mldr_stack_map_base` clamp that used to sit here went with it: that
     * symbol was weak, undefined in every file in the tree, and therefore
     * always 0, so the clamp it guarded never ran and the walk started at the
     * raw rsp of a context already known to be wrecked. Defining it was not
     * an option — nothing in mldr knows what the "guest stack mapping base"
     * would be, so any value would have been invented — and the guard answers
     * the same question honestly, per word. */
    mldr_dump_guarded_stack("guest stack dump at rsp",
                            (uintptr_t)mc->mc_rsp, MLDR_STACK_DUMP_WORDS);

    /* Re-raise as default action so the process terminates with correct signal */
    struct sigaction sa_dfl = { .sa_handler = SIG_DFL };
    sigaction(signo, &sa_dfl, NULL);
    raise(signo);
}
#endif /* __x86_64__ */

/*
 * purpose:  Give the calling thread its own alternate signal stack, so the
 *           SA_ONSTACK handlers installed by setup_macos_syscall_trap() have
 *           somewhere valid to run on threads other than the initial one.
 *
 *           Without this, a thread created inside the guest (libdispatch
 *           worker, pthread) has no alternate stack at all, so SA_ONSTACK
 *           silently degrades to "use the interrupted thread's own stack".
 *           Guest thread stacks are sized by the guest for guest code, not for
 *           an extra host signal frame plus a syscall-translation handler, and
 *           delivering the signal then faults inside the kernel's frame setup —
 *           surfacing as a SIGSEGV in libthr's handle_signal, with the guest
 *           call that trapped nowhere in the backtrace. Observed live on 185:
 *           `defaults` reached Foundation's initializer, libdispatch spun up
 *           its first worker thread, that thread hit a ud2 raw-syscall
 *           trampoline, and the process died in handle_signal — which also
 *           explains why the failure moved around under truss and why purely
 *           single-threaded tests (hello-foundation) never saw it.
 *
 *           mmap rather than a __thread buffer on purpose: mldr rewrites
 *           fsbase for guest threads (see arch_prctl/LINUX_ARCH_SET_FS), so
 *           host thread-local storage is not dependably addressable from a
 *           guest thread — the very context this has to work in.
 * input:    none.
 * output:   0 on success, -1 on failure (already reported to stderr).
 * sideEffects: maps SIGSYS_ALTSTACK_SIZE bytes that live as long as the thread
 *           does, and registers them as the thread's alternate signal stack.
 */
int
mldr_setup_thread_signal_stack(void)
{
    void *stack = mmap(NULL, SIGSYS_ALTSTACK_SIZE, PROT_READ | PROT_WRITE,
                       MAP_PRIVATE | MAP_ANON, -1, 0);

    if (stack == MAP_FAILED) {
        fprintf(stderr,
            "[darling-mldr] WARNING: could not map an alternate signal stack"
            " for this thread: %s — a raw-syscall trap on it will crash in"
            " signal delivery\n", strerror(errno));
        if (mldr_trap_log_enabled)
            mldr_tlog("altstack FAIL(mmap)", 0, errno);
        return -1;
    }

    stack_t altss = {
        .ss_sp    = stack,
        .ss_size  = SIGSYS_ALTSTACK_SIZE,
        .ss_flags = 0,
    };

    if (sigaltstack(&altss, NULL) < 0) {
        fprintf(stderr,
            "[darling-mldr] WARNING: sigaltstack() failed for this thread:"
            " %s — a raw-syscall trap on it will crash in signal delivery\n",
            strerror(errno));
        if (mldr_trap_log_enabled)
            mldr_tlog("altstack FAIL(sigaltstack)", 0, errno);
        munmap(stack, SIGSYS_ALTSTACK_SIZE);
        return -1;
    }

    if (mldr_trap_log_enabled)
        mldr_tlog("altstack OK", (long)(uintptr_t)stack, 0);
    return 0;
}

void
setup_macos_syscall_trap(void)
{
    /* exit-caller logging slice: capture the gate before any thread exists */
    mldr_trap_log_enabled = (getenv("DARLING_TRAP_LOG") != NULL);
    /* survive-window slice: handler-side sub-gate; DARLING_TRAP_LOG_HANDLERS=0
     * silences only the in-handler write(2) markers. */
    {
        const char *h = getenv("DARLING_TRAP_LOG_HANDLERS");
        mldr_trap_log_handlers = (h == NULL || h[0] != '0');
    }

    /* Alternate stack: prevents the handler from clobbering the Mach-O stack */
    stack_t altss = {
        .ss_sp    = _sigsys_altstack,
        .ss_size  = SIGSYS_ALTSTACK_SIZE,
        .ss_flags = 0,
    };
    if (sigaltstack(&altss, NULL) < 0) {
        fprintf(stderr,
            "[darling-mldr] WARNING: sigaltstack() failed: %s\n",
            strerror(errno));
    }

    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_sigaction = sigsys_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_SIGINFO | SA_ONSTACK | SA_RESTART;

    if (sigaction(SIGSYS, &sa, NULL) < 0) {
        fprintf(stderr,
            "[darling-mldr] FATAL: sigaction(SIGSYS) failed: %s\n",
            strerror(errno));
    } else {
        fprintf(stderr,
            "[darling-mldr] macOS BSD syscall trap installed (SIGSYS/"
#if defined(__x86_64__)
            "x86-64"
#elif defined(__aarch64__)
            "aarch64"
#else
            "unknown-arch"
#endif
            ")\n");
    }

#if defined(__x86_64__)
    /* Install SIGILL handler for #198's patched raw-Linux-syscall
     * trampolines (see mldr_patch_linux_raw_syscalls / sigill_handler) */
    struct sigaction sa_ill;
    memset(&sa_ill, 0, sizeof(sa_ill));
    sa_ill.sa_sigaction = sigill_handler;
    sigemptyset(&sa_ill.sa_mask);
    sa_ill.sa_flags = SA_SIGINFO | SA_ONSTACK | SA_RESTART;
    if (sigaction(SIGILL, &sa_ill, NULL) < 0) {
        fprintf(stderr,
            "[darling-mldr] FATAL: sigaction(SIGILL) failed: %s\n",
            strerror(errno));
    }

    /* Install SIGBUS/SIGSEGV debug handler to diagnose dyld startup crashes */
    struct sigaction sa_crash;
    memset(&sa_crash, 0, sizeof(sa_crash));
    sa_crash.sa_sigaction = crash_debug_handler;
    sigemptyset(&sa_crash.sa_mask);
    sa_crash.sa_flags = SA_SIGINFO | SA_ONSTACK;
    sigaction(SIGBUS,  &sa_crash, NULL);
    sigaction(SIGSEGV, &sa_crash, NULL);
#endif
}

#endif /* DARLING_FREEBSD */
