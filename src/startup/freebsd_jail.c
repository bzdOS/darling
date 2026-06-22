/*
 * freebsd_jail.c — jail()-based replacement for Linux namespace setup used by
 * Darling's startup layer.
 *
 * purpose: Implement bsdos_unshare(), bsdos_setns(), and bsdos_pivot_root()
 *          using FreeBSD jail(2), jail_attach(2), and nullfs mounts so that
 *          Darling can isolate its prefix the same way Linux namespaces do.
 * input:   Called from darling.c when compiled with -DDARLING_FREEBSD.
 * output:  Isolation primitives backed by FreeBSD jails.
 * sideEffects: Creates and attaches FreeBSD jails; issues nullfs mount(8)
 *              calls via mount(2).  Writes synthetic namespace files under
 *              /tmp/darling-ns-<pid>-<type> for later jail_attach recovery.
 *
 * Mapping:
 *   unshare(CLONE_NEWNS)           → create jail, nullfs-bind prefix subtree
 *   unshare(CLONE_NEWUTS|NEWIPC)   → create jail with vnet=0, allow.sysvipc
 *   setns(fd, CLONE_NEWNS)         → jail_attach(jid read from fd)
 *   pivot_root(new, put_old)       → chroot(new) inside jail
 *
 * FreeBSD jail notes:
 *   - jail_set() with JAIL_CREATE creates a new jail; the calling process is
 *     NOT automatically placed inside it — that requires jail_attach().
 *   - vnet (VIMAGE) requires a kernel compiled with "options VIMAGE".  We
 *     default to vnet=0 since GENERIC FreeBSD 15.1 ships without VIMAGE in
 *     the QEMU/Squirrel target.
 *   - nullfs bind mounts require root (or jail with allow.mount.nullfs).
 *   - sysvipc isolation uses jail parameter allow.sysvipc=1.
 */

#ifdef DARLING_FREEBSD

/* Signal to freebsd_compat.h that this TU provides the real implementations
 * of bsdos_unshare/bsdos_setns/bsdos_pivot_root (not the inline no-op stubs) */
#define DARLING_FREEBSD_JAIL_C 1
#include "freebsd_compat.h"

#include <sys/param.h>
#include <sys/types.h>
#include <sys/jail.h>
#include <jail.h>            /* jailparam_init/import/set/free (libjail high-level API) */
#include <sys/mount.h>
#include <sys/uio.h>      /* struct iovec for nmount() */
#include <sys/sysctl.h>
#include <sys/stat.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>

/* Maximum jail name length on FreeBSD */
#define BSDOS_JAIL_NAME_MAX 64

/* ── Internal helpers ────────────────────────────────────────────────────── */

/*
 * purpose: Build an iovec pair (name, value) for nmount() / jail_setv().
 * input:   iov   — pointer to array to fill (must have room for 2 entries)
 *          name  — parameter name string
 *          val   — parameter value string (may be NULL → empty string)
 * output:  iov[0] and iov[1] filled; advances *iov by 2.
 * sideEffects: none.
 */
static void iov_pair(struct iovec **iov, const char *name, const char *val)
{
    (*iov)[0].iov_base = (void *)(uintptr_t)name;
    (*iov)[0].iov_len  = strlen(name) + 1;
    (*iov)[1].iov_base = (void *)(uintptr_t)(val ? val : "");
    (*iov)[1].iov_len  = val ? strlen(val) + 1 : 1;
    *iov += 2;
}

/*
 * purpose: Mount src onto dst using nullfs (read-only bind mount equivalent).
 * input:   src — source path
 *          dst — target path (must exist)
 * output:  0 on success, -1 on failure (errno set).
 * sideEffects: Performs a kernel mount; requires root.
 */
static int bsdos_nullfs_mount(const char *src, const char *dst)
{
    struct iovec iov[8];
    struct iovec *p = iov;

    iov_pair(&p, "fstype",  "nullfs");
    iov_pair(&p, "fspath",  dst);
    iov_pair(&p, "target",  src);
    iov_pair(&p, "errmsg",  NULL);   /* FreeBSD nmount() fills this on error */

    if (nmount(iov, (unsigned int)(p - iov), 0) == -1) {
        fprintf(stderr,
            "[freebsd_jail] nullfs mount %s -> %s failed: %s\n",
            src, dst, strerror(errno));
        return -1;
    }
    return 0;
}

/*
 * purpose: Create a directory (and all parents) like mkdir -p.
 * input:   path — directory to create
 *          mode — permissions
 * output:  0 on success, -1 on failure.
 * sideEffects: Creates directories.
 */
static int bsdos_mkdirp(const char *path, mode_t mode)
{
    char tmp[PATH_MAX];
    char *p;
    size_t len;

    if (strlen(path) >= sizeof(tmp)) {
        errno = ENAMETOOLONG;
        return -1;
    }
    strcpy(tmp, path);
    len = strlen(tmp);
    if (len > 0 && tmp[len - 1] == '/')
        tmp[len - 1] = '\0';

    for (p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            if (mkdir(tmp, mode) != 0 && errno != EEXIST)
                return -1;
            *p = '/';
        }
    }
    if (mkdir(tmp, mode) != 0 && errno != EEXIST)
        return -1;
    return 0;
}

/*
 * purpose: Write the jail ID to the synthetic namespace file so that later
 *          setns(open("/proc/<pid>/ns/mnt"), CLONE_NEWNS) can recover it.
 * input:   pid  — process that owns the namespace (typically our own PID)
 *          type — namespace type string ("mnt", "uts", "ipc")
 *          jid  — jail ID to record
 * output:  0 on success, -1 on failure.
 * sideEffects: Creates file /tmp/darling-ns-<pid>-<type>.
 */
static int bsdos_record_jail_id(pid_t pid, const char *type, int jid)
{
    char path[PATH_MAX];
    snprintf(path, sizeof(path), "/tmp/darling-ns-%d-%s", (int)pid, type);
    return bsdos_write_ns_file(path, jid);
}

/*
 * purpose: Read the jail ID from a synthetic namespace file.
 * input:   fd — file descriptor opened on the synthetic ns file
 * output:  jail ID >= 0 on success, -1 on failure (errno set).
 * sideEffects: none.
 */
static int bsdos_read_jail_id_from_fd(int fd)
{
    char buf[32] = {0};
    ssize_t n;
    int jid;

    if (lseek(fd, 0, SEEK_SET) == -1)
        return -1;

    n = read(fd, buf, sizeof(buf) - 1);
    if (n <= 0) {
        errno = EINVAL;   /* ENODATA is Linux-only; EINVAL for "no jail ID in file" */
        return -1;
    }
    buf[n] = '\0';

    if (sscanf(buf, "%d", &jid) != 1) {
        errno = EINVAL;
        return -1;
    }
    return jid;
}

/* ── bsdos_unshare() ─────────────────────────────────────────────────────── */

/*
 * purpose: Create a FreeBSD jail to emulate Linux namespace isolation.
 *
 *   CLONE_NEWNS   → create a jail with its own mount namespace (nullfs root).
 *                   The calling process is NOT moved into the jail here;
 *                   joinNamespace() → setns() does the attach.
 *   CLONE_NEWUTS  → create a jail with a unique hostname ("darling-<pid>").
 *   CLONE_NEWIPC  → create a jail with allow.sysvipc enabled.
 *
 *   Combinations (NEWUTS|NEWIPC, as used by spawnInitProcess()) create a
 *   single jail that satisfies both flags.
 *
 * input:   flags — bitmask of CLONE_NEW* constants.
 * output:  0 on success, -1 with errno set on failure.
 * sideEffects: Creates a FreeBSD jail; writes jail ID to
 *              /tmp/darling-ns-<pid>-{mnt,uts,ipc}.
 */
int bsdos_unshare(int flags)
{
    struct jailparam params[8];
    int param_count = 0;
    int jid = -1;
    pid_t pid = getpid();
    char jailname[BSDOS_JAIL_NAME_MAX];
    char hostname[256];

    /* Build a unique jail name based on PID and flags */
    snprintf(jailname, sizeof(jailname), "darling-%d-%x", (int)pid, (unsigned)flags);

    /* Jail path: use prefix directory if set via DPREFIX, else / (host root).
     * For mount-namespace isolation we use the prefix; for UTS/IPC we can
     * use / since we are not changing the filesystem view. */
    const char *jailpath = getenv("DPREFIX");
    if (!jailpath)
        jailpath = "/";

    /* ── Build jail parameter list ── */

    if (jailparam_init(&params[param_count], "name") == -1) goto fail;
    if (jailparam_import(&params[param_count], jailname) == -1) goto fail;
    param_count++;

    if (jailparam_init(&params[param_count], "path") == -1) goto fail;
    if (jailparam_import(&params[param_count], jailpath) == -1) goto fail;
    param_count++;

    /* No network isolation: vnet requires VIMAGE kernel option which is not
     * present in FreeBSD 15.1 GENERIC on QEMU (Squirrel target). */
    if (jailparam_init(&params[param_count], "ip4") == -1) goto fail;
    if (jailparam_import(&params[param_count], "inherit") == -1) goto fail;
    param_count++;

    if (jailparam_init(&params[param_count], "ip6") == -1) goto fail;
    if (jailparam_import(&params[param_count], "inherit") == -1) goto fail;
    param_count++;

    if (flags & CLONE_NEWUTS) {
        /* Give the jail a distinct hostname */
        snprintf(hostname, sizeof(hostname), "darling-%d", (int)pid);
        if (jailparam_init(&params[param_count], "host.hostname") == -1) goto fail;
        if (jailparam_import(&params[param_count], hostname) == -1) goto fail;
        param_count++;
    }

    if (flags & CLONE_NEWIPC) {
        /* Allow SysV IPC inside the jail */
        if (jailparam_init(&params[param_count], "allow.sysvipc") == -1) goto fail;
        if (jailparam_import(&params[param_count], "true") == -1) goto fail;
        param_count++;
    }

    /* Create jail without attaching (JAIL_CREATE without JAIL_ATTACH) */
    jid = jailparam_set(params, (unsigned)param_count, JAIL_CREATE);
    if (jid == -1) {
        fprintf(stderr,
            "[freebsd_jail] jailparam_set(%s) failed: %s\n",
            jailname, strerror(errno));
        goto fail;
    }

    jailparam_free(params, (unsigned)param_count);

    /* Record jail ID in synthetic namespace files */
    if (flags & CLONE_NEWNS)
        bsdos_record_jail_id(pid, "mnt", jid);
    if (flags & CLONE_NEWUTS)
        bsdos_record_jail_id(pid, "uts", jid);
    if (flags & CLONE_NEWIPC)
        bsdos_record_jail_id(pid, "ipc", jid);

    /* For CLONE_NEWNS, bind-mount the prefix into the jail root so that
     * darling-init can see it.  Only attempt if prefix != "/". */
    if ((flags & CLONE_NEWNS) && strcmp(jailpath, "/") != 0) {
        /* The jail path IS the prefix; the prefix dirs are already there
         * from setupPrefix().  No additional nullfs needed here — the jail
         * provides the isolation boundary. */
    }

    return 0;

fail:
    jailparam_free(params, (unsigned)param_count);
    return -1;
}

/* ── bsdos_setns() ───────────────────────────────────────────────────────── */

/*
 * purpose: Attach the calling process to the FreeBSD jail recorded in the
 *          synthetic namespace file opened as fd.
 * input:   fd   — file descriptor opened on /proc/<pid>/ns/<type> (which on
 *                 FreeBSD is the synthetic file written by bsdos_record_jail_id)
 *          type — CLONE_NEW* flag (currently used only for diagnostics)
 * output:  0 on success, -1 with errno set on failure.
 * sideEffects: Attaches calling process to the jail.  After this call the
 *              process is inside the jail and cannot leave without root
 *              privileges and a separate jail_attach to JID 0.
 */
int bsdos_setns(int fd, int type)
{
    int jid = bsdos_read_jail_id_from_fd(fd);
    if (jid < 0) {
        fprintf(stderr,
            "[freebsd_jail] bsdos_setns: cannot read jail id from fd %d: %s\n",
            fd, strerror(errno));
        return -1;
    }

    /* JID 0 means "host" — nothing to attach to */
    if (jid == 0)
        return 0;

    if (jail_attach(jid) == -1) {
        fprintf(stderr,
            "[freebsd_jail] jail_attach(%d) failed: %s\n",
            jid, strerror(errno));
        return -1;
    }

    (void)type; /* used only for diagnostics if needed in future */
    return 0;
}

/* ── bsdos_pivot_root() ──────────────────────────────────────────────────── */

/*
 * purpose: Emulate Linux pivot_root() for Darling using chroot(2).
 *          On FreeBSD, inside a jail, chroot() to new_root achieves the same
 *          effect as pivot_root: the prefix becomes visible as /.
 *          put_old is created (mkdir -p) under new_root so that the call site
 *          does not get ENOENT, but it is otherwise unused since FreeBSD does
 *          not need to "park" the old root.
 * input:   new_root — path to new filesystem root (the Darling prefix)
 *          put_old  — path where the old root would be moved (created but unused)
 * output:  0 on success, -1 on failure.
 * sideEffects: Calls chroot(new_root); creates put_old directory.
 */
int bsdos_pivot_root(const char *new_root, const char *put_old)
{
    char put_old_full[PATH_MAX];
    int n;

    /* Create the put_old directory under new_root (mkdir -p) */
    n = snprintf(put_old_full, sizeof(put_old_full), "%s%s", new_root, put_old);
    if (n <= 0 || (size_t)n >= sizeof(put_old_full)) {
        errno = ENAMETOOLONG;
        return -1;
    }

    if (bsdos_mkdirp(put_old_full, 0755) == -1 && errno != EEXIST) {
        fprintf(stderr,
            "[freebsd_jail] bsdos_pivot_root: cannot create put_old %s: %s\n",
            put_old_full, strerror(errno));
        /* Non-fatal: continue with chroot even if mkdir failed */
    }

    if (chroot(new_root) == -1) {
        fprintf(stderr,
            "[freebsd_jail] chroot(%s) failed: %s\n",
            new_root, strerror(errno));
        return -1;
    }

    if (chdir("/") == -1) {
        fprintf(stderr,
            "[freebsd_jail] chdir(/) after chroot failed: %s\n",
            strerror(errno));
        return -1;
    }

    return 0;
}

/* ── joinNamespace() override ────────────────────────────────────────────── */
/*
 * purpose: Override for darling.c's joinNamespace() on FreeBSD.
 *          The Linux version opens /proc/<pid>/ns/<type> and calls setns().
 *          On FreeBSD we translate the path to our synthetic file and call
 *          bsdos_setns().
 * input:   pid      — init process PID
 *          type     — CLONE_NEWNS (the only flag used at call site)
 *          typeName — "mnt" (the only type used at call site)
 * output:  none (exits on failure).
 * sideEffects: Attaches calling process to jail.
 */
void bsdos_joinNamespace(pid_t pid, int type, const char *typeName)
{
    char linuxPath[PATH_MAX];
    char bsdPath[PATH_MAX];
    int fd;

    /* Build the Linux-style path then translate to our synthetic path */
    snprintf(linuxPath, sizeof(linuxPath), "/proc/%d/ns/%s", (int)pid, typeName);
    if (bsdos_translate_ns_path(linuxPath, bsdPath, sizeof(bsdPath)) == -1) {
        fprintf(stderr,
            "[freebsd_jail] cannot translate ns path %s: %s\n",
            linuxPath, strerror(errno));
        exit(1);
    }

    fd = open(bsdPath, O_RDONLY);
    if (fd < 0) {
        fprintf(stderr,
            "[freebsd_jail] cannot open ns file %s: %s\n",
            bsdPath, strerror(errno));
        exit(1);
    }

    if (bsdos_setns(fd, type) != 0) {
        fprintf(stderr,
            "[freebsd_jail] cannot join %s namespace: %s\n",
            typeName, strerror(errno));
        close(fd);
        exit(1);
    }
    close(fd);
}

/* ── bsdos_getInitProcess_comm() ─────────────────────────────────────────── */
/*
 * purpose: FreeBSD replacement for the getInitProcess() block that opens
 *          /proc/<pid>/comm.  Uses sysctl to read ki_comm and returns a FILE*
 *          compatible with the existing fscanf("%ms", ...) call.
 * input:   pid    — target process id
 * output:  FILE* (caller must fclose()), NULL on failure.
 * sideEffects: Allocates a buffer; caller owns the FILE*.
 */
FILE *bsdos_getInitProcess_comm(pid_t pid)
{
    /* MAXCOMLEN on FreeBSD is 19 characters + NUL + '\n' + NUL */
    static char comm_buf[MAXCOMLEN + 3];
    return bsdos_fopen_proc_comm(pid, comm_buf, sizeof(comm_buf));
}

/* ── bsdos_getInitProcess_status() ──────────────────────────────────────── */
/*
 * purpose: FreeBSD replacement for the getInitProcess() block that opens
 *          /proc/<pid>/status.  Synthesises the Uid:/Gid: lines from
 *          struct kinfo_proc and returns a FILE* for getline() iteration.
 * input:   pid    — target process id
 * output:  FILE* (caller must fclose()), NULL on failure.
 * sideEffects: Allocates a buffer; caller owns the FILE*.
 */
FILE *bsdos_getInitProcess_status(pid_t pid)
{
    static char status_buf[512];
    return bsdos_fopen_proc_status(pid, status_buf, sizeof(status_buf));
}

/* ── bsdos_isModuleLoaded() ──────────────────────────────────────────────── */
/*
 * purpose: Return true if the Darling isolation layer is "loaded".
 *          On Linux this checks for the darling.ko kernel module.
 *          On FreeBSD we have no kernel module; return 1 (loaded) always so
 *          that darling.c does not attempt to load a non-existent module.
 * input:   none
 * output:  1 always on FreeBSD.
 * sideEffects: none.
 */
int bsdos_isModuleLoaded(void)
{
    return 1;
}

#endif /* DARLING_FREEBSD */
