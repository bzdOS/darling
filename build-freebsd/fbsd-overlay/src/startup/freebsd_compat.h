/*
 * freebsd_compat.h — FreeBSD shims for Linux-specific interfaces used by
 * Darling's startup/ layer.
 *
 * purpose: Provide drop-in replacements for Linux syscalls and /proc paths so
 *          that darling.c and mldr.c compile and run on FreeBSD 14+ without
 *          modification to the shared source.
 * input:   Included unconditionally in startup/darling.c when DARLING_FREEBSD
 *          is defined.  The including translation unit must already have pulled
 *          in <sys/types.h>, <unistd.h>, <errno.h>, <string.h>.
 * output:  Macros and inline functions that map Linux APIs to FreeBSD ones.
 * sideEffects: Pulls in <sys/jail.h>, <sys/param.h>, <sys/sysctl.h>,
 *              <sys/procctl.h>, <sys/user.h>, <libutil.h>.
 *
 * Covered symbols:
 *   unshare()          → jail_create() + nullfs bind (see freebsd_jail.c)
 *   setns()            → jail_attach()
 *   CLONE_NEWNS        → BSDOS_CLONE_NEWNS  (opaque flag, value matches Linux)
 *   CLONE_NEWUTS       → BSDOS_CLONE_NEWUTS
 *   CLONE_NEWIPC       → BSDOS_CLONE_NEWIPC
 *   /proc/self/exe     → /proc/curproc/file  (FreeBSD procfs)
 *   /proc/<pid>/comm   → sysctl kern.proc.name
 *   /proc/<pid>/status → sysctl kern.proc.proc (struct kinfo_proc)
 *   /proc/<pid>/task/<pid>/children → sysctl kern.proc.pgrp
 *   /proc/<pid>/ns/mnt → synthetic path backed by jail id
 *   <sched.h> CLONE_*  → defined below
 *   <pty.h>            → <libutil.h>
 *   <alloca.h>         → <stdlib.h>
 */

#ifndef DARLING_FREEBSD_COMPAT_H
#define DARLING_FREEBSD_COMPAT_H

#ifdef DARLING_FREEBSD

/* ── System headers available on FreeBSD ─────────────────────────────────── */
#include <sys/param.h>
#include <sys/types.h>
#include <sys/sysctl.h>
#include <sys/user.h>        /* struct kinfo_proc */
#include <sys/jail.h>        /* jail_attach(), struct jailparam */
#include <sys/procctl.h>     /* procctl() for signal relay */
#include <sys/wait.h>
#include <sys/filio.h>       /* FIONREAD ioctl constant */
#include <sys/ioctl.h>
#include <stdlib.h>          /* alloca on FreeBSD lives here */
#include <libutil.h>         /* openpty(), forkpty() on FreeBSD */
#include <string.h>
#include <errno.h>
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>

/* environ is declared in unistd.h on FreeBSD but hidden under some feature
 * macros.  Declare it explicitly so darling.c can iterate over it. */
#ifndef _ENVIRON_DECLARED
#define _ENVIRON_DECLARED
extern char **environ;
#endif

/* ── <alloca.h> redirect ─────────────────────────────────────────────────── */
/* Linux darling.c includes <alloca.h>.  On FreeBSD alloca() is in <stdlib.h>
 * which we include above.  Define a guard so the non-existent header is never
 * opened again. */
#ifndef _ALLOCA_H
#define _ALLOCA_H 1
#endif

/* ── <pty.h> redirect ────────────────────────────────────────────────────── */
/* Linux darling.c includes <pty.h> for openpty()/forkpty().  On FreeBSD those
 * functions are in <libutil.h> which we included above. */
#ifndef _PTY_H
#define _PTY_H 1
#endif

/* ── CLONE_* flags ───────────────────────────────────────────────────────── */
/* Preserve the Linux numeric values so that any integer comparisons embedded
 * in shared code still work correctly; these are treated as opaque by
 * freebsd_jail.c which maps them to jail parameters. */
#ifndef CLONE_NEWNS
#define CLONE_NEWNS   0x00020000  /* mount namespace  */
#endif
#ifndef CLONE_NEWUTS
#define CLONE_NEWUTS  0x04000000  /* UTS (hostname) namespace */
#endif
#ifndef CLONE_NEWIPC
#define CLONE_NEWIPC  0x08000000  /* IPC namespace */
#endif
#ifndef CLONE_NEWPID
#define CLONE_NEWPID  0x20000000  /* PID namespace */
#endif
#ifndef CLONE_NEWUSER
#define CLONE_NEWUSER 0x10000000  /* user namespace */
#endif

/* ── unshare() ───────────────────────────────────────────────────────────── */
/*
 * purpose: Redirect Linux unshare() to the FreeBSD jail-based namespace
 *          implementation in freebsd_jail.c.
 * input:   flags — bitmask of CLONE_NEW* constants.
 * output:  0 on success, -1 with errno set on failure.
 * sideEffects: May create a new jail for the calling process.
 */
int bsdos_unshare(int flags);

static inline int unshare(int flags)
{
    return bsdos_unshare(flags);
}

/* ── setns() ─────────────────────────────────────────────────────────────── */
/*
 * purpose: Redirect Linux setns() to FreeBSD jail_attach().
 *          The fd is expected to point to a synthetic /proc/<pid>/ns/mnt file
 *          whose content is the ASCII jail id written by joinNamespace().
 * input:   fd   — file descriptor opened on /proc/<pid>/ns/<type>
 *          type — CLONE_NEW* flag (used to select the right jail parameter)
 * output:  0 on success, -1 with errno set on failure.
 * sideEffects: Attaches the calling process to a FreeBSD jail.
 */
int bsdos_setns(int fd, int type);

static inline int setns(int fd, int type)
{
    return bsdos_setns(fd, type);
}

/* ── pivot_root() ────────────────────────────────────────────────────────── */
/*
 * purpose: Stub for Linux pivot_root().  Darling's startup code does not call
 *          this directly but some included headers may prototype it.
 *          On FreeBSD the equivalent is chroot(2) inside a jail; the full
 *          implementation lives in freebsd_jail.c.
 * input:   new_root — path to become new /
 *          put_old  — path under new_root where old / is moved
 * output:  0 on success, -1 with errno set.
 * sideEffects: Changes the visible filesystem root for the calling process.
 */
int bsdos_pivot_root(const char *new_root, const char *put_old);

static inline int pivot_root(const char *new_root, const char *put_old)
{
    return bsdos_pivot_root(new_root, put_old);
}

/* ── /proc/self/exe → /proc/curproc/file ────────────────────────────────── */
/*
 * FreeBSD's procfs exposes the current executable path at
 * /proc/curproc/file (a symlink to the binary), not /proc/self/exe.
 * Replace the literal path constant at call sites.
 *
 * Usage in darling.c / mldr.c:
 *   readlink(PROC_SELF_EXE, buf, sz)
 */
#define PROC_SELF_EXE "/proc/curproc/file"

/* ── /proc/<pid>/comm → sysctl kern.proc.name.<pid> ─────────────────────── */
/*
 * purpose: Read the command name of a process on FreeBSD without opening
 *          /proc/<pid>/comm (which may not exist or may require procfs).
 *          Uses sysctl kern.proc.proc to get struct kinfo_proc.
 * input:   pid  — target process id
 *          buf  — output buffer (at least COMMLEN+1 bytes)
 *          bufsz— size of buf
 * output:  0 on success, -1 on failure.
 * sideEffects: none.
 */
static inline int bsdos_read_proc_comm(pid_t pid, char *buf, size_t bufsz)
{
    int mib[4];
    struct kinfo_proc kp;
    size_t len = sizeof(kp);

    mib[0] = CTL_KERN;
    mib[1] = KERN_PROC;
    mib[2] = KERN_PROC_PID;
    mib[3] = (int)pid;

    if (sysctl(mib, 4, &kp, &len, NULL, 0) == -1)
        return -1;

    /* ki_comm is the basename of the executable (COMMLEN=19 chars max) */
    snprintf(buf, bufsz, "%s", kp.ki_comm);
    return 0;
}

/*
 * purpose: Emulate fopen("/proc/<pid>/comm", "r") for Darling's getInitProcess().
 *          Writes the comm string into *out_buf and returns a synthetic FILE*
 *          backed by a memstream so that existing fscanf("%ms") code works.
 * input:   pid    — target pid
 *          out_buf— caller-allocated buffer (MAXCOMLEN+2 bytes)
 *          bufsz  — size of out_buf
 * output:  FILE* on success, NULL on failure (errno set).
 * sideEffects: Caller must fclose() the returned FILE*.
 */
static inline FILE *bsdos_fopen_proc_comm(pid_t pid, char *out_buf, size_t bufsz)
{
    if (bsdos_read_proc_comm(pid, out_buf, bufsz) == -1)
        return NULL;
    /* Append newline to match Linux /proc/comm format */
    size_t len = strlen(out_buf);
    if (len + 2 < bufsz) {
        out_buf[len]   = '\n';
        out_buf[len+1] = '\0';
    }
    return fmemopen(out_buf, strlen(out_buf), "r");
}

/* ── /proc/<pid>/status → kinfo_proc ────────────────────────────────────── */
/*
 * purpose: Emulate the Uid:/Gid: lines from Linux /proc/<pid>/status using
 *          FreeBSD's struct kinfo_proc.  The caller iterates lines with
 *          getline() and sscanf(line, "Uid: %d %d %d %d", ...).
 * input:   pid    — target pid
 *          out_buf— caller-allocated buffer (256+ bytes)
 *          bufsz  — size of out_buf
 * output:  FILE* on success (caller fclose()), NULL on failure.
 * sideEffects: none.
 */
static inline FILE *bsdos_fopen_proc_status(pid_t pid, char *out_buf, size_t bufsz)
{
    int mib[4];
    struct kinfo_proc kp;
    size_t len = sizeof(kp);

    mib[0] = CTL_KERN;
    mib[1] = KERN_PROC;
    mib[2] = KERN_PROC_PID;
    mib[3] = (int)pid;

    if (sysctl(mib, 4, &kp, &len, NULL, 0) == -1)
        return NULL;

    /*
     * Synthesise the two lines that darling.c actually parses:
     *   Uid: ruid euid suid fuid
     *   Gid: rgid egid sgid fgid
     * FreeBSD kinfo_proc has ki_ruid, ki_uid (effective), ki_svuid, ki_uid.
     */
    int written = snprintf(out_buf, bufsz,
        "Uid:\t%d\t%d\t%d\t%d\n"
        "Gid:\t%d\t%d\t%d\t%d\n",
        (int)kp.ki_ruid, (int)kp.ki_uid, (int)kp.ki_svuid, (int)kp.ki_uid,
        (int)kp.ki_rgid, (int)kp.ki_groups[0], (int)kp.ki_svgid, (int)kp.ki_groups[0]);

    if (written <= 0 || (size_t)written >= bufsz) {
        errno = ENOBUFS;
        return NULL;
    }
    return fmemopen(out_buf, (size_t)written, "r");
}

/* ── /proc/<pid>/task/<pid>/children ─────────────────────────────────────── */
/*
 * purpose: Emulate /proc/<pid>/task/<pid>/children for darling.c shutdown path.
 *          Returns the first child PID of <pid> (its launchd).
 * input:   ppid   — parent pid
 *          child  — output: first child pid, 0 if none
 * output:  0 on success, -1 on failure.
 * sideEffects: none.
 */
static inline int bsdos_first_child_pid(pid_t ppid, pid_t *child)
{
    int mib[4];
    struct kinfo_proc *procs = NULL;
    size_t len = 0;

    *child = 0;

    mib[0] = CTL_KERN;
    mib[1] = KERN_PROC;
    mib[2] = KERN_PROC_PROC;   /* all processes */
    mib[3] = 0;

    /* First call: get required buffer size */
    if (sysctl(mib, 4, NULL, &len, NULL, 0) == -1)
        return -1;

    /* Over-allocate to handle TOCTOU race */
    len += len / 4;
    procs = (struct kinfo_proc *)malloc(len);
    if (!procs)
        return -1;

    if (sysctl(mib, 4, procs, &len, NULL, 0) == -1) {
        free(procs);
        return -1;
    }

    size_t count = len / sizeof(struct kinfo_proc);
    for (size_t i = 0; i < count; i++) {
        if (procs[i].ki_ppid == ppid) {
            *child = procs[i].ki_pid;
            break;
        }
    }

    free(procs);
    return 0;
}

/* ── /proc/<pid>/ns/<type> synthetic file ────────────────────────────────── */
/*
 * purpose: Create a synthetic namespace file at path that encodes the current
 *          process's jail ID so that joinNamespace() → setns() can later
 *          recover it.  The file contains the decimal jail ID as ASCII.
 * input:   path  — destination path, e.g. /tmp/darling-ns-<pid>-mnt
 *          jid   — the jail ID to encode (0 = host)
 * output:  0 on success, -1 on failure.
 * sideEffects: Creates a file at path.
 */
static inline int bsdos_write_ns_file(const char *path, int jid)
{
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (fd < 0)
        return -1;
    char buf[32];
    int n = snprintf(buf, sizeof(buf), "%d\n", jid);
    if (write(fd, buf, (size_t)n) != n) {
        close(fd);
        return -1;
    }
    close(fd);
    return 0;
}

/*
 * purpose: Translate a Linux /proc/<pid>/ns/<type> path into the synthetic
 *          path written by bsdos_write_ns_file().
 * input:   linux_path — e.g. "/proc/123/ns/mnt"
 *          out        — output buffer
 *          outsz      — size of output buffer
 * output:  out filled with translated path; 0 on success, -1 on overflow.
 * sideEffects: none.
 */
static inline int bsdos_translate_ns_path(const char *linux_path,
                                           char *out, size_t outsz)
{
    /* Expected form: /proc/<pid>/ns/<type> */
    pid_t pid = 0;
    char type_buf[32] = {0};
    if (sscanf(linux_path, "/proc/%d/ns/%31s", &pid, type_buf) != 2) {
        /* Not a namespace path — pass through unchanged */
        if (strlen(linux_path) + 1 > outsz) {
            errno = ENOBUFS;
            return -1;
        }
        memcpy(out, linux_path, strlen(linux_path) + 1);
        return 0;
    }
    int n = snprintf(out, outsz, "/tmp/darling-ns-%d-%s", (int)pid, type_buf);
    if (n <= 0 || (size_t)n >= outsz) {
        errno = ENOBUFS;
        return -1;
    }
    return 0;
}

/* ── readlink /proc/self/exe helper ──────────────────────────────────────── */
/*
 * purpose: Wrapper around readlink() that transparently rewrites
 *          "/proc/self/exe" to "/proc/curproc/file" before the syscall,
 *          and also handles the common pattern of reading mldr's own path.
 * input:   path  — original path (may be /proc/self/exe or any other path)
 *          buf   — output buffer
 *          bufsz — size of buf
 * output:  number of bytes placed in buf (no NUL), or -1 on error.
 * sideEffects: none.
 */
static inline ssize_t bsdos_readlink_exe(const char *path,
                                          char *buf, size_t bufsz)
{
    const char *actual = path;
    if (strcmp(path, "/proc/self/exe") == 0)
        actual = PROC_SELF_EXE;
    return readlink(actual, buf, bufsz);
}

/* Redirect the literal readlink("/proc/self/exe", ...) calls.
 * We cannot override readlink() directly (it would recurse), so we define a
 * macro that only rewrites the specific pattern used by Darling sources. */
#define readlink(path, buf, sz) \
    (strcmp((path), "/proc/self/exe") == 0 \
        ? bsdos_readlink_exe(PROC_SELF_EXE, (buf), (sz)) \
        : readlink((path), (buf), (sz)))

#endif /* DARLING_FREEBSD */
#endif /* DARLING_FREEBSD_COMPAT_H */
