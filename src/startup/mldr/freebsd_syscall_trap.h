/*
 * freebsd_syscall_trap.h — FreeBSD-only macOS syscall interception via SIGSYS.
 *
 * purpose:    Expose setup_macos_syscall_trap() for mldr.c to call before
 *             start_thread() hands control to the Mach-O entry point.
 * sideEffects: None on Linux (entire file is guarded by #ifdef DARLING_FREEBSD).
 */

#pragma once

#ifdef DARLING_FREEBSD

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

#endif /* DARLING_FREEBSD */
