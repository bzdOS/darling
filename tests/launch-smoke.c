/*
 * launch-smoke.c — minimal smoke test harness.
 * Starts darlingserver in a subprocess (proper pipefd handshake),
 * waits for it to bind its socket, then execs mldr on a static Mach-O.
 *
 * Build: cc -o /tmp/launch-smoke /path/to/darling/tests/launch-smoke.c
 * Run as root on FreeBSD 15.1 dev VM.
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

#define PREFIX        "/tmp/darling-smoke"
#define SOCK_PATH     PREFIX "/.darlingserver.sock"
#define DSERVER       "/var/darling-build/dserver/darlingserver"
#define MLDR          "/var/darling-build/dserver/mldr-real/mldr"
#define BINARY        "/path/to/darling/tests/hello-static-macho"

static void cleanup(void) {
    system("pkill -9 darlingserver 2>/dev/null");
    system("rm -rf " PREFIX);
}

int main(void) {
    if (getuid() != 0) {
        fprintf(stderr, "Must run as root\n");
        return 1;
    }

    cleanup();
    mkdir(PREFIX, 0755);

    /* Create pipe: pipefd[0]=read, pipefd[1]=write.
     * Pass write end (pipefd[1]) to darlingserver as argv[4].
     * darlingserver writes "." when it's started and ready to fork. */
    int pipefd[2];
    if (pipe(pipefd) < 0) {
        perror("pipe"); return 1;
    }

    pid_t dserver_pid = fork();
    if (dserver_pid < 0) { perror("fork"); return 1; }

    if (dserver_pid == 0) {
        /* Child: exec darlingserver */
        close(pipefd[0]);

        char pipestr[16];
        snprintf(pipestr, sizeof(pipestr), "%d", pipefd[1]);

        /* argv: darlingserver prefix uid gid pipefd fix_permissions */
        char uidstr[16], gidstr[16];
        snprintf(uidstr, sizeof(uidstr), "%d", (int)getuid());
        snprintf(gidstr, sizeof(gidstr), "%d", (int)getgid());

        execl(DSERVER, "darlingserver",
              PREFIX, uidstr, gidstr, pipestr, "0",
              (char*)NULL);
        perror("execl darlingserver");
        _exit(1);
    }

    /* Parent: wait for darlingserver to write "." to pipe */
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

    /* Poll for socket to appear (darlingserver binds it in a child process) */
    struct stat st;
    int waited = 0;
    while (stat(SOCK_PATH, &st) < 0 || !S_ISSOCK(st.st_mode)) {
        if (waited++ > 50) {
            fprintf(stderr, "Socket %s did not appear after 5s\n", SOCK_PATH);
            cleanup();
            return 1;
        }
        usleep(100000); /* 100ms */
    }
    printf("darlingserver socket ready: %s\n", SOCK_PATH);

    /* Set environment and exec mldr */
    setenv("__mldr_sockpath", SOCK_PATH, 1);
    setenv("__mldr_DYLD_ROOT_PATH", PREFIX, 1);

    printf("Running: %s %s\n", MLDR, BINARY);
    fflush(stdout);

    execl(MLDR, MLDR, BINARY, (char*)NULL);
    perror("execl mldr");
    cleanup();
    return 1;
}
