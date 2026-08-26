/*
 * sigaction_translate.h — Linux ABI `rt_sigaction` -> FreeBSD `sigaction(2)`
 * bridge for mldr.
 *
 * purpose:    Declare mldr_sys_rt_sigaction(), the translator that lets
 *             `dispatch_linux_syscall()`'s LINUX_SYS_rt_sigaction case (see
 *             freebsd_syscall_trap.c) install a REAL FreeBSD signal
 *             disposition on behalf of guest Linux-ABI code, instead of the
 *             current pass-through (`freebsd_raw_syscall(SYS_sigaction, a1,
 *             a2, a3, 0, 0, 0)`) which hands the kernel a Linux-layout
 *             `struct sigaction` it cannot parse and always returns EINVAL.
 *
 * Written against docs/SPEC-signal-abi-bridge.md in this tree — that
 * document has the full field-by-field layout comparison, the sa_flags bit
 * tables, and the reasoning behind every decision below; this header only
 * restates the parts a caller needs, not the derivation.
 *
 * Why this exists as its own translation unit rather than a case body
 * inline in freebsd_syscall_trap.c: this file was written without editing
 * any existing file (task constraint — verify with the calling agent
 * before assuming that still holds). Its caller is expected to add exactly
 * one `case LINUX_SYS_rt_sigaction:` arm to `dispatch_linux_syscall()` that
 * forwards to mldr_sys_rt_sigaction() — see the worked example in this
 * header's closing comment block.
 *
 * Ownership of the two "signals mldr itself needs" facts this file
 * depends on:
 *   - `mldr_linux_signo_to_freebsd()` (freebsd_syscall_trap.c) is `static`
 *     to that translation unit, so it cannot be called from here. Rather
 *     than un-`static` it (an edit to an existing file), this file keeps
 *     its OWN private copy — see sigaction_translate.c's
 *     `sat_linux_signo_to_freebsd()`. Same pattern already used in this
 *     tree for `freebsd_raw_syscall()` -> `psp_raw_syscall()` in
 *     process_spawn.c (see that file's near-identical header comment).
 *     **Risk**: two independently-maintained copies of the same signal
 *     table can drift. Keep them in sync by hand until someone hoists a
 *     shared, non-static version into a common header.
 *   - The four FreeBSD signal numbers mldr's own SIGSYS/SIGILL/SIGBUS/
 *     SIGSEGV handlers occupy (setup_macos_syscall_trap(),
 *     freebsd_syscall_trap.c:2901-2962) must NEVER have their FreeBSD
 *     `sigaction()` disposition touched by a guest rt_sigaction() call —
 *     doing so would tear down the very mechanism this whole file's
 *     syscall trap depends on. See §5 of the SPEC doc and the
 *     SAT_RESERVED_FREEBSD_SIGNO table in the .c file.
 *
 * Async-signal-safety: mldr_sys_rt_sigaction() is reachable from
 * `sigill_handler()` (a SIGILL handler, see freebsd_syscall_trap.c:2771),
 * i.e. it may run with an arbitrary guest fsbase live and inside a signal
 * context. It performs no malloc()/free() and touches only a static,
 * process-wide, fixed-size table (see "Threading" below) plus the raw
 * syscall path — same async-signal-safety posture as
 * `LINUX_SYS_rt_sigprocmask`'s existing handling in the same dispatcher.
 *
 * Threading: the per-signal "what did the guest last ask for" registry
 * (needed because FreeBSD's `sigaction(2)` has no `sa_restorer` field to
 * echo back on a later oldact query — see SPEC §4/§7 step 5) is a single
 * process-wide static array, NOT lock-protected. This mirrors the SPEC's
 * documented, explicit compromise (SPEC "Открытые вопросы" #4): mldr
 * already has no locking on the comparable per-signal state elsewhere in
 * this file (e.g. `_sigsys_altstack`), and sigaction() registration is
 * overwhelmingly a startup-time, not steady-state-concurrent, operation.
 * A genuine race here means two guest threads racing rt_sigaction() on the
 * SAME signal number at the SAME time; the loser's registry entry can be
 * observed torn (mixed old/new fields) by a concurrent oldact reader. This
 * is a known, accepted gap, not an oversight — flagging it here per the
 * task's "no quiet stubs" instruction, since it is the one place this
 * file deliberately leaves a hole open.
 *
 * sideEffects: sigaction_translate.c defines one process-wide static
 *              registry (see above) that mldr_sys_rt_sigaction() reads and
 *              writes; no other global state.
 */

#pragma once

#ifdef DARLING_FREEBSD

#include <stddef.h> /* size_t */

/*
 * purpose:  Translate and execute a Linux `rt_sigaction(signum, act, oldact,
 *           sigsetsize)` syscall against the real FreeBSD `sigaction(2)`,
 *           per docs/SPEC-signal-abi-bridge.md.
 *
 * input:    linux_signo — Linux signal number as the guest passed it
 *             (`a1`/`rdi` of the raw Linux `rt_sigaction` syscall; NOT yet
 *             translated to a FreeBSD number — that happens inside).
 *           act    — guest pointer to a Linux-ABI-layout `struct
 *             linux_sigaction_wire` (see the .c file for the exact layout;
 *             SPEC §1) describing the NEW disposition, or NULL to only
 *             query the current one.
 *           oldact — guest pointer to receive the PREVIOUS disposition in
 *             the same Linux-ABI layout, or NULL if the caller doesn't
 *             want it.
 *           sigsetsize — the `a4`/`r10` argument from the raw syscall
 *             (`sizeof(linux_sigset_t)`, always 8 on this ABI per SPEC §0).
 *             Validated but otherwise unused: Linux's own kernel entry
 *             point rejects any value other than sizeof(linux_sigset_t)
 *             defensively against ABI skew between caller and kernel; this
 *             translator does the same rather than silently accepting a
 *             mismatched size that would indicate the caller and this file
 *             disagree about the wire struct.
 *
 * output:   0 on success; a negative errno value on failure. Specifically:
 *             -EINVAL  — linux_signo out of the valid 1..31 range, OR
 *                        sigsetsize != sizeof(linux 8-byte sigset_t).
 *             -EFAULT  — act/oldact point somewhere unreadable/unwritable
 *                        (surfaces only if the underlying FreeBSD
 *                        sigaction(2) call itself faults; this function
 *                        does not pre-validate guest pointers beyond
 *                        NULL-checks, matching the rest of this
 *                        dispatcher's style, e.g. LINUX_SYS_rt_sigprocmask).
 *             other    — whatever FreeBSD's sigaction(2) returns via
 *                        freebsd_raw_syscall(), unmodified.
 *           For Linux signals with no FreeBSD counterpart at all
 *           (SIGSTKFLT=16, SIGPWR=30, or any realtime signal >31 — none of
 *           which this ABI's 8-byte sigsetsize can even represent past bit
 *           63) this returns 0 and, if oldact != NULL, writes an all-zero
 *           `struct linux_sigaction_wire` — mirroring the existing guest-
 *           side `sys_sigaction()`'s deliberate "unknown signal is a no-op
 *           success" behavior (SPEC §7 step 2, §5's resilience rationale:
 *           code that probes every signal number in a loop must not die on
 *           the first gap in a 1:1 mapping between two different kernels).
 *
 * sideEffects:
 *   - For the four FreeBSD signals mldr's own SIGSYS/SIGILL/SIGBUS/SIGSEGV
 *     handlers occupy, NO real `sigaction(2)` call is made at all — the
 *     guest's request is recorded in this file's private registry only,
 *     and `oldact` (if requested) is answered FROM that registry, never
 *     from the kernel's actual (mldr-owned) disposition. This is the one
 *     mandatory "quiet success without doing the real thing" case in this
 *     file, and it is mandatory: honoring the guest's request literally
 *     would replace mldr's own syscall-trap handler and break the
 *     mechanism this very function is reached through. See SPEC §5.
 *   - For every other valid signal, calls the real FreeBSD `sigaction(2)`
 *     via a raw syscall (own private `sat_raw_syscall()` copy — see
 *     rationale in this header's top comment) and, on success, updates
 *     this file's per-signal wire-format registry so a later oldact query
 *     (on THIS signal) can report back fields FreeBSD's `sigaction(2)`
 *     itself has no storage for (`sa_restorer`) — see SPEC §7 step 5.
 *   - Never blocks, never allocates.
 *
 * ---------------------------------------------------------------------
 * Worked example of the single case arm the caller is expected to add to
 * `dispatch_linux_syscall()`'s switch in freebsd_syscall_trap.c, replacing
 * the current broken case at (per the SPEC doc) lines 2269-2273:
 *
 *     case LINUX_SYS_rt_sigaction:
 *         return mldr_sys_rt_sigaction((int)a1, (const void *)a2,
 *                                       (void *)a3, (size_t)a4);
 *
 * `a4` here is `sizeof(sa.sa_mask)` as sent by the guest's sys_sigaction()
 * (SPEC §0) — already the argument the existing (broken) case simply
 * dropped. This file needs `#include "sigaction_translate.h"` added near
 * freebsd_syscall_trap.c's other local includes.
 * ---------------------------------------------------------------------
 */
long mldr_sys_rt_sigaction(int linux_signo, const void *act, void *oldact,
                            size_t sigsetsize);

#endif /* DARLING_FREEBSD */
