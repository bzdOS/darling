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
 *   - All macOS BSD syscalls not in the dispatch table return ENOSYS in rax.
 *
 * Architecture: x86_64 only in this revision.  aarch64 requires reading
 *               the syscall number from x16/x8 in the saved mcontext.
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
 *   - mach_msg 9-argument overwrite form: the SIGSYS context only captures 6
 *     argument registers; extra stack args (priority, rcv_msg) default to
 *     0/msg respectively.  Full support requires reading the Mach-O stack.
 */

#ifdef DARLING_FREEBSD

#include "freebsd_syscall_trap.h"

#include <sys/types.h>
#include <signal.h>
#include <ucontext.h>
#include <errno.h>
#include <unistd.h>
#include <sys/syscall.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <darlingserver/rpc.h>

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

/* ── alternate signal stack ────────────────────────────────────────────────── */

#define SIGSYS_ALTSTACK_SIZE (64 * 1024)
static char _sigsys_altstack[SIGSYS_ALTSTACK_SIZE] __attribute__((aligned(16)));

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
    /* aarch64 stub — implement when aarch64 VM is available */
    (void)nr; (void)a1; (void)a2; (void)a3;
    (void)a4; (void)a5; (void)a6;
    ret = -ENOSYS;
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
 *           To recover the original number we scan back for:
 *             rip[-7] = 0xb8          (MOV EAX, imm32)
 *             rip[-6..rip-3] = imm32  (the macOS syscall class+number)
 *             rip[-2..rip-1] = 0f 05  (SYSCALL opcode)
 *
 * input:    rip_ptr — mc_rip as a byte pointer (points past SYSCALL)
 * output:   recovered eax value, or 0 if the pattern is not found
 * sideEffects: none (read-only access to faulting instruction bytes)
 */
static uint32_t
recover_macos_syscall_nr(const uint8_t *rip_ptr)
{
    /* Confirm bytes[-2..-1] are the SYSCALL opcode (0F 05) */
    if (rip_ptr[-2] != 0x0f || rip_ptr[-1] != 0x05) {
        return 0;
    }
    /* Check for MOV EAX, imm32 (opcode 0xB8) 5 bytes earlier */
    if (rip_ptr[-7] == 0xb8) {
        uint32_t nr;
        memcpy(&nr, rip_ptr - 6, 4);
        return nr;
    }
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
 * input:    trap_nr — raw int32_t Mach trap number (negative, e.g. -28)
 *           a1..a6  — argument registers from the saved mcontext (intact)
 * output:   port name (uint32_t) on success, or -errno on error
 * sideEffects: issues a synchronous RPC to darlingserver; may log to stderr.
 */
static long
dispatch_mach_trap(int trap_nr,
                   long a1, long a2, long a3,
                   long a4, long a5, long a6)
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
    /* macOS mach_msg_trap args: msg, option, send_size, rcv_size, rcv_name,
     * timeout, priority.  Mapped 1:1 to darlingserver mach_msg_overwrite
     * with rcv_msg == msg (in-place receive). */
    case MACH_TRAP_mach_msg_trap:
        ret = dserver_rpc_mach_msg_overwrite(
            (void*)a1,      /* msg */
            (int32_t)a2,    /* option */
            (uint32_t)a3,   /* send_size */
            (uint32_t)a4,   /* rcv_size */
            (uint32_t)a5,   /* rcv_name */
            (uint32_t)a6,   /* timeout */
            0,              /* priority (not in 6-arg form) */
            (void*)a1       /* rcv_msg == msg (overwrite in place) */
        );
        if (ret < 0) {
            fprintf(stderr,
                "[darling-mldr] mach_trap: mach_msg_trap RPC failed: %d\n", ret);
            return (long)ret;
        }
        return 0; /* MACH_MSG_SUCCESS */

    case MACH_TRAP_mach_msg_overwrite:
        /* 9-argument form: a1=msg, a2=option, a3=send_sz, a4=rcv_sz,
         * a5=rcv_name, a6=timeout; priority and rcv_msg come from stack —
         * the SIGSYS handler only has 6 registers; treat rcv_msg == msg. */
        ret = dserver_rpc_mach_msg_overwrite(
            (void*)a1, (int32_t)a2, (uint32_t)a3,
            (uint32_t)a4, (uint32_t)a5, (uint32_t)a6,
            0, (void*)a1
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
}

/* ── SIGSYS handler ────────────────────────────────────────────────────────── */

/*
 * purpose:  SA_SIGINFO SIGSYS handler — intercepts macOS `syscall` instructions.
 *
 *           On FreeBSD:
 *             - mc_rip is already advanced past the syscall instruction.
 *             - mc_rax has been overwritten with ENOSYS by the kernel.
 *             - Argument registers (rdi, rsi, rdx, r10, r8, r9) are intact.
 *             - The original eax is recovered from instruction bytes at
 *               rip[-7..rip-3] (MOV EAX, imm32 opcode + immediate).
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

#if defined(__x86_64__)
    mcontext_t *mc = &uctx->uc_mcontext;

    /* Recover the original macOS syscall number from instruction bytes */
    const uint8_t *rip = (const uint8_t *)(uintptr_t)mc->mc_rip;
    uint32_t raw_eax = recover_macos_syscall_nr(rip);

    if (raw_eax == 0) {
        /* Could not recover — fallback to default handler */
        fprintf(stderr,
            "[darling-mldr] SIGSYS: cannot recover syscall nr at rip=0x%llx "
            "(bytes[-2]=0x%02x [-1]=0x%02x [-7]=0x%02x)\n",
            (unsigned long long)mc->mc_rip,
            rip[-2], rip[-1], rip[-7]);
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

    long ret;

    if ((raw_eax & MACOS_BSD_CLASS_MASK) == MACOS_BSD_CLASS) {
        /* ── macOS BSD syscall (class 0x02000000) ──────────────────── */
        unsigned int macos_nr = MACOS_BSD_NR(raw_eax);
        ret = dispatch_macos_bsd_syscall(macos_nr, a1, a2, a3, a4, a5, a6);
    } else if ((int32_t)raw_eax < 0 && (raw_eax & MACOS_BSD_CLASS_MASK) == 0xFF000000u) {
        /* ── macOS Mach trap (negative eax, upper byte = 0xFF) ────────
         *   e.g. task_self_trap = -28 = 0xFFFFFFE4, stored in uint32_t.
         *   We interpret as int32_t to pass the negative trap number. */
        int trap_nr = (int32_t)raw_eax;
        ret = dispatch_mach_trap(trap_nr, a1, a2, a3, a4, a5, a6);
    } else {
        /* Unknown syscall class — log and re-raise as default signal */
        fprintf(stderr,
            "[darling-mldr] SIGSYS: unhandled syscall class 0x%02x (raw_eax=0x%08x)"
            " at rip=0x%llx\n",
            (raw_eax >> 24) & 0xff, raw_eax,
            (unsigned long long)mc->mc_rip);
        struct sigaction sa_dfl = { .sa_handler = SIG_DFL };
        sigaction(SIGSYS, &sa_dfl, NULL);
        raise(SIGSYS);
        return;
    }

    /* Write result back.  FreeBSD has already advanced rip past syscall;
     * we must NOT adjust rip further.
     *
     * macOS ABI on error: carry flag set, rax = errno (positive).
     * On success: carry cleared, rax = return value. */
    if (ret < 0) {
        mc->mc_rax = (uint64_t)(long)(-ret); /* positive errno */
        mc->mc_rflags |= 0x1ULL;             /* set carry flag */
    } else {
        mc->mc_rax = (uint64_t)ret;
        mc->mc_rflags &= ~0x1ULL;            /* clear carry flag */
    }
    /* rip is already past the syscall — no mc_rip adjustment needed */

#elif defined(__aarch64__)
    /* aarch64: stub — TODO */
    (void)uctx;
    struct sigaction sa_dfl = { .sa_handler = SIG_DFL };
    sigaction(SIGSYS, &sa_dfl, NULL);
    raise(SIGSYS);
#else
    (void)uctx;
    struct sigaction sa_dfl = { .sa_handler = SIG_DFL };
    sigaction(SIGSYS, &sa_dfl, NULL);
    raise(SIGSYS);
#endif
}

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
void
setup_macos_syscall_trap(void)
{
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
            "[darling-mldr] macOS BSD syscall trap installed (SIGSYS/x86-64)\n");
    }
}

#endif /* DARLING_FREEBSD */
