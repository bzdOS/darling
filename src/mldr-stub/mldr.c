/* mldr.c — minimal mldr shim for FreeBSD darlingserver testing.
 *
 * Real mldr loads Mach-O binaries via dyld inside a vchroot.
 * This stub skips Mach-O loading entirely and exec()s the init process as a
 * native FreeBSD binary, keeping darlingserver's env vars intact so the init
 * process can find the Mach IPC socket and register with darlingserver.
 *
 * darlingserver calls:
 *   execl(mldrPath,
 *         "mldr!<LIBEXEC_PATH>/usr/libexec/darling/vchroot",  <- argv[0]
 *         "vchroot",                                           <- argv[1]
 *         prefix,                                              <- argv[2]
 *         initPath,                                            <- argv[3]
 *         NULL);
 *
 * Relevant env vars set by darlingserver before exec:
 *   __mldr_sockpath      — path to darlingserver Unix socket
 *   __mldr_DYLD_ROOT_PATH — LIBEXEC_PATH (macOS root overlay)
 *   DSERVER_INIT         — override for initPath (checked first)
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>

int main(int argc, char **argv, char **envp)
{
    const char *init = getenv("DSERVER_INIT");

    if (!init || init[0] == '\0') {
        if (argc > 3 && argv[3] != NULL && argv[3][0] != '\0')
            init = argv[3];
    }

    const char *sockpath = getenv("__mldr_sockpath");
    fprintf(stderr, "mldr-stub: sockpath=%s init=%s\n",
            sockpath ? sockpath : "(none)",
            init     ? init     : "(none)");

    if (init && init[0] != '\0') {
        /* exec init process as a native FreeBSD binary, env passthrough */
        char *args[] = { (char *)init, NULL };
        execve(init, args, envp);
        fprintf(stderr, "mldr-stub: execve(%s) failed: %s\n", init, strerror(errno));
    }

    /* No init or exec failed — keep child alive so darlingserver
     * does not see a premature init exit and panic. */
    fprintf(stderr, "mldr-stub: idling (no init process)\n");
    for (;;)
        pause();

    return 0;
}
