/*
 * launch-dynamic-smoke.c — dynamic Mach-O smoke test.
 *
 * purpose:    Verify that mldr can load a dynamically-linked Mach-O through dyld.
 * input:      None (paths resolved from env or defaults).
 * output:     "hello-dynamic\n" printed to stdout if dyld load succeeds;
 *             mldr exits non-zero on failure.
 * sideEffects: Spawns darlingserver child; cleans up on exit.
 *
 * Build: cc -o /tmp/launch-dynamic tests/launch-dynamic-smoke.c
 * Run as root on FreeBSD 15.1.
 *
 * Environment (no defaults — the paths are machine-specific, so say where they
 * are rather than guessing):
 *   DARLING_BUILD_DIR  — build output dir, e.g. /var/darling-build
 *   DARLING_OVERLAY    — darling overlay dir
 *   DARLING_SRC_DIR    — repo root
 *
 * Note on 9p/p9fs + mmap: FreeBSD's virtio-9p driver may return BUS_OBJERR
 * when page-faulting into mmap'd files on the 9p mount.  To avoid this, dyld
 * is copied from the overlay (9p) to /tmp before exec'ing mldr.  Only dyld
 * itself needs copying because it is mmap'd with PROT_EXEC; libSystem and other
 * dylibs are loaded later by dyld using the overlay root path (DYLD_ROOT_PATH),
 * and dyld opens them with read() + mmap() using the file descriptor — which
 * may also fault.  The safest approach is to shadow the critical files.
 *
 * How it differs from launch-smoke.c:
 *   1. Sets __mldr_DYLD_ROOT_PATH to the darling overlay (not PREFIX), so dyld
 *      and libSystem.B.dylib are found at their Mach-O paths.
 *   2. Runs hello-dynamic-macho (LC_LOAD_DYLINKER + LC_LOAD_DYLIB) instead of
 *      the static binary.
 *   3. Uses /var/darling-build as default build dir (matches VM deploy path).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/un.h>

#define PREFIX        "/tmp/darling-dynamic-smoke"
#define SOCK_PATH     PREFIX "/.darlingserver.sock"
#define LOCAL_OVERLAY "/tmp/darling-local-overlay"

static const char *build_dir(void) {
    const char *v = getenv("DARLING_BUILD_DIR");
    return v ? v : "/var/darling-build";
}

/* No fallback: a wrong default would send the run at some other machine's
   tree. Missing environment is reported by name at startup. */
static const char *overlay_dir(void) {
    return getenv("DARLING_OVERLAY");
}

static const char *src_dir(void) {
    return getenv("DARLING_SRC_DIR");
}

static void cleanup(void) {
    system("pkill -9 darlingserver 2>/dev/null");
    system("rm -rf " PREFIX);
    system("rm -rf " LOCAL_OVERLAY);
}

/*
 * purpose:  Copy a single file from src to dst (mkdir -p for dst parent).
 * input:    src — source path, dst — destination path
 * output:   0 on success, -1 on error (message to stderr)
 * sideEffects: creates dst and all parent directories
 */
static int copy_file(const char *src, const char *dst) {
    /* mkdir -p for the parent directory of dst */
    char parent[512];
    snprintf(parent, sizeof(parent), "%s", dst);
    char *slash = strrchr(parent, '/');
    if (slash) {
        *slash = '\0';
        /* create directory tree (simplified: only one level deep for our use) */
        char cmd[1024];
        snprintf(cmd, sizeof(cmd), "mkdir -p '%s'", parent);
        if (system(cmd) != 0) {
            fprintf(stderr, "mkdir -p %s failed\n", parent);
            return -1;
        }
    }

    int fdin  = open(src, O_RDONLY);
    if (fdin < 0) { perror(src); return -1; }

    /* get size for progress */
    struct stat st;
    if (fstat(fdin, &st) < 0) { perror("fstat"); close(fdin); return -1; }

    int fdout = open(dst, O_WRONLY|O_CREAT|O_TRUNC, 0755);
    if (fdout < 0) { perror(dst); close(fdin); return -1; }

    char buf[65536];
    ssize_t nr;
    while ((nr = read(fdin, buf, sizeof(buf))) > 0) {
        const char *p = buf;
        while (nr > 0) {
            ssize_t nw = write(fdout, p, (size_t)nr);
            if (nw < 0) { perror(dst); close(fdin); close(fdout); return -1; }
            p  += nw;
            nr -= nw;
        }
    }
    close(fdin);
    close(fdout);
    return nr < 0 ? (perror("read"), -1) : 0;
}

int main(void) {
    const char *bd = build_dir();
    const char *od = overlay_dir();
    const char *sd = src_dir();
    if (bd == NULL || od == NULL || sd == NULL) {
        fprintf(stderr, "Set DARLING_BUILD_DIR, DARLING_OVERLAY and DARLING_SRC_DIR\n");
        return 1;
    }

    if (getuid() != 0) {
        fprintf(stderr, "Must run as root\n");
        return 1;
    }

    char dserver[512], mldr[512], binary[512];

    /* DARLING_TEST_BINARY selects which generated test binary to run —
     * e.g. hello-bind-macho, which exercises real symbol binding against
     * libSystem rather than hello-dynamic-macho's raw syscalls. */
    const char *test_bin = getenv("DARLING_TEST_BINARY");
    if (!test_bin || !test_bin[0])
        test_bin = "hello-dynamic-macho";

    snprintf(dserver, sizeof(dserver), "%s/dserver/darlingserver",  bd);
    snprintf(mldr,    sizeof(mldr),    "%s/dserver/mldr-real/mldr", bd);
    snprintf(binary,  sizeof(binary),  "%s/tests/%s", sd, test_bin);

    /* Sanity-check that all three files exist */
    struct stat st;
    int missing = 0;
    if (stat(dserver, &st) < 0) { fprintf(stderr, "Missing: %s\n", dserver); missing = 1; }
    if (stat(mldr,    &st) < 0) { fprintf(stderr, "Missing: %s\n", mldr);    missing = 1; }
    if (stat(binary,  &st) < 0) { fprintf(stderr, "Missing: %s\n", binary);  missing = 1; }
    if (stat(od,      &st) < 0) { fprintf(stderr, "Missing overlay: %s\n", od); missing = 1; }
    if (missing) return 1;

    printf("darlingserver : %s\n", dserver);
    printf("mldr          : %s\n", mldr);
    printf("binary        : %s\n", binary);
    printf("overlay       : %s\n", od);

    cleanup();
    mkdir(PREFIX, 0755);

    /*
     * Copy dyld and libSystem.B.dylib from the overlay (9p/p9fs mount) to a
     * local tmpfs path.  FreeBSD's virtio-9p driver may return BUS_OBJERR on
     * page-fault reads from mmap'd 9p-backed files; local copies are safe.
     * We mirror the exact Mach-O paths so __mldr_DYLD_ROOT_PATH still works.
     */
    {
        char src[512], dst[512];
        struct stat cached;

        /* Reuse an existing local copy. Re-reading the ~80MB tree over
         * virtiofs on every run retains ~50MB of fuse_msgbuf in the guest
         * kernel each time (see the FUSE_READLINK note below — the same client
         * holds on to buffers on plain reads too, just far more slowly).
         * Set DARLING_SMOKE_REFRESH=1 to force a fresh copy. */
        if (getenv("DARLING_SMOKE_REFRESH") != NULL ||
            stat(LOCAL_OVERLAY "/usr/lib/libSystem.B.dylib", &cached) < 0) {
            system("rm -rf " LOCAL_OVERLAY);
        }

        /* Copy the whole usr/lib tree (~80MB). Copying only dyld +
         * libSystem.B.dylib + usr/lib/system/ is not enough: the transitive
         * dependency closure reaches further (libdispatch.dylib pulls in
         * libobjc.A.dylib, and so on), and every miss surfaces as an opaque
         * "image not found" from dyld. */
        (void)src; (void)dst;
        {
            char cmd[1024];
            snprintf(cmd, sizeof(cmd),
                     /* Regular files only, via find -type f (which lstat()s and
                      * never readlink()s).
                      *
                      * Do NOT use cp -a / cp -RL here: the overlay lives on the
                      * virtiofs mount, whose host server returns a malformed
                      * FUSE_READLINK reply (embedded NUL). Every symlink walked
                      * fails with EIO *and* leaks a fuse_msgbuf in the FreeBSD
                      * FUSE client — enough of them wires all of RAM and the
                      * guest dies in an unrecoverable OOM spiral. */
                     "mkdir -p '%s/usr/lib' && cd '%s/usr/lib' && "
                     "find . -type f | pax -rw '%s/usr/lib'",
                     LOCAL_OVERLAY, od, LOCAL_OVERLAY);
            /* A cached copy makes this a no-op (the tree already exists).
             * Verify by checking the two files we cannot run without, rather
             * than by the copy command's exit status. */
            (void)system(cmd);

            struct stat lst;
            if (stat(LOCAL_OVERLAY "/usr/lib/dyld", &lst) < 0) {
                fprintf(stderr, "Failed to copy dyld to local overlay\n");
                return 1;
            }
            if (stat(LOCAL_OVERLAY "/usr/lib/libSystem.B.dylib", &lst) < 0) {
                fprintf(stderr, "Failed to copy libSystem.B.dylib to local overlay\n");
                return 1;
            }
            printf("usr/lib cached locally: %s/usr/lib\n", LOCAL_OVERLAY);

            /* Frameworks (e.g. CoreFoundation.framework) — same copy
             * approach, only staged if the overlay actually has one, since
             * most test binaries don't need it. */
            char fw_src[512];
            snprintf(fw_src, sizeof(fw_src), "%s/System/Library/Frameworks", od);
            struct stat fw_st;
            if (stat(fw_src, &fw_st) == 0) {
                char fw_cmd[1024];
                snprintf(fw_cmd, sizeof(fw_cmd),
                         "mkdir -p '%s/System/Library/Frameworks' && cd '%s' && "
                         "find . -type f | pax -rw '%s/System/Library/Frameworks'",
                         LOCAL_OVERLAY, fw_src, LOCAL_OVERLAY);
                (void)system(fw_cmd);
                printf("Frameworks cached locally: %s/System/Library/Frameworks\n", LOCAL_OVERLAY);
            }

            /* etc (master.passwd/pwd.db/group) — real Foundation code calls
             * getpwuid()/getpwnam() (e.g. for NSHomeDirectory()-for-root and
             * similar lookups) which resolve through the vchroot'd overlay,
             * not the guest's own /etc. Without a passwd db there the lookup
             * just fails, but some callers don't handle that failure — worth
             * caching same as usr/lib/Frameworks above. od's "etc" is a
             * symlink (Darwin-style etc -> private/etc); go straight to the
             * real target so this doesn't need a readlink over virtiofs. */
            char etc_src[512];
            snprintf(etc_src, sizeof(etc_src), "%s/private/etc", od);
            struct stat etc_st;
            if (stat(etc_src, &etc_st) == 0) {
                char etc_cmd[1024];
                snprintf(etc_cmd, sizeof(etc_cmd),
                         "mkdir -p '%s/etc' && cd '%s' && "
                         "find . -maxdepth 1 -type f | pax -rw '%s/etc'",
                         LOCAL_OVERLAY, etc_src, LOCAL_OVERLAY);
                (void)system(etc_cmd);
                printf("etc cached locally: %s/etc\n", LOCAL_OVERLAY);
            }
        }

        /* Copy the Mach-O test binary too — it's on the same 9p mount */
        char local_binary[512];
        snprintf(local_binary, sizeof(local_binary), "%s/%s", LOCAL_OVERLAY, test_bin);
        if (copy_file(binary, local_binary) != 0) {
            fprintf(stderr, "Failed to copy %s\n", test_bin);
            return 1;
        }
        /* Redirect binary to local copy */
        snprintf(binary, sizeof(binary), "%s", local_binary);
        printf("binary cached locally: %s\n", binary);

        /*
         * Chrome staging: when running chrome-macho, stage the embedded
         * Chrome framework into $LOCAL/Frameworks (so the guest sees it at
         * /Frameworks/Google Chrome for Testing Framework.framework/...) and
         * create a /tmp/Frameworks symlink so Chrome's @loader_path/../
         * resolution works.
         *
         * Skip the staging when the test isn't chrome-macho, when CHROME_APP
         * isn't set, or when the framework is already cached. Use
         * DARLING_SMOKE_REFRESH=1 to force a re-stage.
         */
        if (strcmp(test_bin, "chrome-macho") == 0) {
            const char *chrome_app = getenv("CHROME_APP");
            if (chrome_app && chrome_app[0]) {
                char fw_src[1024], fw_dst[1024];
                snprintf(fw_src, sizeof(fw_src),
                         "%s/Contents/Frameworks/Google Chrome for Testing Framework.framework",
                         chrome_app);
                snprintf(fw_dst, sizeof(fw_dst),
                         "%s/Frameworks/Google Chrome for Testing Framework.framework",
                         LOCAL_OVERLAY);
                struct stat fw_st;
                int need_stage = (getenv("DARLING_SMOKE_REFRESH") != NULL) ||
                                  (stat(fw_dst, &fw_st) < 0);
                if (need_stage) {
                    char stage_cmd[1536];
                    /* The framework's top-level entry is itself a symlink
                     * ("Google Chrome for Testing Framework.framework" ->
                     * "Versions/Current/...").  find -type f would skip it
                     * and pax -rw would then deposit Versions/ at the
                     * FRAMEWORKS root instead of inside the framework dir —
                     * "/tmp/.../Frameworks/Versions/..." not
                     * "/tmp/.../Frameworks/Google Chrome for Testing
                     * Framework.framework/Versions/...".  dyld then can't
                     * resolve the framework at the path Chrome asks for.
                     *
                     * Fix: create fw_dst first (so pax -rw copies INTO it),
                     * not just its parent Frameworks/. */
                    snprintf(stage_cmd, sizeof(stage_cmd),
                             "cd '%s' && "
                             "mkdir -p '%s' && "
                             "find . -type f | pax -rw '%s'",
                             fw_src, fw_dst, fw_dst);
                    int r = system(stage_cmd);
                    if (r != 0) {
                        fprintf(stderr, "Chrome framework stage failed (rc=%d)\n", r);
                        return 1;
                    }
                    /* Restore the top-level symlinks that pax's -type f skip
                     * would otherwise drop. Without these, dyld cannot
                     * resolve Chrome's @loader_path/Google Chrome for Testing
                     * Framework which depends on the framework's root symlink.
                     *
                     * Order matters: Versions/Current must be created FIRST,
                     * because the framework root symlink points to
                     * Versions/Current/<file>.
                     *
                     * Each symlink is created via a separate system() call —
                     * a single sh -c "&&"-chained command silently returns
                     * the first failure's exit code but, more importantly,
                     * here we have observed `ln` exit 0 yet no symlink
                     * appearing, suggesting a parse-quirk with the &&-chain
                     * inside /bin/sh under load-dynamic's system(). */
                    char link_cmd[1024];
                    int lr;
                    snprintf(link_cmd, sizeof(link_cmd),
                             "ln -sf '154.0.8029.0' '%s/Versions/Current'",
                             fw_dst);
                    lr = system(link_cmd);
                    if (lr != 0) fprintf(stderr, "  link Versions/Current rc=%d\n", lr);
                    snprintf(link_cmd, sizeof(link_cmd),
                             "ln -sf 'Versions/Current/Google Chrome for Testing Framework' "
                             "'%s/Google Chrome for Testing Framework'",
                             fw_dst);
                    lr = system(link_cmd);
                    if (lr != 0) fprintf(stderr, "  link framework root rc=%d\n", lr);
                    snprintf(link_cmd, sizeof(link_cmd),
                             "ln -sf 'Versions/Current/Helpers' '%s/Helpers'",
                             fw_dst);
                    lr = system(link_cmd);
                    snprintf(link_cmd, sizeof(link_cmd),
                             "ln -sf 'Versions/Current/Libraries' '%s/Libraries'",
                             fw_dst);
                    lr = system(link_cmd);
                    snprintf(link_cmd, sizeof(link_cmd),
                             "ln -sf 'Versions/Current/Resources' '%s/Resources'",
                             fw_dst);
                    lr = system(link_cmd);
                    printf("Chrome framework staged: %s\n", fw_dst);
                } else {
                    printf("Chrome framework already staged (DARLING_SMOKE_REFRESH=1 to re-stage)\n");
                }

                /* A stale /tmp/Frameworks directory (left over from an old
                 * hand-staging) shadows the symlink we're about to create
                 * and gets in the way of Chrome's "@loader_path/../Frameworks"
                 * lexical resolution. Nuke any such directory before linking. */
                (void)system("rm -rf /tmp/Frameworks");
                /* Symlink /tmp/Frameworks -> ../Frameworks. Chrome's
                 * @loader_path resolution: "<chrome-exe>/../Frameworks" =
                 * "<chrome-exe-dir>/../Frameworks". With the symlink in
                 * place, that path lexically resolves to the staged tree. */
                if (symlink("../Frameworks", "/tmp/Frameworks") < 0 && errno != EEXIST) {
                    perror("symlink /tmp/Frameworks");
                    /* non-fatal: Chrome may still find it via guest /Frameworks */
                }
            } else {
                printf("CHROME_APP not set — chrome-macho will fail to find framework\n");
            }
        }

        /* update od to point to the local copy */
        od = LOCAL_OVERLAY;
    }

    /*
     * Pipe handshake: pass write-end fd to darlingserver as argv[4].
     * darlingserver writes "." when its child (the real server process) is
     * ready to accept connections.
     */
    int pipefd[2];
    if (pipe(pipefd) < 0) { perror("pipe"); return 1; }

    pid_t dserver_pid = fork();
    if (dserver_pid < 0) { perror("fork"); return 1; }

    if (dserver_pid == 0) {
        /* Child: exec darlingserver */
        close(pipefd[0]);

        /*
         * darlingserver hands this path to dyld via the vchroot_path RPC; dyld
         * then rewrites *every* macOS path it resolves as <vchroot> + <path>.
         * Without it darlingserver falls back to the installed overlay
         * (/usr/local/darling-overlay), which does not exist in this harness,
         * so /usr/lib/libSystem.B.dylib would never be found.
         */
        setenv("DARLING_VCHROOT_PATH", od, 1);

        char pipestr[16], uidstr[16], gidstr[16];
        snprintf(pipestr, sizeof(pipestr), "%d", pipefd[1]);
        snprintf(uidstr,  sizeof(uidstr),  "%d", (int)getuid());
        snprintf(gidstr,  sizeof(gidstr),  "%d", (int)getgid());

        /* argv: darlingserver <prefix> <uid> <gid> <pipe_fd> <fix_perms> */
        execl(dserver, "darlingserver",
              PREFIX, uidstr, gidstr, pipestr, "0",
              (char*)NULL);
        perror("execl darlingserver");
        _exit(1);
    }

    /* Parent: wait for darlingserver to signal ready */
    close(pipefd[1]);
    char buf[2];
    ssize_t n = read(pipefd[0], buf, 1);
    close(pipefd[0]);
    if (n <= 0) {
        fprintf(stderr, "darlingserver did not signal readiness (n=%zd)\n", n);
        cleanup();
        return 1;
    }
    printf("darlingserver signaled ready\n");

    /* Poll for UNIX socket to appear (darlingserver forks a child that binds it) */
    int waited = 0;
    while (stat(SOCK_PATH, &st) < 0 || !S_ISSOCK(st.st_mode)) {
        if (waited++ > 50) {
            fprintf(stderr, "Socket %s did not appear after 5s\n", SOCK_PATH);
            cleanup();
            return 1;
        }
        usleep(100000); /* 100 ms */
    }
    printf("darlingserver socket ready: %s\n", SOCK_PATH);

    /*
     * Set up mldr environment:
     *   __mldr_sockpath       — where mldr finds darlingserver
     *   __mldr_DYLD_ROOT_PATH — mldr prepends this to LC_LOAD_DYLINKER path
     *                           ("/usr/lib/dyld") → overlay/usr/lib/dyld
     *                           mldr also rewrites this to DYLD_ROOT_PATH so
     *                           dyld itself resolves libSystem.B.dylib correctly.
     */
    setenv("__mldr_sockpath",       SOCK_PATH, 1);
    setenv("__mldr_DYLD_ROOT_PATH", od, 1);

    /* Chrome / dlopen-probe: inject darling-extras.dylib so the ~969 framework
     * symbols Chrome references but Darling's (stub/partial) frameworks don't
     * export resolve at bind time. Real implementations are not shadowed
     * because the extras dylib only defines symbols the source framework lacks. */
    if (strcmp(test_bin, "chrome-macho") == 0 ||
        strcmp(test_bin, "dlopen-probe-macho") == 0 ||
        strcmp(test_bin, "chrome-dlopen-probe-macho") == 0) {
        setenv("DYLD_INSERT_LIBRARIES", "/usr/lib/darling-extras.dylib", 1);
        /* dyld-trace: separate add-image probe. Prepends to the existing
         * DYLD_INSERT_LIBRARIES so the constructor runs first and the
         * per-image trace is visible alongside Chrome's load activity. */
        const char *cur = getenv("DYLD_INSERT_LIBRARIES");
        char combined[1024];
        snprintf(combined, sizeof(combined), "/usr/lib/dyld-trace.dylib%s%s",
                 cur ? ":" : "", cur ? cur : "");
        setenv("DYLD_INSERT_LIBRARIES", combined, 1);
    }

    /* Chrome: force dyld to print load/search activity so we can see which
     * dependent dylib dlopen fails to resolve ("image not found"). */
    if (strcmp(test_bin, "chrome-macho") == 0 ||
        strcmp(test_bin, "dlopen-probe-macho") == 0 ||
        strcmp(test_bin, "chrome-dlopen-probe-macho") == 0) {
        setenv("DYLD_PRINT_LIBRARIES", "1", 1);
        setenv("DYLD_PRINT_FILES", "1", 1);
        setenv("DYLD_PRINT_SEARCHING", "1", 1);
        setenv("DYLD_PRINT_RPATHS", "1", 1);
    }

    printf("Running: %s %s\n", mldr, binary);
    fflush(stdout);

    /*
     * Put mldr in its own process group before exec.
     *
     * When dyld halts it calls abort_with_payload(), whose Darling
     * implementation ends in kill(0, SIGABRT) — pid 0 meaning *the entire
     * process group*. Without this, that blast also takes out darlingserver
     * (forked above, so same group), the invoking shell, and any truss
     * attached — which drops dyld's diagnostics before they reach the log and
     * yields nonsense exit codes, making one deterministic failure look like a
     * flaky one.
     */
    if (setpgid(0, 0) < 0)
        perror("setpgid");

    /* DARLING_TEST_ARGS — space-separated argv[1..] for the TARGET binary
     * (not mldr itself). mldr forwards everything past its own argv[1]
     * (the target path) straight through as the target's argv — see
     * mldr.c's "adjust argv (remove mldr's argv[0])" comment. */
    char *extra_argv[32];
    int extra_argc = 0;
    char args_buf[1024];
    const char *test_args = getenv("DARLING_TEST_ARGS");
    if (test_args && test_args[0]) {
        snprintf(args_buf, sizeof(args_buf), "%s", test_args);
        char *saveptr = NULL;
        for (char *tok = strtok_r(args_buf, " ", &saveptr);
             tok != NULL && extra_argc < 30;
             tok = strtok_r(NULL, " ", &saveptr)) {
            extra_argv[extra_argc++] = tok;
        }
    }

    char *mldr_argv[2 + 32 + 1];
    int i = 0;
    mldr_argv[i++] = mldr;
    mldr_argv[i++] = binary;
    for (int j = 0; j < extra_argc; j++)
        mldr_argv[i++] = extra_argv[j];
    mldr_argv[i] = NULL;

    execv(mldr, mldr_argv);
    perror("execv mldr");
    cleanup();
    return 1;
}
