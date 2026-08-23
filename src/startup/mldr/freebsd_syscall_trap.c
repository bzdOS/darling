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
#include <string.h>
#include <fcntl.h> /* AT_FDCWD / AT_SYMLINK_NOFOLLOW — #198 stat/lstat via fstatat */
#include <sys/stat.h> /* native struct stat — #198 stat/fstat/lstat translation */
#include <time.h> /* CLOCK_MONOTONIC — #198 clock_gettime translation */
#include <sys/mman.h> /* mprotect — #198 raw-syscall trampoline patching */
#include <dirent.h> /* native struct dirent — #198 getdents64 translation */
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
#define LINUX_SYS_sendmsg       46
#define LINUX_SYS_recvmsg       47
#define LINUX_SYS_exit          60
#define LINUX_SYS_wait4         61
#define LINUX_SYS_kill          62
#define LINUX_SYS_sigaltstack  131
#define LINUX_SYS_arch_prctl   158
#define LINUX_SYS_gettid       186
#define LINUX_SYS_exit_group   231
#define LINUX_SYS_getdents64   217
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
        return freebsd_raw_syscall(SYS_read, a1, a2, a3, 0, 0, 0);
    case LINUX_SYS_write:
        return freebsd_raw_syscall(SYS_write, a1, a2, a3, 0, 0, 0);
    case LINUX_SYS_open:
        return freebsd_raw_syscall(SYS_open, a1, a2, a3, 0, 0, 0);
    case LINUX_SYS_close:
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
    case LINUX_SYS_openat: {
        /* openat(dirfd, path, flags, mode) — dirfd's AT_FDCWD (-100)
         * coincides between the two OSes and access-mode bits (RDONLY/
         * WRONLY/RDWR) are identical; the rest of the flag bits are NOT
         * (see LINUX_O_* above) and must be translated explicitly. */
        long freebsd_oflags = a3 & 0x3; /* O_RDONLY/O_WRONLY/O_RDWR */
        if (a3 & LINUX_O_CREAT)     freebsd_oflags |= O_CREAT;
        if (a3 & LINUX_O_EXCL)      freebsd_oflags |= O_EXCL;
        if (a3 & LINUX_O_TRUNC)     freebsd_oflags |= O_TRUNC;
        if (a3 & LINUX_O_APPEND)    freebsd_oflags |= O_APPEND;
        if (a3 & LINUX_O_NONBLOCK)  freebsd_oflags |= O_NONBLOCK;
        if (a3 & LINUX_O_DIRECTORY) freebsd_oflags |= O_DIRECTORY;
        if (a3 & LINUX_O_CLOEXEC)   freebsd_oflags |= O_CLOEXEC;
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
        if (mr >= 0 && (a3 & PROT_EXEC) && (int)a5 >= 0)
            mldr_patch_linux_raw_syscalls((void *)mr, (size_t)a2);
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
        /* Signal *numbers* differ between Linux and macOS/FreeBSD; passing
         * through is only correct for numbers that happen to coincide
         * (mirrors the same caveat already noted on MACOS_SYS_sigprocmask). */
        return freebsd_raw_syscall(SYS_sigaction, a1, a2, a3, 0, 0, 0);
    case LINUX_SYS_rt_sigprocmask:
        return freebsd_raw_syscall(SYS_sigprocmask, a1, a2, a3, 0, 0, 0);

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

#define MLDR_PATCH_PAGE_SIZE 4096u

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
    (void)info;

    ucontext_t *uctx = (ucontext_t *)uctx_void;
    mcontext_t *mc = &uctx->uc_mcontext;
    const uint8_t *pc = (const uint8_t *)(uintptr_t)mc->mc_rip;

    if (pc[0] != 0x0f || pc[1] != 0x0b) {
        /* Not one of ours — a genuine illegal instruction elsewhere.
         * Restore default disposition and re-raise so the process gets the
         * normal SIGILL termination/core dump. */
        struct sigaction sa_dfl = { .sa_handler = SIG_DFL };
        sigaction(signo, &sa_dfl, NULL);
        raise(signo);
        return;
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

#if defined(__x86_64__)
static void
crash_debug_handler(int signo, siginfo_t *info, void *uctx_void)
{
    ucontext_t *uctx = (ucontext_t *)uctx_void;
    mcontext_t *mc   = &uctx->uc_mcontext;

    fprintf(stderr,
        "[darling-mldr] FATAL signal %d (code=%d) at addr=%p\n"
        "  rip=0x%016llx  rax=0x%016llx  rbx=0x%016llx\n"
        "  rcx=0x%016llx  rdx=0x%016llx  rsi=0x%016llx\n"
        "  rdi=0x%016llx  rbp=0x%016llx  rsp=0x%016llx\n"
        "  r8 =0x%016llx  r9 =0x%016llx  r10=0x%016llx\n"
        "  r11=0x%016llx  r12=0x%016llx  r13=0x%016llx\n"
        "  r14=0x%016llx  r15=0x%016llx\n",
        signo, info->si_code, info->si_addr,
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

    /* DEBUG (dyld M0 investigation, temporary): dump the stack at rsp — if the
     * crashing function was entered via a plain `call` with no prologue yet
     * (rbp==rsp, as observed for this crash), *(uint64_t*)rsp is the caller's
     * return address, identifying WHO calls into the crashing function. */
    {
        fprintf(stderr, "  stack dump at rsp=0x%016llx:\n", (unsigned long long)mc->mc_rsp);
        volatile unsigned long long *sp = (unsigned long long *)(uintptr_t)mc->mc_rsp;
        for (int i = 0; i < 16; i++) {
            fprintf(stderr, "  [rsp+%3d] 0x%016llx\n", i * 8, sp[i]);
        }
        fflush(stderr);
    }

    /* Re-raise as default action so the process terminates with correct signal */
    struct sigaction sa_dfl = { .sa_handler = SIG_DFL };
    sigaction(signo, &sa_dfl, NULL);
    raise(signo);
}
#endif /* __x86_64__ */

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
