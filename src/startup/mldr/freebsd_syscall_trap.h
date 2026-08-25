/*
 * freebsd_syscall_trap.h — FreeBSD-only macOS syscall interception via SIGSYS.
 *
 * purpose:    Expose setup_macos_syscall_trap() for mldr.c to call before
 *             start_thread() hands control to the Mach-O entry point.
 * sideEffects: None on Linux (entire file is guarded by #ifdef DARLING_FREEBSD).
 */

#pragma once

#ifdef DARLING_FREEBSD

#include <stddef.h> /* size_t */

/*
 * purpose:   Install a SA_SIGINFO SIGSYS handler that translates macOS BSD
 *            syscall numbers (class 0x2000000) to FreeBSD native syscalls and
 *            emulates them in-handler, then resumes the faulting instruction.
 * input:     none
 * output:    none
 * sideEffects: Registers a process-wide SIGSYS handler; sets SA_ONSTACK so
 *              the handler runs on an alternate signal stack (avoids clobbering
 *              the Mach-O stack).
 */
void setup_macos_syscall_trap(void);

/*
 * purpose:   Register an alternate signal stack for the CALLING thread, so the
 *            SA_ONSTACK handlers installed by setup_macos_syscall_trap() can
 *            run on threads created after it. sigaltstack(2) is per-thread, so
 *            the initial thread's registration covers only that thread; guest
 *            threads (libdispatch workers, pthreads) must call this from their
 *            entry point — see darling_thread_entry in elfcalls/threads.c.
 * input:     none
 * output:    0 on success, -1 on failure (reported to stderr; the thread stays
 *            runnable, but a raw-syscall trap on it will crash).
 * sideEffects: Maps a per-thread stack and registers it via sigaltstack().
 */
int mldr_setup_thread_signal_stack(void);

#if defined(__x86_64__)
/*
 * purpose:   Rewrite the fixed-signature raw-Linux-syscall trampolines
 *            (the generic `mov eax,[rsp+8]; syscall; ret` thunk and the
 *            `mov eax,15; syscall` rt_sigreturn stub) inside an already-
 *            mapped, executable dyld image from `syscall` (0F 05) to `ud2`
 *            (0F 0B). See the raw-Linux-ABI section of freebsd_syscall_trap.c
 *            for why SIGSYS cannot reliably catch these: most low Linux
 *            syscall numbers collide with real, implemented FreeBSD
 *            syscalls at the same number, so the FreeBSD kernel runs its
 *            OWN syscall instead of ever raising SIGSYS.
 * input:     base — start of a mapped, page-aligned-or-not EXEC segment;
 *            size — length of that segment's file-backed content.
 * output:    none
 * sideEffects: Temporarily adds PROT_WRITE to the pages covering [base,
 *              base+size) via mprotect(), then restores the original
 *              (execute-only) protection. Logs to stderr if a signature is
 *              found a suspicious number of times (0 is not logged: not
 *              every EXEC segment contains these trampolines).
 */
void mldr_patch_linux_raw_syscalls(void *base, size_t size);
#endif /* __x86_64__ */

#endif /* DARLING_FREEBSD */
