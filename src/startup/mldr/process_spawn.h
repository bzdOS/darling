/*
 * process_spawn.h — macOS process-creation syscalls (fork/vfork/execve/
 * posix_spawn) for mldr's FreeBSD SIGSYS syscall trap.
 *
 * purpose:    Declare the entry points `dispatch_macos_bsd_syscall()` (in
 *             freebsd_syscall_trap.c) is meant to call for MACOS_SYS_fork,
 *             MACOS_SYS_vfork, MACOS_SYS_execve and MACOS_SYS_posix_spawn.
 * input:      n/a (declarations only)
 * output:     n/a
 * sideEffects: n/a
 *
 * See docs/SPEC-mldr-process-creation.md for:
 *   - the darlingserver checkin/checkout protocol these functions drive
 *     (why raw fork()/execve() alone are NOT enough for darlingserver to
 *     recognize the new/replaced process),
 *   - where to wire the MACOS_SYS_* case branches into
 *     dispatch_macos_bsd_syscall(), and
 *   - known-unverified risk areas (signal-handler context, guest fsbase).
 *
 * Calling convention (matches freebsd_raw_syscall()'s convention used
 * throughout freebsd_syscall_trap.c): return value >= 0 is success (macOS
 * BSD syscalls share POSIX errno numbers with FreeBSD for everything used
 * here, so no translation table is needed — see the file header comment in
 * freebsd_syscall_trap.c), return value < 0 is -errno.
 *
 * Async-signal-safety: every function here may be called directly from
 * mldr's SIGSYS handler (sigsys_handler() in freebsd_syscall_trap.c), i.e.
 * with an arbitrary, possibly-guest fsbase live (see the "fsbase" note in
 * process_spawn.c's file header and the SPEC doc). None of these functions
 * call malloc()/free(), and none reference any `__thread`-qualified
 * variable directly — RPC calls go through dserver_rpc_explicit_checkin()/
 * dserver_rpc_explicit_checkout()/etc (the *_explicit_* entry points,
 * which take the server socket fd as an explicit argument) rather than
 * the plain dserver_rpc_*() ones, specifically to avoid the TLS-based
 * socket lookup those resolve to — see process_spawn.c's file header.
 * __mldr_create_rpc_socket() (called from mldr_sys_fork()/vfork()'s child
 * path) is reused as-is from the existing codebase; it is NOT proven
 * async-signal-safe in this context (the mutex inside
 * socket_bitmap_get() being the specific concern) — see the SPEC doc's
 * risk list. This is a pre-existing property of mldr's RPC plumbing, not
 * something introduced here.
 */

#ifndef _MLDR_PROCESS_SPAWN_H_
#define _MLDR_PROCESS_SPAWN_H_

#ifdef DARLING_FREEBSD

#include <stdint.h>
#include <stdbool.h>
#include <sys/types.h>
#include <limits.h> /* PATH_MAX */

/* ── on-wire mirror of the macOS libSystem posix_spawn() ABI ──────────────
 *
 * These structs mirror (field-for-field, same order, same types) the
 * layout in:
 *   Developer/Platforms/MacOSX.platform/Developer/SDKs/MacOSX.sdk/
 *     usr/include/sys/spawn.h            (flag bit values)
 *   src/external/xnu/bsd/sys/spawn_internal.h
 *     (struct _posix_spawnattr, struct _psfa_action,
 *      struct _posix_spawn_file_actions, struct _posix_spawn_args_desc)
 *
 * WHY a private mirror instead of #include-ing spawn_internal.h directly:
 * that header pulls in mach/coalition.h, mach/task_policy.h, os/overflow.h
 * and other xnu-internal headers whose availability/buildability in mldr's
 * own (FreeBSD-native, non-xnu-emulation) compilation unit was not checked
 * in this session — no build was run here (out of scope for this change).
 * A local mirror avoids a fragile, unverified include chain. It DOES mean
 * this struct can silently drift from the real one if Apple/upstream xnu
 * ever changes it; see the SPEC doc's risk list for a suggested
 * static_assert/offsetof guard to add once this builds on real hardware.
 *
 * Only the fields this implementation actually acts on are given real
 * effect; every other populated pointer (port_actions, mac_extensions,
 * coal_info, persona_info, posix_cred_info, subsystem_root_path) is
 * rejected with -ENOTSUP rather than silently ignored — those are
 * privilege/sandboxing-relevant macOS features with no FreeBSD equivalent,
 * and silently dropping them would let a caller believe a security-relevant
 * request took effect when it did not. Purely advisory/resource-hint fields
 * (jetsam memory limits, QoS clamp, apptype, ...) are intentionally no-ops,
 * same as upstream Darling's own Linux implementation (see
 * src/external/xnu/darling/src/libsystem_kernel/emulation/src/xnu_syscall/
 * bsd/impl/process/posix_spawn.c, which has the same "// TODO: other
 * attributes" gap).
 */

#define MLDR_NBINPREFS 4

struct mldr_posix_spawnattr {
	int16_t  psa_flags;
	int16_t  flags_padding;
	uint32_t psa_sigdefault;        /* Darwin sigset_t is a 32-bit mask */
	uint32_t psa_sigmask;
	pid_t    psa_pgroup;
	int32_t  psa_binprefs[MLDR_NBINPREFS];
	int32_t  psa_pcontrol;
	int32_t  psa_apptype;
	uint64_t psa_cpumonitor_percent;
	uint64_t psa_cpumonitor_interval;
	uint64_t psa_reserved;

	int16_t  psa_jetsam_flags;
	int16_t  short_padding;
	int32_t  psa_priority;
	int32_t  psa_memlimit_active;
	int32_t  psa_memlimit_inactive;

	uint64_t psa_qos_clamp;
	int32_t  psa_darwin_role;
	int32_t  psa_thread_limit;

	uint64_t psa_max_addr;
	bool     psa_no_smt;
	bool     psa_tecs;
	int32_t  psa_platform;

	int32_t  psa_subcpuprefs[MLDR_NBINPREFS];
	uint32_t psa_options;

	/* Every pointer below this line, if non-NULL, is rejected — see the
	 * struct-level comment above. */
	void *psa_ports;
	void *psa_mac_extensions;
	void *psa_coalition_info;
	void *psa_persona_info;
	void *psa_posix_cred_info;
	char *psa_subsystem_root_path;
};

/* POSIX_SPAWN_* flag bits (sys/spawn.h) actually consulted here. */
#define MLDR_POSIX_SPAWN_RESETIDS        0x0001
#define MLDR_POSIX_SPAWN_SETPGROUP       0x0002
#define MLDR_POSIX_SPAWN_SETSIGDEF       0x0004 /* recognized, rejected — see .c */
#define MLDR_POSIX_SPAWN_SETSIGMASK      0x0008
#define MLDR_POSIX_SPAWN_SETEXEC         0x0040
#define MLDR_POSIX_SPAWN_START_SUSPENDED 0x0080
#define MLDR_POSIX_SPAWN_CLOEXEC_DEFAULT 0x4000 /* recognized, rejected — see .c */

typedef enum {
	MLDR_PSFA_OPEN = 0,
	MLDR_PSFA_CLOSE = 1,
	MLDR_PSFA_DUP2 = 2,
	MLDR_PSFA_INHERIT = 3,
	MLDR_PSFA_FILEPORT_DUP2 = 4, /* recognized, rejected — Mach fileports
	                              * don't exist at this raw-syscall layer. */
	MLDR_PSFA_CHDIR = 5,
	MLDR_PSFA_FCHDIR = 6,
} mldr_psfa_t;

struct mldr_psfa_action {
	int32_t psfaa_type; /* mldr_psfa_t */
	union {
		int32_t psfaa_filedes;
		uint32_t psfaa_fileport;
	};
	union {
		struct {
			int32_t  psfao_oflag;
			uint16_t psfao_mode;
			char     psfao_path[PATH_MAX];
		} psfaa_openargs;
		struct {
			int32_t psfad_newfiledes;
		} psfaa_dup2args;
		struct {
			char psfac_path[PATH_MAX];
		} psfaa_chdirargs;
	};
};

struct mldr_posix_spawn_file_actions {
	int32_t psfa_act_alloc;
	int32_t psfa_act_count;
	struct mldr_psfa_action psfa_act_acts[]; /* flexible array, C99 */
};

struct mldr_posix_spawn_args_desc {
	size_t                                attr_size;
	struct mldr_posix_spawnattr          *attrp;
	size_t                                file_actions_size;
	struct mldr_posix_spawn_file_actions *file_actions;
	size_t                                port_actions_size;
	void                                  *port_actions;
	size_t                                mac_extensions_size;
	void                                  *mac_extensions;
	size_t                                coal_info_size;
	void                                  *coal_info;
	size_t                                persona_info_size;
	void                                  *persona_info;
	size_t                                posix_cred_info_size;
	void                                  *posix_cred_info;
	size_t                                subsystem_root_path_size;
	char                                  *subsystem_root_path;
};

/*
 * purpose:  Implement macOS MACOS_SYS_fork (raw fork(2) BSD syscall, nr 2).
 * input:    (none)
 * output:   In the parent: child's pid (>=0) or -errno. In the child: 0.
 * sideEffects:
 *   - Calls FreeBSD fork(2) directly (freebsd_raw_syscall(SYS_fork, ...)).
 *   - In the child ONLY: closes the inherited (now-shared-with-parent, and
 *     therefore unsafe to keep using — see SPEC doc "why raw fork() is not
 *     enough") darlingserver RPC socket and process-lifetime pipe, opens
 *     fresh ones, and performs `dserver_rpc_checkin(true, ...)` so
 *     darlingserver registers the new process. If any of this fails, the
 *     child aborts (there is no way to report the failure through the
 *     macOS fork() ABI once the kernel fork() itself has already
 *     succeeded, and continuing without a working checkin leaves an
 *     unmonitored, protocol-invisible process behind — the same failure
 *     mode Linux Darling's sys_fork() treats as unrecoverable).
 *   - Does NOT touch any thread but the calling one: like real fork(2),
 *     only the calling thread survives into the child. Forking a
 *     multi-threaded macOS process through this path leaves the child with
 *     exactly one thread (POSIX behavior), and this implementation makes NO
 *     attempt to fix up any other guest thread's per-thread RPC socket —
 *     there is nothing left in the child to fix up, since those threads no
 *     longer exist. If a *different* (not the forking) guest thread later
 *     tries to reuse its old per-thread RPC socket number believing it is
 *     still valid, that is a guest-side bug already present before this
 *     change, not one introduced by it.
 */
long mldr_sys_fork(void);

/*
 * purpose:  Implement macOS MACOS_SYS_vfork (nr 66).
 * input:    (none)
 * output:   Same convention as mldr_sys_fork().
 * sideEffects: Identical to mldr_sys_fork() — this is fork(2), not a true
 *   vfork(2). See the SPEC doc for why: true vfork() shares the parent's
 *   address space (and therefore its live SIGSYS altstack/mcontext) until
 *   the child calls _exit()/execve(), which is incompatible with running
 *   the checkin dance (new sockets, RPC calls) in the "child" without also
 *   corrupting the suspended parent's state. POSIX explicitly permits a
 *   conforming vfork() to behave exactly like fork() — the only user-
 *   visible difference is supposed to be performance, not semantics, as
 *   long as the caller only calls _exit()/execve() in the vfork "child"
 *   (which is the only legal use of vfork() to begin with). Callers doing
 *   something POSIX doesn't allow in a vfork child were already relying on
 *   undefined behavior on real macOS too.
 */
long mldr_sys_vfork(void);

/*
 * purpose:  Implement macOS MACOS_SYS_execve (nr 59): replace the calling
 *           process's image in place.
 * input:    macos_path — path as the guest passed it (already vchroot-
 *             relative from the guest's point of view, i.e. a macOS-style
 *             absolute or relative path, NOT yet prefixed with mldr's
 *             overlay root).
 *           argv, envp — NULL-terminated, passed through to the new image
 *             mostly as-is (see sideEffects for the one exception).
 * output:   On success, does not return (the calling thread's image is
 *           gone). On failure, returns -errno and the caller's image is
 *           unchanged (this call is executed synchronously in-thread,
 *           unlike mldr_sys_fork(); there is no separate "child" to clean
 *           up on failure).
 * sideEffects:
 *   - Resolves the on-disk path via mldr's existing overlay root
 *     (mldr_load_results.root_path, the same prefix loader.c uses for
 *     LC_LOAD_DYLINKER — see process_spawn.c for why no vchroot_expand()-
 *     equivalent exists yet for arbitrary exec targets, only this simple
 *     prefix).
 *   - If the target is a Mach-O (or a `#!` script, or has no recognizable
 *     magic at all — treated the same as upstream's is_script heuristic),
 *     it is NOT exec'd directly: mldr re-execs *itself* against the target,
 *     exactly like the existing argv[0]="mldr!path" convention already
 *     parsed at the top of mldr.c's main() (see mldr.c:148-166). This is
 *     required for darlingserver to keep recognizing the process at all —
 *     see the SPEC doc.
 *   - Sends `dserver_rpc_checkout(read_end_of_a_cloexec_pipe,
 *     executing_macho)` to darlingserver BEFORE the real execve(2), and
 *     writes a failure byte to the pipe's write end (then closes it) if
 *     the underlying execve(2) fails — this is how darlingserver
 *     distinguishes "execve succeeded" (all FD_CLOEXEC descriptors,
 *     including the write end, close on their own) from "execve failed"
 *     (the write end is still open and a byte arrives). Mirrors
 *     execve.c's `dserver_execve_pipe` handling on Linux exactly (see
 *     xnu_syscall/bsd/impl/process/execve.c) because darlingserver's
 *     server-side Checkout handler (call.cpp:331-388) is OS-agnostic and
 *     expects exactly this protocol.
 */
long mldr_sys_execve(const char *macos_path, char *const argv[], char *const envp[]);

/*
 * purpose:  Implement macOS MACOS_SYS_posix_spawn (nr 244).
 * input:    pid_out — where to store the spawned pid (parent side only; may
 *             be NULL). May be NULL only when not needed by the caller.
 *           macos_path — see mldr_sys_execve().
 *           desc — NULL, or a pointer to a struct mldr_posix_spawn_args_desc
 *             as the guest's libSystem.B.dylib built it on the stack/heap
 *             (see the struct-mirror comment above for why this layout is
 *             trusted). May have attrp and/or file_actions NULL/absent.
 *           argv, envp — as for execve.
 * output:   >= 0 (0) on success (the spawned pid is written to *pid_out,
 *           not returned — matches the real posix_spawn(2) syscall's own
 *           convention, which is different from fork()'s), or -errno.
 *           The one exception is POSIX_SPAWN_SETEXEC (desc->attrp->
 *           psa_flags & MLDR_POSIX_SPAWN_SETEXEC): like the real syscall,
 *           this replaces the CALLING process in place (no fork), same
 *           as execve(), and only returns on failure.
 * sideEffects:
 *   - Without POSIX_SPAWN_SETEXEC: forks (via the same internal logic as
 *     mldr_sys_fork(), including the darlingserver checkin dance — see
 *     mldr_sys_fork()'s sideEffects), applies the requested attributes and
 *     file actions in the child, then execve()s (via the same internal
 *     logic as mldr_sys_execve(), including the checkout dance). Spawn
 *     failures detected in the child (bad file action, attribute this
 *     implementation refuses — see below, or the execve itself failing)
 *     are reported back to the parent over an internal CLOEXEC pipe, same
 *     mechanism as upstream's sys_posix_spawn() (see reference file
 *     xnu_syscall/bsd/impl/process/posix_spawn.c) — NOT as a return value
 *     from the forked child, which has already diverged from the parent's
 *     control flow by that point.
 *   - Applies (see process_spawn.c for exact ordering, which matters —
 *     e.g. file actions run after attribute flags, matching upstream):
 *     RESETIDS, SETPGROUP, SETSIGMASK, START_SUSPENDED (via
 *     dserver_rpc_stop_after_exec(), same as upstream), and file actions
 *     OPEN/CLOSE/DUP2/INHERIT/CHDIR/FCHDIR.
 *   - Explicitly REJECTS (-ENOTSUP, does not spawn) rather than silently
 *     ignoring: SETSIGDEF (recognized but not implemented — no per-signal
 *     default-reset syscall wired up here, and silently continuing without
 *     the requested reset could change caught-signal behavior the caller
 *     depends on), CLOEXEC_DEFAULT (recognized but not implemented —
 *     correctly enumerating and re-flagging every open fd needs a
 *     getdirentries(2) loop this implementation doesn't have, and
 *     silently skipping it would leave fds inheritable that the caller
 *     asked to have closed on exec), any file action of type
 *     MLDR_PSFA_FILEPORT_DUP2, and any of psa_ports/psa_mac_extensions/
 *     coal_info/persona_info/posix_cred_info/subsystem_root_path being
 *     non-NULL — see the struct-level comment above for why these
 *     specifically are refused instead of ignored.
 */
long mldr_sys_posix_spawn(pid_t *pid_out, const char *macos_path,
                           const struct mldr_posix_spawn_args_desc *desc,
                           char *const argv[], char *const envp[]);

#endif /* DARLING_FREEBSD */

#endif /* _MLDR_PROCESS_SPAWN_H_ */
