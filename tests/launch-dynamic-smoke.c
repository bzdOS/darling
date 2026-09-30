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
#include <dirent.h>

#define PREFIX        "/tmp/darling-dynamic-smoke"
#define SOCK_PATH     PREFIX "/.darlingserver.sock"
#define LOCAL_OVERLAY "/tmp/darling-local-overlay"

/* Fallback for the harnesses that do not derive a staging list (run-smoke, the
 * real-macho tests). It is the pre-closure list and it is knowingly short --
 * see the comment at the staging site. The probe never uses it: its preflight
 * passes DARLING_STAGING_TREES computed from the dylib closure. */
#define DEFAULT_STAGING_TREES "usr/lib:System/Library/Frameworks"

/* Stage one overlay-relative tree into the local cache.
 *
 * Regular files only, via find -type f (which lstat()s and never readlink()s).
 *
 * Do NOT use cp -a / cp -RL here: the overlay lives on the virtiofs mount,
 * whose host server returns a malformed FUSE_READLINK reply (embedded NUL).
 * Every symlink walked fails with EIO *and* leaks a fuse_msgbuf in the FreeBSD
 * FUSE client -- enough of them wires all of RAM and the guest dies in an
 * unrecoverable OOM spiral. */
static void stage_tree(const char *od, const char *rel) {
    char src[1024], dst[1024], cmd[2048];
    struct stat st;

    snprintf(src, sizeof(src), "%s/%s", od, rel);
    snprintf(dst, sizeof(dst), "%s/%s", LOCAL_OVERLAY, rel);

    if (stat(src, &st) < 0) {
        printf("staging: %s is not in the overlay, skipped\n", rel);
        return;
    }

    /* -k is load-bearing and was found the hard way.
     *
     * pax in copy mode removes destination entries the archive does not
     * contain. The archive is `find . -type f`, which by construction
     * contains no symlinks, so without -k the file pass DELETES every link
     * the link pass created — and the staged tree comes out with zero
     * symlinks in it, whichever order the two passes run in. With the old
     * order that was invisible, because the links were created afterwards;
     * with the link pass first it is total.
     *
     * Measured, not assumed: after a run with the link pass first, the
     * staged tree had 0 symlinks against 54 in the overlay, and
     * Versions/ held only C with no Current beside it. -k means "keep
     * destination entries the archive does not mention", which is what a
     * separate pass that owns those names needs. */
    snprintf(cmd, sizeof(cmd),
             "mkdir -p '%s' && cd '%s' && find . -type f | pax -k -rw '%s'",
             dst, src, dst);
    (void)system(cmd);
    printf("cached locally: %s\n", dst);
}

/* Symlinks: the manifest transfer, and why it exists.
 *
 * stage_tree's `find . -type f` emits REGULAR FILES ONLY, so the staged copy
 * of a framework tree comes out with no symlinks in it at all. Measured on
 * this overlay: 54 under System/Library/Frameworks, 0 in the staged tree.
 *
 * That is not cosmetic. AppKit.framework/Resources is a symlink to
 * Versions/Current/Resources, and CFBundle builds its resource path as
 * <bundle>/Resources/Backends (CFBundle_Resources.c,
 * _CFBundleGetResourceDirForVersion). In the guest that path does not exist
 * at all -- opendir returns ENOENT, not EIO and not an empty listing -- while
 * the same directory reached through Versions/C lists fine. One root run's
 * worth of an exception that reads like a discovery bug and is a staging bug.
 *
 * THE HAZARD THIS AVOIDS, and why the manifest is the shape it is:
 * do NOT use cp -a / cp -RL, and do not let pax walk links. The comment above
 * stage_tree records why: the overlay's virtiofs returns a malformed
 * FUSE_READLINK reply (embedded NUL), every symlink walk fails with EIO *and*
 * leaks a fuse_msgbuf in the FreeBSD FUSE client, and enough of them wire all
 * of RAM and kill the guest in an unrecoverable OOM spiral. That was a real
 * run, and the `find -type f` form is the fix for it.
 *
 * So the links are NOT handed to a recursive copier. Each one is found with
 * lstat(), read with a single readlink() into a buffer we own, and recreated
 * with a single symlink() into the staged tree. That is one readlink per link,
 * with no traversal, no recursion into a link target, and no unbounded walk
 * for a buggy server to leak on. readlink() is the call that leaks; calling it
 * exactly 54 times is not the failure mode the OOM was.
 *
 * symlink() does not require its target to exist, so the links may be created
 * in any order: AppKit.framework/Resources -> Versions/Current/Resources and
 * Versions/Current -> C resolve at opendir time, not at creation time.
 *
 * The walk uses lstat and never stats a link, so it does not follow into a
 * symlinked directory and cannot loop. Directories are created on demand with
 * mkdir, and an existing destination link is replaced rather than skipped, so
 * the transfer is idempotent across runs.
 *
 * ROLLBACK: DARLING_STAGE_SYMLINKS=0 stages regular files only, which is
 * byte-for-byte the behaviour before this function existed. If a run ever
 * reproduces the EIO/OOM, that one variable is the whole revert. */
struct stage_symlinks_stats {
    unsigned long found;
    unsigned long created;
    unsigned long failed;
    unsigned long mkdirs;
};

static int stage_symlinks_walk(const char *src, const char *dst, unsigned depth,
                               struct stage_symlinks_stats *st) {
    DIR *d = opendir(src);
    struct dirent *de;

    if (d == NULL) {
        /* A directory that will not open here is not worth aborting for: the
         * regular-file pass has already copied what it could, and a link we
         * cannot reach is a link we would not have copied correctly either. */
        return 0;
    }

    while ((de = readdir(d)) != NULL) {
        char sp[2048], dp[2048], target[1024];
        struct stat lst;

        if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0)
            continue;

        snprintf(sp, sizeof(sp), "%s/%s", src, de->d_name);
        snprintf(dp, sizeof(dp), "%s/%s", dst, de->d_name);

        /* lstat, always: a stat here would follow a link and could walk into
         * whatever it points at, including a loop. */
        if (lstat(sp, &lst) < 0)
            continue;

        if (S_ISLNK(lst.st_mode)) {
            ssize_t n = readlink(sp, target, sizeof(target) - 1);
            st->found++;
            if (n < 0) {
                st->failed++;
                printf("staging: readlink(%s) failed: %s\n", de->d_name,
                       strerror(errno));
                continue;
            }
            target[n] = '\0';
            /* mkdir -p the parent, one level at a time, by hand: no shell, no
             * recursion into anything, and the path is ours. */
            {
                char *slash = dp;
                while ((slash = strchr(slash + 1, '/')) != NULL) {
                    *slash = '\0';
                    if (mkdir(dp, 0755) == 0)
                        st->mkdirs++;
                    *slash = '/';
                }
            }
            unlink(dp); /* an existing link is replaced, not skipped */
            if (symlink(target, dp) == 0)
                st->created++;
            else {
                st->failed++;
                printf("staging: symlink(%s) failed: %s\n", de->d_name,
                       strerror(errno));
            }
            continue;
        }

        if (S_ISDIR(lst.st_mode)) {
            /* Depth cap: the source trees are shallow (a framework is a
             * handful of levels), and a cap costs one comparison and makes
             * the walk impossible to hang on a cyclic bind mount. */
            if (depth >= 12)
                continue;
            if (mkdir(dp, 0755) < 0 && errno != EEXIST)
                continue;
            stage_symlinks_walk(sp, dp, depth + 1, st);
        }
    }

    closedir(d);
    return 0;
}

static void stage_symlinks(const char *od, const char *rel) {
    char src[1024], dst[1024];
    struct stat st;
    struct stage_symlinks_stats stats = {0, 0, 0, 0};
    const char *flag = getenv("DARLING_STAGE_SYMLINKS");
    int enabled = (flag == NULL || strcmp(flag, "0") != 0);

    if (!enabled) {
        printf("staging: symlinks OFF (DARLING_STAGE_SYMLINKS=0) -- regular"
               " files only, the behaviour before the manifest transfer\n");
        return;
    }

    snprintf(src, sizeof(src), "%s/%s", od, rel);
    snprintf(dst, sizeof(dst), "%s/%s", LOCAL_OVERLAY, rel);

    if (stat(src, &st) < 0)
        return; /* same "not in the overlay" case stage_tree already reports */

    if (mkdir(dst, 0755) < 0 && errno != EEXIST) {
        printf("staging: %s: mkdir failed: %s\n", dst, strerror(errno));
        return;
    }

    stage_symlinks_walk(src, dst, 0, &stats);
    printf("staging: symlinks under %s: %lu found, %lu created, %lu failed\n",
           rel, stats.found, stats.created, stats.failed);
}

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

        /* Which trees to stage is no longer a hand-written list. It comes from
         * the transitive dylib closure of the test binary, computed host-side
         * by check-guest-dylib-compat.py --closure --emit-staging-trees and
         * handed over in DARLING_STAGING_TREES (colon-separated, paths relative
         * to the overlay).
         *
         * This list used to be hardcoded to usr/lib, System/Library/Frameworks
         * and private/etc, and that cost a root run: AppKit pulls in Onyx2D out
         * of System/Library/PrivateFrameworks, which was on none of them, and
         * the guest died on a bare "image not found". Nothing about that
         * failure looked like a staging bug, which is why a list curated by
         * hand from two examples is the wrong shape for this.
         *
         * The default is only for the harnesses that do not run that preflight
         * (run-smoke, the real-macho tests); the probe passes its own derived
         * list. It is announced, never silent: a staging list nobody derived is
         * the exact condition that produced the Onyx2D run, and whoever reads
         * the log should be able to see which mode produced it without reading
         * this file. */
        const char *trees = getenv("DARLING_STAGING_TREES");
        if (trees == NULL || trees[0] == '\0') {
            printf("staging trees: NOT derived -- using the built-in default; "
                   "run the probe's preflight to derive them from the closure\n");
            trees = DEFAULT_STAGING_TREES;
        } else {
            printf("staging trees: derived from the closure -- %s\n", trees);
        }

        /* Copy the trees the closure lands in. Copying a whole tree rather than
         * only the closure's own files is deliberate: the tree is a superset of
         * the closure, so this cannot regress a run that works today, and it is
         * the granularity that has always been staged here.
         *
         * A tree the overlay does not have is skipped, not an error: a test
         * binary with a small closure has no PrivateFrameworks, and the probe
         * preflight is the thing that must notice a MISSING tree, not this. */
        /* TWO PASSES, LINKS FIRST, and the order is the point rather than an
         * implementation detail.
         *
         * The guest's directory enumeration hands back a PREFIX of a listing
         * and drops the tail (WORKAROUND-344.md §9, measured six ways), so
         * where an entry sits in the listing decides whether the guest ever
         * sees it at all. A staged entry's position is its creation order, and
         * inode numbers are handed out in creation order too — so a link
         * created after every regular file lands at the END of the listing,
         * outside the window.
         *
         * That is what happened, and it was filed as a different bug. The
         * framework root came back from the guest as `. .. Versions pad-01 …`
         * with AppKit and Resources absent, both of them symlinks that were
         * present on disk and resolved. Not missing — last. A long argument
         * concluded the emulation was dropping symlink entries and that two
         * causes were indistinguishable; the staging order explains it with no
         * second hypothesis at all, and this is the change that follows.
         *
         * So: every link in every tree, then every regular file. The two
         * passes write disjoint sets of names, so the order cannot lose
         * anything, and the link pass copies no data so it is the cheap one to
         * do first.
         *
         * The link pass stays a separate, separately-disableable step and
         * never moves inside stage_tree, so reverting to regular-files-only
         * remains one variable. See the comment above stage_symlinks. */
        {
            const char *t = trees;
            while (*t) {
                const char *sep = strchr(t, ':');
                size_t len = sep ? (size_t)(sep - t) : strlen(t);
                if (len > 0) {
                    char tree[512];
                    if (len >= sizeof(tree)) len = sizeof(tree) - 1;
                    memcpy(tree, t, len);
                    tree[len] = '\0';
                    stage_symlinks(od, tree);
                }
                if (sep == NULL) break;
                t = sep + 1;
            }

            t = trees;
            while (*t) {
                const char *sep = strchr(t, ':');
                size_t len = sep ? (size_t)(sep - t) : strlen(t);
                if (len > 0) {
                    char tree[512];
                    if (len >= sizeof(tree)) len = sizeof(tree) - 1;
                    memcpy(tree, t, len);
                    tree[len] = '\0';
                    stage_tree(od, tree);
                }
                if (sep == NULL) break;
                t = sep + 1;
            }
        }

        /* A cached copy makes every copy above a no-op, so their exit status
         * is not evidence. Verify the two files we cannot run without. */
        struct stat lst;
        if (stat(LOCAL_OVERLAY "/usr/lib/dyld", &lst) < 0) {
            fprintf(stderr, "Failed to copy dyld to local overlay\n");
            return 1;
        }
        if (stat(LOCAL_OVERLAY "/usr/lib/libSystem.B.dylib", &lst) < 0) {
            fprintf(stderr, "Failed to copy libSystem.B.dylib to local overlay\n");
            return 1;
        }

        /* etc (master.passwd/pwd.db/group) -- real Foundation code calls
         * getpwuid()/getpwnam() (e.g. for NSHomeDirectory()-for-root and
         * similar lookups) which resolve through the vchroot'd overlay, not
         * the guest's own /etc. Without a passwd db there the lookup just
         * fails, but some callers don't handle that failure -- worth caching
         * same as usr/lib above. od's "etc" is a symlink (Darwin-style
         * etc -> private/etc); go straight to the real target so this does
         * not need a readlink over virtiofs.
         *
         * NOT part of the closure -- no dylib is loaded out of here. It is
         * staged for the data, not for the loader, so it stays out of the
         * derived list and must not be lost when that list replaces the
         * hardcoded one. */
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
