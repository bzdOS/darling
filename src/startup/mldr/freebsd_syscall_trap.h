/*
 * freebsd_syscall_trap.h — FreeBSD-only macOS syscall interception via SIGSYS.
 *
 * purpose:    Expose setup_macos_syscall_trap() for mldr.c to call before
 *             start_thread() hands control to the Mach-O entry point.
 * sideEffects: None on Linux (entire file is guarded by #ifdef DARLING_FREEBSD).
 */

#pragma once

/*
 * Entry points the guest calls directly (elfcalls) - the fork child among them (sys_fork closes and
 * recreates its RPC socket and lifetime pipe). The guest may reach them with rsp not 16-byte aligned;
 * host libc then faults on an aligned SSE store (movaps in vfprintf: #GP, delivered as SIGBUS at the
 * instruction). force_align_arg_pointer realigns the stack on entry.
 */
#if defined(__x86_64__)
#define MLDR_GUEST_ENTRY __attribute__((force_align_arg_pointer))
#else
#define MLDR_GUEST_ENTRY
#endif

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

/*
 * purpose:   Call fn(arg) as a guest signal handler. If the native signal was
 *            delivered on mldr's own signal stack, switch to the guest's stack
 *            first: the interrupted guest rsp minus the red zone, or the top of
 *            the guest's registered sigaltstack when sa_onstack is set and one
 *            exists. The handler never runs on mldr's stack.
 * input:     intr_rsp — rsp of the interrupted context; sa_onstack — guest asked SA_ONSTACK.
 */
void mldr_call_guest_handler(uintptr_t intr_rsp, int sa_onstack, void (*fn)(void *), void *arg);

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

/*
 * purpose:   Register a guest image mapping in mldr's image registry and arm
 *            any MLDR_TRAP_AT/MLDR_TRAP_RESUME_AT sites that name it. The mmap
 *            syscall trap calls this for dyld-mapped images; mldr calls it
 *            directly for dyld itself because dyld is mapped by loader.c, not
 *            by a guest mmap syscall.
 * input:     base/size — mapped range; fd — backing fd (for path lookup via
 *            F_KINFO, -1 if unavailable); executable — PROT_EXEC;
 *            name — image name for trap matching and address resolution
 *            (NULL to derive from fd via F_KINFO).
 * output:    none.
 * sideEffects: appends to image registry, logs when MLDR_LOG_MAPPINGS is set,
 *              may plant ud2 traps in executable images.
 */
void mldr_register_image_mapping(void *base, size_t size, int fd, bool executable, const char *name);
#endif /* __x86_64__ */

#endif /* DARLING_FREEBSD */
