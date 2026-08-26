/*
 * process_spawn.c — implementation of macOS fork/vfork/execve/posix_spawn
 * for mldr's FreeBSD SIGSYS syscall trap.
 *
 * See process_spawn.h for the public contract of each function and
 * docs/SPEC-mldr-process-creation.md for the darlingserver protocol these
 * are wired to, where to call them from, and unverified risk areas.
 *
 * ── why raw fork()/execve() alone are wrong (short version — see SPEC) ──
 *
 * darlingserver identifies a process by the real PID it reads off the
 * RPC socket's credentials, and lazily creates a Process record for any
 * PID it hasn't seen on the FIRST call that PID makes (see
 * darlingserver's call.cpp, processRegistry().registerIfAbsent() keyed on
 * header->pid). That means a raw fork() child WOULD eventually get a
 * Process record the moment it made some other RPC call — but:
 *
 *   1. Until then, and for the socket used to make that first call, it's
 *      using the exact same UNIX-domain socket FD the parent still holds
 *      (fork() duplicates fd table entries, not the underlying socket).
 *      Two processes reading/writing one socket race, and darlingserver
 *      cannot tell which process a given inbound message is really from
 *      via the socket alone — see threads.c's own comment on the
 *      equivalent per-THREAD bug this exact codebase already hit and
 *      fixed (__darling_thread_rpc_socket()).
 *   2. darlingserver's Process constructor (process.cpp) does real setup
 *      from the checkin call specifically — e.g. Process::notifyCheckin()
 *      (process.cpp:410) is what wakes up the parent's fork-wait semaphore
 *      and fires the NOTE_FORK kqueue event
 *      (process.cpp:495-504) so a parent blocked in a macOS
 *      wait-for-fork primitive (or watching via kqueue) ever unblocks.
 *      A process that never calls checkin never fires that.
 *   3. mldr's OWN process-lifetime-pipe bookkeeping
 *      (__dserver_process_lifetime_pipe_fd) and per-process RPC socket
 *      (__dserver_main_thread_socket_fd) are process-global C variables;
 *      after fork() the child's copies point at the SAME kernel objects as
 *      the parent's until something closes and replaces them. Nothing
 *      about calling libc fork() does that on its own.
 *
 * The `is_fork` field carried in the checkin RPC body
 * (generated-rpc/include/darlingserver/rpc.h:265) turns out NOT to be the
 * mechanism that makes any of this work, despite the name suggesting it's
 * load-bearing: darlingserver's own Checkin::processCall()
 * (call.cpp:311-329) never reads `_body.is_fork` at all. The fork-vs-exec
 * distinction darlingserver actually acts on is server-side state
 * (Process::_pendingReplacement, set by setPendingReplacement() from
 * Checkout::processCall() at call.cpp:377, *before* the real execve(2)
 * runs) — see Process::notifyCheckin() (process.cpp:410-505), which
 * branches on `_pendingReplacement`, not on anything the checkin call
 * itself carries. So `is_fork` is passed through here (matching the
 * existing call sites this code mirrors) for protocol-shape compatibility
 * and because a future darlingserver revision may start reading it, but
 * correctness here does NOT depend on its value being right.
 *
 * ── async-signal-safety / guest-fsbase note ──────────────────────────────
 *
 * dispatch_macos_bsd_syscall() (freebsd_syscall_trap.c) — which is meant
 * to call into this file — runs INSIDE the SIGSYS handler
 * (sigsys_handler(), same file). That handler does not save or restore
 * %fs/fsbase, and mldr rewrites the guest's fsbase via the emulated
 * arch_prctl(ARCH_SET_FS) (see freebsd_syscall_trap.c:2385-2411's own
 * comment on this). So by the time a guest binary is far enough into its
 * own bootstrap to call fork()/posix_spawn() — which for any real macOS
 * process happens well after dyld has set up its own TLS — %fs during
 * this handler's execution is very likely the GUEST's TLS base, not
 * mldr's own.
 *
 * Consequence: code in this file MUST NOT reference any `__thread`
 * qualified C variable, directly or indirectly. This is why every RPC
 * call here uses the `dserver_rpc_explicit_*()` entry points (which take
 * the socket fd as an explicit argument) instead of the plain
 * `dserver_rpc_*()` ones — the latter resolve the socket via
 * `dserver_rpc_hooks_get_socket()` → `__darling_thread_rpc_socket()`
 * (resources/dserver-rpc-defs.h:127, elfcalls/threads.c:435), which reads
 * the `static __thread int t_server_socket` in threads.c:54. That would
 * be exactly the same class of bug threads.c's own comment describes
 * (elfcalls/threads.c:446-470), just triggered via a swapped fsbase
 * instead of a second thread.
 *
 * This file always addresses the process's single "main thread" RPC
 * socket (`__dserver_main_thread_socket_fd`, an ordinary — non-TLS —
 * global) directly. That is exactly right for mldr_sys_fork()/vfork():
 * only the calling thread survives fork(2) regardless of which guest
 * thread called it, so "the new process's main thread" and "the thread
 * that called fork()" are the same thread by construction. It is only an
 * ASSUMPTION for mldr_sys_execve()/mldr_sys_posix_spawn() when called
 * from a guest thread that is NOT the process's actual main thread — see
 * the SPEC doc's risk list; this was not exercised or verified in this
 * session.
 *
 * A second, deeper concern in the same family — NOT resolved here — is
 * that `__mldr_create_rpc_socket()` (mldr.c) calls `socket_bitmap_get()`,
 * which takes a `pthread_mutex_t` and may `realloc()`. FreeBSD's libthr
 * itself resolves the calling thread (for its mutex fast path, and for
 * anything touching `errno`) via a TCB reached through %fs on amd64 — if
 * that is accurate for the libthr actually linked into mldr on 185 (NOT
 * verified in this sandboxed session: no FreeBSD libthr source is vendored
 * in this repository to check against), then this call is *also* suspect
 * once guest fsbase is live, independent of the __thread issue above. It
 * is used anyway, because: it is the only existing, tested code path that
 * produces a correctly bound-and-registered FreeBSD RPC socket, and the
 * signal this trap responds to is synchronous (raised by the very `fork`/
 * `execve`/`posix_spawn` syscall instruction the calling thread itself
 * executed — see freebsd_syscall_trap.c's own "Background" comment on how
 * SIGSYS is raised on FreeBSD — not delivered asynchronously mid some
 * unrelated critical section), which rules out the most common way this
 * class of bug bites (the same thread re-entering a lock it already
 * holds). See the SPEC doc for the recommended real fix (save/restore
 * mldr's own fsbase around the whole SIGSYS dispatch, which would also
 * benefit the pre-existing Mach-trap RPC calls in freebsd_syscall_trap.c —
 * out of scope here).
 */

#ifdef DARLING_FREEBSD

#include "process_spawn.h"
#include "loader.h"

#include <darlingserver/rpc.h>

#include <mach-o/loader.h>
#include <mach-o/fat.h>

#include <sys/syscall.h>
#include <sys/types.h>
#include <fcntl.h>
#include <errno.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>

/* ── globals/functions defined in mldr.c and elfcalls/threads.c ─────────
 * All of these are ordinary (non-`__thread`) C linkage — safe to touch
 * from a signal handler with arbitrary live fsbase, per the file header
 * note above. Declared here rather than in a shared header because
 * editing mldr.c/loader.h/threads.h is out of scope for this change; the
 * symbols already exist with exactly these signatures (see
 * elfcalls/threads.c:45-46 for the __mldr_create_rpc_socket/
 * __mldr_close_rpc_socket prototypes this mirrors). */
extern int __mldr_create_rpc_socket(void);
extern void __mldr_close_rpc_socket(int socket);
extern int __mldr_create_process_lifetime_pipe(int *fds);
extern void __mldr_close_process_lifetime_pipe(int fd);
extern int __dserver_main_thread_socket_fd;
extern int __dserver_process_lifetime_pipe_fd;
extern struct load_results mldr_load_results;

/* Bound on the rewritten argv mldr_sys_execve() builds when re-exec'ing
 * itself against a Mach-O target (adds one slot for "mldr!path" in front
 * of the guest's own argv). A fixed bound instead of alloca/VLA, per this
 * project's style rules (no hidden/attacker-sized stack allocation in a
 * signal-handler hot path). Guest argv longer than this is rejected with
 * -E2BIG rather than silently truncated. */
#define PSP_MAX_ARGV 256

/* ── raw FreeBSD syscall helper ──────────────────────────────────────────
 * Byte-for-byte the same as freebsd_raw_syscall() in freebsd_syscall_trap.c
 * (kept as a private copy here since that one is `static` to its own
 * translation unit). Same calling convention: >=0 success, <0 = -errno. */
static long
psp_raw_syscall(long nr, long a1, long a2, long a3, long a4, long a5, long a6)
{
    long ret;
#if defined(__x86_64__)
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
    register long _nr  __asm__("x8")  = nr;
    register long _a1  __asm__("x0")  = a1;
    register long _a2  __asm__("x1")  = a2;
    register long _a3  __asm__("x2")  = a3;
    register long _a4  __asm__("x3")  = a4;
    register long _a5  __asm__("x4")  = a5;
    register long _a6  __asm__("x5")  = a6;
    __asm__ volatile (
        "svc #0\n\t"
        "b.cc 1f\n\t"
        "neg %0, %0\n\t"
        "1:"
        : "=r"(_a1)
        : "r"(_nr), "0"(_a1), "r"(_a2), "r"(_a3), "r"(_a4), "r"(_a5), "r"(_a6)
        : "memory"
    );
    ret = _a1;
#else
    #error process_spawn.c: unsupported architecture
#endif
    return ret;
}

/*
 * purpose:  Prefix a macOS-space path with mldr's overlay root, the same
 *           way loader.c does for LC_LOAD_DYLINKER (loader.c:355-370) —
 *           simple concatenation, no normalization.
 * input:    macos_path — guest-supplied path (NUL-terminated).
 *           out/out_size — destination buffer.
 * output:   Resolved length (>=0) on success, or -errno (-EFAULT if
 *           macos_path is NULL, -ENAMETOOLONG if it doesn't fit).
 * sideEffects: none.
 *
 * NOTE: unlike the Linux emulation layer's vchroot_expand() (used by
 * xnu_syscall/bsd/impl/process/execve.c), there is no general path-
 * canonicalization/symlink-aware vchroot resolver available to mldr
 * itself on FreeBSD — mldr's own loader only ever needed this same simple
 * prefix-concatenation for LC_LOAD_DYLINKER. A guest path containing
 * "../" components that escape the overlay is NOT rejected here; that
 * matches loader.c's existing behavior (no such check there either) but
 * is worth flagging explicitly since escaping the overlay for an exec
 * target is more consequential than for a dylib load. See the SPEC doc.
 */
static long
psp_resolve_overlay_path(const char *macos_path, char *out, size_t out_size)
{
    if (macos_path == NULL || out == NULL)
        return -EFAULT;

    size_t path_len = strlen(macos_path);
    size_t root_len = (mldr_load_results.root_path != NULL) ? mldr_load_results.root_path_length : 0;

    if (root_len + path_len + 1 > out_size)
        return -ENAMETOOLONG;

    if (root_len > 0)
        memcpy(out, mldr_load_results.root_path, root_len);
    memcpy(out + root_len, macos_path, path_len + 1);

    return (long)(root_len + path_len);
}

/*
 * purpose:  Decide whether a resolved on-disk path is a Mach-O (or FAT/
 *           universal Mach-O) image, i.e. something FreeBSD's own kernel
 *           cannot execve() directly and that must instead be re-run
 *           through mldr itself (see mldr.c:148-166's "mldr!path" argv[0]
 *           convention, already present and unrelated to this change).
 * input:    resolved_path — FreeBSD filesystem path (already overlay-
 *           prefixed).
 * output:   true if the first 4 bytes match a Mach-O/FAT magic; false for
 *           anything else, INCLUDING "couldn't open/read it" — in that
 *           case the caller falls through to a plain execve() attempt,
 *           which will fail with the kernel's own (correct) errno instead
 *           of a misleading one manufactured here.
 * sideEffects: opens/reads/closes resolved_path.
 *
 * Deliberately does NOT replicate the Linux execve.c reference's
 * "#!" / four-printable-bytes script heuristic: FreeBSD's own kernel
 * image activator already understands "#!" shebang scripts natively
 * (imgact_shell), so a script target can just be passed to execve(2)
 * unmodified and the kernel does the interpreter lookup itself — no
 * manual shebang parsing needed here. The one case this does NOT cover
 * is a shebang line naming ANOTHER Mach-O as the interpreter; that is not
 * specially handled (it would fail the normal way, since FreeBSD can't
 * natively exec a Mach-O) — see the SPEC doc's known-gaps list.
 */
static bool
psp_path_is_macho(const char *resolved_path)
{
    long fd = psp_raw_syscall(SYS_open, (long)resolved_path, O_RDONLY, 0, 0, 0, 0);
    if (fd < 0)
        return false;

    unsigned char hdr[4] = {0, 0, 0, 0};
    long n = psp_raw_syscall(SYS_read, fd, (long)hdr, sizeof(hdr), 0, 0, 0);
    psp_raw_syscall(SYS_close, fd, 0, 0, 0, 0, 0);

    if (n < (long)sizeof(hdr))
        return false;

    uint32_t magic;
    memcpy(&magic, hdr, sizeof(magic));

    return magic == MH_MAGIC || magic == MH_CIGAM ||
           magic == MH_MAGIC_64 || magic == MH_CIGAM_64 ||
           magic == FAT_MAGIC || magic == FAT_CIGAM;
}

/*
 * purpose:  Perform the darlingserver checkout/execve(2)/failure-report
 *           dance shared by mldr_sys_execve() and the exec step of
 *           mldr_sys_posix_spawn(). Mirrors execve.c's
 *           `dserver_execve_pipe` handling (xnu_syscall/bsd/impl/process/
 *           execve.c:226-254) exactly, because darlingserver's server-side
 *           Checkout handler (darlingserver/src/call.cpp:331-388) is
 *           OS-agnostic and expects precisely this protocol: send the read
 *           end of a CLOEXEC pipe via checkout, then either execve()
 *           successfully (all CLOEXEC fds — including our write end —
 *           close on their own, darlingserver sees EOF) or write one byte
 *           to the write end on failure (darlingserver sees data instead
 *           of EOF and leaves the process's state alone).
 * input:    path — FreeBSD filesystem path to execve(2) (already resolved/
 *             rewritten by the caller).
 *           argv/envp — passed to execve(2) as-is.
 *           executing_macho — true if `path` is mldr re-exec'ing itself
 *             against a Mach-O target (darlingserver keeps monitoring the
 *             process); false for a plain native exec (darlingserver
 *             treats a successful exec as this process leaving Darling's
 *             world — see Checkout::processCall()'s
 *             process->notifyDead() branch, call.cpp:378-382).
 * output:   Does not return on success. On failure, -errno.
 * sideEffects: creates and closes a pipe; sends an RPC to darlingserver;
 *              on success, replaces the calling process's image.
 */
static long
psp_do_raw_execve(const char *path, char *const argv[], char *const envp[], bool executing_macho)
{
    int pipefd[2];
    long pr = psp_raw_syscall(SYS_pipe2, (long)pipefd, O_CLOEXEC, 0, 0, 0, 0);
    if (pr < 0)
        return pr;

    int server_socket = __dserver_main_thread_socket_fd;
    if (server_socket < 0) {
        psp_raw_syscall(SYS_close, pipefd[0], 0, 0, 0, 0, 0);
        psp_raw_syscall(SYS_close, pipefd[1], 0, 0, 0, 0, 0);
        return -ECONNRESET;
    }

    int checkout_status = dserver_rpc_explicit_checkout(server_socket, pipefd[0], executing_macho);
    /* Our copy of the read end is no longer needed either way — checkout
     * sends darlingserver its own (dup'd via SCM_RIGHTS) copy. Matches
     * execve.c's "close the read end for ourselves" (execve.c:239). */
    psp_raw_syscall(SYS_close, pipefd[0], 0, 0, 0, 0, 0);

    if (checkout_status < 0) {
        psp_raw_syscall(SYS_close, pipefd[1], 0, 0, 0, 0, 0);
        return checkout_status;
    }

    long ret = psp_raw_syscall(SYS_execve, (long)path, (long)argv, (long)envp, 0, 0, 0);

    /* Only reached if execve(2) failed. */
    unsigned char fail_byte = 1;
    psp_raw_syscall(SYS_write, pipefd[1], (long)&fail_byte, 1, 0, 0, 0);
    psp_raw_syscall(SYS_close, pipefd[1], 0, 0, 0, 0, 0);
    return ret;
}

long
mldr_sys_execve(const char *macos_path, char *const argv[], char *const envp[])
{
    if (macos_path == NULL || argv == NULL)
        return -EFAULT;

    char resolved[PATH_MAX];
    long rlen = psp_resolve_overlay_path(macos_path, resolved, sizeof(resolved));
    if (rlen < 0)
        return rlen;

    if (!psp_path_is_macho(resolved)) {
        /* Native target (ELF, shell script FreeBSD's own kernel will
         * interpret, etc.) — exec it directly. A successful exec here
         * means this process has left Darling's world; see
         * psp_do_raw_execve()'s executing_macho=false contract. */
        return psp_do_raw_execve(resolved, argv, envp, false);
    }

    int server_socket = __dserver_main_thread_socket_fd;
    if (server_socket < 0)
        return -ECONNRESET;

    char mldr_path[PATH_MAX];
    uint64_t mldr_path_len = 0;
    int mp_status = dserver_rpc_explicit_mldr_path(server_socket, mldr_path, sizeof(mldr_path), &mldr_path_len);
    if (mp_status < 0)
        return mp_status;
    if (mldr_path_len == 0 || mldr_path_len >= sizeof(mldr_path))
        return -ENAMETOOLONG;
    mldr_path[mldr_path_len] = '\0';

    /* Rewrite argv[0] to "mldr_path!resolved_path" and shift the guest's
     * whole original argv (including its own argv[0]) one slot right —
     * exactly the convention mldr.c's own main() already parses
     * (mldr.c:148-166), just built from the other direction here instead
     * of by the Linux emulation layer's execve.c. */
    char argv0_buf[PATH_MAX * 2 + 2];
    size_t mp_len = strlen(mldr_path);
    size_t rp_len = strlen(resolved);
    if (mp_len + 1 + rp_len + 1 > sizeof(argv0_buf))
        return -ENAMETOOLONG;
    memcpy(argv0_buf, mldr_path, mp_len);
    argv0_buf[mp_len] = '!';
    memcpy(argv0_buf + mp_len + 1, resolved, rp_len + 1);

    char *new_argv[PSP_MAX_ARGV];
    size_t argc = 0;
    while (argv[argc] != NULL) {
        if (argc + 2 >= PSP_MAX_ARGV) /* +1 for argv0_buf, +1 for NULL */
            return -E2BIG;
        argc++;
    }
    new_argv[0] = argv0_buf;
    for (size_t i = 0; i < argc; i++)
        new_argv[i + 1] = argv[i];
    new_argv[argc + 1] = NULL;

    return psp_do_raw_execve(mldr_path, new_argv, envp, true);
}

/*
 * purpose:  Apply a macOS sigset_t (32-bit mask, Darwin signal numbering)
 *           as the calling thread/process's FreeBSD signal mask.
 * input:    macos_sigmask — bit N-1 set means signal N is in the set
 *           (N in 1..31 — macOS's own sigset_t has no room for numbers
 *           above 31, see <sys/signal.h>'s __darwin_sigset_t = uint32_t).
 * output:   0 on success, -errno on failure.
 * sideEffects: changes the calling thread's signal mask.
 *
 * ASSUMPTION (not verified against a live FreeBSD 15.1 <sys/_sigset.h> in
 * this sandboxed session — no FreeBSD system headers are available here):
 * FreeBSD's sigset_t is the standard 4x uint32_t bitmask struct
 * (`{ uint32_t __bits[4]; }`, signals 1..128 across the 4 words), and
 * SIG_SETMASK == 3 (matches every BSD-descended and Linux sigprocmask(2)
 * ABI this project has otherwise assumed, e.g. Linux's rt_sigprocmask use
 * at freebsd_syscall_trap.c's LINUX_SYS_rt_sigprocmask case, but has never
 * been cross-checked against FreeBSD's own value here). Darwin and
 * FreeBSD signal NUMBERS themselves were checked against this repo's own
 * vendored Darwin header (Developer/.../usr/include/sys/signal.h) and
 * confirmed identical for 1..31 (both descend from 4.3BSD numbering) —
 * that part is solid; the sigset_t wire layout and SIG_SETMASK value are
 * the unverified part.
 */
static long
psp_apply_sigmask(uint32_t macos_sigmask)
{
    struct {
        uint32_t bits[4];
    } freebsd_set = {{0, 0, 0, 0}};

    for (int signo = 1; signo <= 31; signo++) {
        if (macos_sigmask & (1u << (signo - 1)))
            freebsd_set.bits[0] |= (1u << (signo - 1));
    }

    return psp_raw_syscall(SYS_sigprocmask, 3 /* SIG_SETMASK */, (long)&freebsd_set, 0, 0, 0, 0);
}

/*
 * purpose:  Apply one posix_spawn() file action to the calling (about to
 *           be exec'd) process. Mirrors the reference file-action switch
 *           in xnu_syscall/bsd/impl/process/posix_spawn.c:231-301, minus
 *           the pipefd[1]-relocation dance that reference does (that
 *           exists there to protect ITS OWN internal error-reporting
 *           pipe from being clobbered by a guest-requested action — the
 *           equivalent protection here is that mldr_sys_posix_spawn()
 *           applies file actions using ITS OWN pipe fds, which are not
 *           exposed to or nameable by the guest's file-action list at
 *           all, so no collision is possible here).
 * input:    act — one action record, in the guest's own address space
 *           (mldr and the guest Mach-O share one address space; no
 *           cross-process copy needed).
 * output:   0 on success, -errno on failure (including -ENOTSUP for
 *           MLDR_PSFA_FILEPORT_DUP2 or an unrecognized type).
 * sideEffects: opens/closes/dups fds, changes cwd, per action type.
 */
static long
psp_apply_file_action(const struct mldr_psfa_action *act)
{
    switch ((mldr_psfa_t)act->psfaa_type) {

    case MLDR_PSFA_CLOSE:
        return psp_raw_syscall(SYS_close, act->psfaa_filedes, 0, 0, 0, 0, 0);

    case MLDR_PSFA_DUP2:
        return psp_raw_syscall(SYS_dup2, act->psfaa_filedes,
                                act->psfaa_dup2args.psfad_newfiledes, 0, 0, 0, 0);

    case MLDR_PSFA_OPEN: {
        char resolved[PATH_MAX];
        long rl = psp_resolve_overlay_path(act->psfaa_openargs.psfao_path, resolved, sizeof(resolved));
        if (rl < 0)
            return rl;

        /* macOS and FreeBSD open(2) flag bit VALUES coincide (both
         * BSD-derived) — no translation needed, same assumption already
         * made by MACOS_SYS_open's own case in freebsd_syscall_trap.c
         * (freebsd_syscall_trap.c:583-584), which passes its flags
         * argument straight through to FreeBSD's SYS_open the same way. */
        long fd = psp_raw_syscall(SYS_open, (long)resolved,
                                   act->psfaa_openargs.psfao_oflag,
                                   act->psfaa_openargs.psfao_mode, 0, 0, 0);
        if (fd < 0)
            return fd;

        if (fd != act->psfaa_filedes) {
            long dr = psp_raw_syscall(SYS_dup2, fd, act->psfaa_filedes, 0, 0, 0, 0);
            psp_raw_syscall(SYS_close, fd, 0, 0, 0, 0, 0);
            if (dr < 0)
                return dr;
        }
        return 0;
    }

    case MLDR_PSFA_CHDIR: {
        char resolved[PATH_MAX];
        long rl = psp_resolve_overlay_path(act->psfaa_chdirargs.psfac_path, resolved, sizeof(resolved));
        if (rl < 0)
            return rl;
        return psp_raw_syscall(SYS_chdir, (long)resolved, 0, 0, 0, 0, 0);
    }

    case MLDR_PSFA_FCHDIR:
        return psp_raw_syscall(SYS_fchdir, act->psfaa_filedes, 0, 0, 0, 0, 0);

    case MLDR_PSFA_INHERIT: {
        long flags = psp_raw_syscall(SYS_fcntl, act->psfaa_filedes, F_GETFD, 0, 0, 0, 0);
        if (flags < 0)
            return flags;
        return psp_raw_syscall(SYS_fcntl, act->psfaa_filedes, F_SETFD,
                                flags & ~FD_CLOEXEC, 0, 0, 0);
    }

    case MLDR_PSFA_FILEPORT_DUP2:
        /* Mach fileports don't exist at this raw-syscall layer — no Mach
         * IPC namespace to resolve one against. Explicitly refused rather
         * than silently skipped: skipping would hand the child a missing
         * fd it was told it would have. */
        return -ENOTSUP;

    default:
        return -ENOTSUP;
    }
}

/*
 * purpose:  Apply every recognized posix_spawnattr_t flag and every file
 *           action, in that order (attrs first, then file actions —
 *           matches the reference's own ordering, posix_spawn.c:75-305,
 *           and matters: e.g. a SETPGROUP or RESETIDS should land before
 *           any newly-opened fd's permissions could depend on them).
 * input:    attr — may be NULL (no attributes requested).
 *           facts — may be NULL (no file actions requested).
 * output:   0 on success, -errno on the first failure (including
 *           -ENOTSUP for SETSIGDEF and CLOEXEC_DEFAULT — see below).
 * sideEffects: see psp_apply_sigmask()/psp_apply_file_action(); may call
 *              setuid/setgid/setpgid/sigprocmask; may call
 *              dserver_rpc_explicit_stop_after_exec().
 */
static long
psp_apply_spawn_attrs_and_actions(const struct mldr_posix_spawnattr *attr,
                                   const struct mldr_posix_spawn_file_actions *facts)
{
    if (attr != NULL) {
        if (attr->psa_flags & MLDR_POSIX_SPAWN_SETSIGDEF) {
            /* Recognized but not implemented: resetting a set of signals
             * to SIG_DFL needs a sigaction(2) per signal in the set, which
             * this implementation does not do. Refused explicitly rather
             * than silently continuing with the caller's current
             * dispositions, which is not what was asked for. */
            return -ENOTSUP;
        }

        if (attr->psa_flags & MLDR_POSIX_SPAWN_RESETIDS) {
            long ruid = psp_raw_syscall(SYS_getuid, 0, 0, 0, 0, 0, 0);
            long rgid = psp_raw_syscall(SYS_getgid, 0, 0, 0, 0, 0, 0);
            /* Order matches the reference (posix_spawn.c:79-80): drop gid
             * before uid, so the process never briefly holds a raised uid
             * with its old (possibly more privileged) gid still active. */
            long r = psp_raw_syscall(SYS_setgid, rgid, 0, 0, 0, 0, 0);
            if (r < 0)
                return r;
            r = psp_raw_syscall(SYS_setuid, ruid, 0, 0, 0, 0, 0);
            if (r < 0)
                return r;
        }

        if (attr->psa_flags & MLDR_POSIX_SPAWN_SETPGROUP) {
            long r = psp_raw_syscall(SYS_setpgid, 0, attr->psa_pgroup, 0, 0, 0, 0);
            if (r < 0)
                return r;
        }

        if (attr->psa_flags & MLDR_POSIX_SPAWN_SETSIGMASK) {
            long r = psp_apply_sigmask(attr->psa_sigmask);
            if (r < 0)
                return r;
        }

        if (attr->psa_flags & MLDR_POSIX_SPAWN_START_SUSPENDED) {
            int server_socket = __dserver_main_thread_socket_fd;
            if (server_socket < 0)
                return -ECONNRESET;
            long r = dserver_rpc_explicit_stop_after_exec(server_socket);
            if (r < 0)
                return r;
        }

        if (attr->psa_flags & MLDR_POSIX_SPAWN_CLOEXEC_DEFAULT) {
            /* Recognized but not implemented: doing this correctly means
             * enumerating every open fd (getdirentries(2) on /dev/fd) and
             * setting FD_CLOEXEC on each one not named by a file action —
             * meaningfully more code, for a flag NSTask does not set by
             * default. Refused explicitly rather than silently leaving
             * fds inheritable that the caller asked to have closed on
             * exec, which is a real (if narrow) fd-leak/confused-deputy
             * risk to hand the spawned process. */
            return -ENOTSUP;
        }
    }

    if (facts != NULL) {
        for (int32_t i = 0; i < facts->psfa_act_count; i++) {
            long r = psp_apply_file_action(&facts->psfa_act_acts[i]);
            if (r < 0)
                return r;
        }
    }

    return 0;
}

/*
 * purpose:  fork(2) plus the darlingserver re-registration a genuine new
 *           process needs (see this file's header comment for why raw
 *           fork() alone isn't enough). Shared by mldr_sys_fork() and the
 *           fork+exec path of mldr_sys_posix_spawn().
 * input:    (none)
 * output:   Parent: child pid (>=0) or -errno (fork(2) itself failed — no
 *             child exists, nothing else to clean up).
 *           Child: 0 (already re-registered with darlingserver by the
 *             time this returns).
 * sideEffects: see mldr_sys_fork()'s contract in process_spawn.h — this
 *   IS that implementation, factored out so posix_spawn's fork+exec path
 *   doesn't duplicate it.
 */
static long
psp_fork_and_register(void)
{
    long ret = psp_raw_syscall(SYS_fork, 0, 0, 0, 0, 0, 0);
    if (ret != 0)
        return ret; /* parent, or fork(2) itself failed */

    /* ── child ── */

    int old_kernfd = __dserver_main_thread_socket_fd;
    int old_lifetime_fd = __dserver_process_lifetime_pipe_fd;

    /* Close the inherited copies FIRST — see this file's header comment,
     * point 1: they still point at the exact same kernel objects the
     * parent is using, and leaving them open invites the parent and this
     * child to race on them. Closed via the existing
     * __mldr_close_*()  helpers (not a raw SYS_close) so the CHILD's own
     * (fork-copied, independent from the parent's) socket_bitmap stays
     * consistent — a raw close would leave a stale "reserved" bit for an
     * fd number nothing still holds, which a later __mldr_create_rpc_socket()
     * call in this same child could then hand out a SECOND time.
     *
     * KNOWN LEAK introduced by using __mldr_close_rpc_socket() here rather
     * than a raw close(): that function's FreeBSD unlink path is built
     * from getpid() (mldr.c:865, "/tmp/darling-mldr-%d-%d"), computed at
     * CLOSE time. The socket being closed here was bound under the
     * PARENT's pid (mldr.c:821-822, also getpid()-based, at the time the
     * PARENT created it) — by the time this child calls
     * __mldr_close_rpc_socket(), getpid() already returns the CHILD's own
     * (different) pid, so the unlink here targets a path that was never
     * bound, and the actual socket file the parent created is left behind
     * in /tmp permanently. This is a real, narrow leak (a small unlinked-
     * looking-but-not socket file per fork(), never cleaned up), traded
     * deliberately against the stale-bitmap-bit risk of a raw close()
     * described above — not fixed here (fixing it means either passing
     * the original binder's pid through to the unlink, or moving to a
     * naming scheme that doesn't embed pid at all; out of scope for this
     * change). See the SPEC doc. */
    if (old_kernfd >= 0)
        __mldr_close_rpc_socket(old_kernfd);
    if (old_lifetime_fd >= 0)
        __mldr_close_process_lifetime_pipe(old_lifetime_fd);

    /* See this file's header "async-signal-safety" note for the mutex/
     * realloc caveat on this call. */
    int new_kernfd = __mldr_create_rpc_socket();
    if (new_kernfd < 0) {
        /* No working socket means there is no way left to tell
         * darlingserver — or anything else — that this happened. A child
         * that continues here exists but is permanently invisible to
         * darlingserver's process tracking. Matches Linux Darling's own
         * sys_fork(), which __simple_abort()s in the equivalent spot
         * (xnu_syscall/bsd/impl/process/fork.c:61-62). */
        psp_raw_syscall(SYS__exit, 127, 0, 0, 0, 0, 0);
        __builtin_unreachable();
    }
    __dserver_main_thread_socket_fd = new_kernfd;
    mldr_load_results.kernfd = new_kernfd; /* keep the mirror in sync, same as setup_space() (mldr.c:960,966) */

    int lifetime_pipe[2];
    int lp_status = __mldr_create_process_lifetime_pipe(lifetime_pipe);
    int read_end = -1;
    if (lp_status == 0) {
        read_end = lifetime_pipe[0];
        __dserver_process_lifetime_pipe_fd = lifetime_pipe[1];
    } else {
        /* Not immediately fatal: dserver_rpc_explicit_checkin() accepts
         * lifetime_listener_pipe == -1 (matches how mldr's own startup
         * path already tolerates it — see the `is_kernel_at_least(5, 3)`
         * short-circuit in __mldr_create_process_lifetime_pipe's Linux
         * branch, mldr.c:873-877 — though FreeBSD's own build of that
         * function always takes the pipe path today, so reaching this
         * branch here means the pipe() syscall itself failed, which is a
         * real (if rare) degradation: darlingserver loses one specific
         * signal, its EVFILT_PROC-based pidfd fallback (process.cpp:44-62)
         * still covers plain process-exit tracking). */
        __dserver_process_lifetime_pipe_fd = -1;
    }

    int dummy_stack_hint;
    int checkin_status = dserver_rpc_explicit_checkin(new_kernfd, true, &dummy_stack_hint, read_end);

    if (read_end >= 0)
        psp_raw_syscall(SYS_close, read_end, 0, 0, 0, 0, 0);

    if (checkin_status < 0) {
        /* Same reasoning as the new_kernfd failure above: an unregistered
         * child is worse than no child. */
        psp_raw_syscall(SYS__exit, 127, 0, 0, 0, 0, 0);
        __builtin_unreachable();
    }

    return 0;
}

long
mldr_sys_fork(void)
{
    return psp_fork_and_register();
}

long
mldr_sys_vfork(void)
{
    /* See process_spawn.h's contract comment for why this is fork(2), not
     * a true vfork(2): the two are only permitted to differ in
     * performance for a POSIX-conforming program (one that only calls
     * _exit()/execve() in the vfork "child"), and a real vfork's shared-
     * address-space-until-exec semantics are incompatible with running
     * the checkin dance in psp_fork_and_register() without corrupting the
     * suspended parent's own state. */
    return psp_fork_and_register();
}

long
mldr_sys_posix_spawn(pid_t *pid_out, const char *macos_path,
                      const struct mldr_posix_spawn_args_desc *desc,
                      char *const argv[], char *const envp[])
{
    if (macos_path == NULL || argv == NULL)
        return -EFAULT;

    const struct mldr_posix_spawnattr *attr = (desc != NULL) ? desc->attrp : NULL;
    const struct mldr_posix_spawn_file_actions *facts = (desc != NULL) ? desc->file_actions : NULL;

    if (desc != NULL) {
        /* See process_spawn.h's struct-level comment: these are
         * privilege/sandboxing-relevant macOS features with no FreeBSD
         * equivalent implemented here. Refused rather than ignored. */
        if (desc->port_actions != NULL || desc->mac_extensions != NULL ||
            desc->coal_info != NULL || desc->persona_info != NULL ||
            desc->posix_cred_info != NULL || desc->subsystem_root_path != NULL) {
            return -ENOTSUP;
        }
    }

    if (facts != NULL) {
        for (int32_t i = 0; i < facts->psfa_act_count; i++) {
            if (facts->psfa_act_acts[i].psfaa_type == MLDR_PSFA_FILEPORT_DUP2)
                return -ENOTSUP;
        }
    }

    bool set_exec = (attr != NULL) && (attr->psa_flags & MLDR_POSIX_SPAWN_SETEXEC);

    if (set_exec) {
        /* No-fork variant: apply attributes/file actions to the CALLING
         * process, then exec in place. Real posix_spawn(2) semantics for
         * POSIX_SPAWN_SETEXEC — only returns on failure, same as
         * execve(). */
        long r = psp_apply_spawn_attrs_and_actions(attr, facts);
        if (r < 0)
            return r;
        return mldr_sys_execve(macos_path, argv, envp);
    }

    /* fork+exec variant. A spawn failure from here on is reported to the
     * PARENT over this pipe — matches the reference's own error-reporting
     * pipe (posix_spawn.c:56,343) — because by the time anything here can
     * fail, the child has already diverged from the parent's control flow
     * and there's no other way back to it.
     *
     * KNOWN GAP (matches the reference exactly, not introduced here): a
     * child that dies via _exit(127) INSIDE psp_fork_and_register()
     * itself (checkin failure — see that function) closes this pipe's
     * write end without writing anything, which reads back as a clean
     * EOF — indistinguishable here from "execve succeeded, CLOEXEC closed
     * it". Upstream's own sys_posix_spawn() has the identical gap (its
     * "if (sys_read(...) != sizeof(ret)) ret = 0;" at posix_spawn.c:367-368
     * treats any short/empty read as success too). Not fixed here to keep
     * behavior aligned with upstream; see the SPEC doc. */
    int pipefd[2];
    long pr = psp_raw_syscall(SYS_pipe2, (long)pipefd, O_CLOEXEC, 0, 0, 0, 0);
    if (pr < 0)
        return pr;

    long fork_ret = psp_fork_and_register();

    if (fork_ret != 0) {
        /* parent (fork_ret > 0), or fork/checkin failed (fork_ret < 0 —
         * note psp_fork_and_register() only returns negative for a failed
         * fork(2) itself; a failed checkin _exit()s the child instead of
         * returning, per the KNOWN GAP above). */
        psp_raw_syscall(SYS_close, pipefd[1], 0, 0, 0, 0, 0);

        if (fork_ret < 0) {
            psp_raw_syscall(SYS_close, pipefd[0], 0, 0, 0, 0, 0);
            return fork_ret;
        }

        if (pid_out != NULL)
            *pid_out = (pid_t)fork_ret;

        int spawn_errno = 0;
        long rr = psp_raw_syscall(SYS_read, pipefd[0], (long)&spawn_errno, sizeof(spawn_errno), 0, 0, 0);
        psp_raw_syscall(SYS_close, pipefd[0], 0, 0, 0, 0, 0);

        if (rr == (long)sizeof(spawn_errno) && spawn_errno != 0)
            return -spawn_errno;
        return 0;
    }

    /* ── child (already checked in as a new darlingserver-visible process
     * by psp_fork_and_register()) ── */
    psp_raw_syscall(SYS_close, pipefd[0], 0, 0, 0, 0, 0);

    long ar = psp_apply_spawn_attrs_and_actions(attr, facts);
    if (ar < 0) {
        int err = (int)-ar;
        psp_raw_syscall(SYS_write, pipefd[1], (long)&err, sizeof(err), 0, 0, 0);
        psp_raw_syscall(SYS__exit, 127, 0, 0, 0, 0, 0);
        __builtin_unreachable();
    }

    long er = mldr_sys_execve(macos_path, argv, envp);
    /* Only reached if execve failed (psp_do_raw_execve() inside it already
     * told darlingserver about the failure via the checkout pipe — this
     * second pipe write is a SEPARATE report, to OUR parent, over OUR OWN
     * pipe, not the one execve's checkout dance used). */
    int err = (int)-er;
    psp_raw_syscall(SYS_write, pipefd[1], (long)&err, sizeof(err), 0, 0, 0);
    psp_raw_syscall(SYS__exit, 127, 0, 0, 0, 0, 0);
    __builtin_unreachable();
}

#endif /* DARLING_FREEBSD */
