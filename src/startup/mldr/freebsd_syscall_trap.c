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
#include <signal.h>
#include <ucontext.h>
#include <errno.h>
#include <unistd.h>
#include <sys/syscall.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <fcntl.h> /* AT_FDCWD / AT_SYMLINK_NOFOLLOW — #198 stat/lstat via fstatat */
#include <darlingserver/rpc.h>
#if defined(__x86_64__)
#include <machine/sysarch.h> /* AMD64_{SET,GET}_{FS,GS}BASE — #198 arch_prctl bridge */
#endif

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
#define LINUX_SYS_lseek          8
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
#define LINUX_SYS_exit          60
#define LINUX_SYS_wait4         61
#define LINUX_SYS_kill          62
#define LINUX_SYS_arch_prctl   158
#define LINUX_SYS_gettid       186
#define LINUX_SYS_exit_group   231

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
/* ── Linux-ABI syscall dispatcher (x86-64 only, #198) ─────────────────────── */

/*
 * purpose:  Translate a raw Linux x86-64 syscall (as made directly by the
 *           upstream dyld/libdyld.dylib overlay binary, which was compiled
 *           to run on Linux) to a FreeBSD equivalent and execute it.
 * input:    linux_nr — Linux syscall number; a1..a6 — arguments (already in
 *           the right registers: Linux and FreeBSD raw syscall ABI share the
 *           same argument-register convention on x86-64).
 * output:   FreeBSD kernel retval (negative = -errno) or 0/positive on success
 * sideEffects: performs the requested kernel operation; arch_prctl additionally
 *              stores a local variable whose address is passed to sysarch().
 */
static long
dispatch_linux_syscall(unsigned int linux_nr,
                       long a1, long a2, long a3,
                       long a4, long a5, long a6)
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
    case LINUX_SYS_stat:
        /* FreeBSD 15 dropped SYS_stat entirely (11-compat only, old ABI
         * struct); fstatat(AT_FDCWD, path, buf, 0) is the modern ino64
         * equivalent (mirrors the MACOS_SYS_fstat comment above). */
        return freebsd_raw_syscall(SYS_fstatat, AT_FDCWD, a1, a2, 0, 0, 0);
    case LINUX_SYS_fstat:
        return freebsd_raw_syscall(SYS_fstat, a1, a2, 0, 0, 0, 0);
    case LINUX_SYS_lstat:
        /* Same as LINUX_SYS_stat but AT_SYMLINK_NOFOLLOW (don't follow the
         * final symlink component) — that's the only difference between
         * stat(2) and lstat(2). */
        return freebsd_raw_syscall(SYS_fstatat, AT_FDCWD, a1, a2,
                                   AT_SYMLINK_NOFOLLOW, 0, 0);
    case LINUX_SYS_lseek:
        return freebsd_raw_syscall(SYS_lseek, a1, a2, a3, 0, 0, 0);
    case LINUX_SYS_mmap:
        /* Linux mmap(addr,len,prot,flags,fd,off) — same arg layout as FreeBSD,
         * but MAP_* flag *values* differ between the two OSes. dyld's own
         * bootstrap mmaps are anonymous+private (flag bits that happen to
         * coincide), so pass through as-is for now; a real flag-value
         * translation table is a follow-up if a differing-flag mmap is ever
         * observed to misbehave. */
        return freebsd_raw_syscall(SYS_mmap, a1, a2, a3, a4, a5, a6);
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
        /* Linux arch_prctl(ARCH_SET_FS, addr) takes addr *directly* as a2.
         * FreeBSD's sysarch(2) takes a *pointer to* the value instead:
         *   int sysarch(int op, void *parms);  // parms -> the addr itself
         * dyld needs a working %fs-relative TLS base before it can do much
         * else, so this one is worth getting right rather than ENOSYS. */
        if (a1 == LINUX_ARCH_SET_FS) {
            long fsbase = a2;
            return freebsd_raw_syscall(SYS_sysarch, AMD64_SET_FSBASE,
                                       (long)&fsbase, 0, 0, 0, 0);
        } else if (a1 == LINUX_ARCH_GET_FS) {
            long fsbase = 0;
            long r = freebsd_raw_syscall(SYS_sysarch, AMD64_GET_FSBASE,
                                         (long)&fsbase, 0, 0, 0, 0);
            if (r < 0) return r;
            *(long *)a2 = fsbase;
            return 0;
        } else if (a1 == LINUX_ARCH_SET_GS) {
            long gsbase = a2;
            return freebsd_raw_syscall(SYS_sysarch, AMD64_SET_GSBASE,
                                       (long)&gsbase, 0, 0, 0, 0);
        } else if (a1 == LINUX_ARCH_GET_GS) {
            long gsbase = 0;
            long r = freebsd_raw_syscall(SYS_sysarch, AMD64_GET_GSBASE,
                                         (long)&gsbase, 0, 0, 0, 0);
            if (r < 0) return r;
            *(long *)a2 = gsbase;
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
        /* ── raw Linux-ABI syscall (#198, upstream dyld overlay binary) ── */
        ret = dispatch_linux_syscall(raw_eax, a1, a2, a3, a4, a5, a6);
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
