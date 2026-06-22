/* bsdos_posix_compat.c — FreeBSD shim implementations for Linux-specific
 * APIs used by darlingserver: eventfd and timerfd.
 *
 * eventfd: implemented via a socketpair.  The read socket is returned as
 * the eventfd; the write socket is stored in a global table.
 *
 * timerfd: implemented via a pipe + pthread.  timerfd_settime() starts a
 * background thread that sleeps until the deadline then writes to the pipe.
 * The server's epoll loop sees the pipe-read-end as readable and drains it. */

#include <sys/socket.h>
#include <sys/types.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <pthread.h>
#include <time.h>
#include <sys/eventfd.h>
#include <sys/timerfd.h>

/* ── eventfd ──────────────────────────────────────────────────────────────── */

#define BSDOS_EFD_MAX 4096

static int _efd_write[BSDOS_EFD_MAX];  /* _efd_write[read_fd] = write_fd */

static void _efd_init(void) {
    static int done = 0;
    if (!done) {
        memset(_efd_write, -1, sizeof(_efd_write));
        done = 1;
    }
}

int eventfd(unsigned int initval, int flags) {
    _efd_init();
    int sv[2];
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv) < 0)
        return -1;
    /* Apply O_NONBLOCK / O_CLOEXEC to both ends */
    if (flags & EFD_NONBLOCK) {
        fcntl(sv[0], F_SETFL, fcntl(sv[0], F_GETFL) | O_NONBLOCK);
        fcntl(sv[1], F_SETFL, fcntl(sv[1], F_GETFL) | O_NONBLOCK);
    }
    if (flags & EFD_CLOEXEC) {
        fcntl(sv[0], F_SETFD, FD_CLOEXEC);
        fcntl(sv[1], F_SETFD, FD_CLOEXEC);
    }
    if (sv[0] < BSDOS_EFD_MAX)
        _efd_write[sv[0]] = sv[1];
    if (initval > 0) {
        uint64_t v = initval;
        (void)send(sv[1], &v, sizeof(v), 0);
    }
    return sv[0];  /* read end is the "eventfd" */
}

int eventfd_read(int fd, eventfd_t *value) {
    uint64_t v = 0;
    ssize_t r = recv(fd, &v, sizeof(v), 0);
    if (r < 0) return -1;
    if (value) *value = v;
    return 0;
}

int eventfd_write(int fd, eventfd_t value) {
    if (fd < 0 || fd >= BSDOS_EFD_MAX || _efd_write[fd] < 0) {
        errno = EBADF;
        return -1;
    }
    uint64_t v = value;
    ssize_t r = send(_efd_write[fd], &v, sizeof(v), 0);
    return (r < 0) ? -1 : 0;
}

/* ── timerfd ──────────────────────────────────────────────────────────────── */

#define BSDOS_TFD_MAX 4096

typedef struct {
    int write_fd;           /* pipe write end */
    pthread_t thread;
    int active;
    struct itimerspec spec;
    int abstime;
    pthread_mutex_t mu;
    pthread_cond_t cv;
    int cancelled;
} bsdos_tfd_t;

static bsdos_tfd_t *_tfd_state[BSDOS_TFD_MAX];

static void *_tfd_thread(void *arg) {
    bsdos_tfd_t *s = arg;
    pthread_mutex_lock(&s->mu);
    while (!s->cancelled) {
        struct timespec deadline = s->spec.it_value;
        if (deadline.tv_sec == 0 && deadline.tv_nsec == 0) {
            /* timer disarmed — wait for re-arm */
            pthread_cond_wait(&s->cv, &s->mu);
            continue;
        }
        if (!s->abstime) {
            /* relative: convert to absolute */
            struct timespec now;
            clock_gettime(CLOCK_MONOTONIC, &now);
            deadline.tv_sec  += now.tv_sec;
            deadline.tv_nsec += now.tv_nsec;
            if (deadline.tv_nsec >= 1000000000L) {
                deadline.tv_sec++;
                deadline.tv_nsec -= 1000000000L;
            }
        }
        int r = pthread_cond_timedwait(&s->cv, &s->mu, &deadline);
        if (s->cancelled) break;
        if (r == ETIMEDOUT) {
            /* Timer fired: write expiry count */
            uint64_t v = 1;
            (void)write(s->write_fd, &v, sizeof(v));
            /* Disarm (single-shot) */
            s->spec.it_value.tv_sec  = 0;
            s->spec.it_value.tv_nsec = 0;
        }
        /* On spurious wakeup / re-arm: loop back */
    }
    pthread_mutex_unlock(&s->mu);
    return NULL;
}

int timerfd_create(int clockid, int flags) {
    (void)clockid;
    int pfd[2];
    if (pipe(pfd) < 0) return -1;
    if (flags & TFD_NONBLOCK) {
        fcntl(pfd[0], F_SETFL, fcntl(pfd[0], F_GETFL) | O_NONBLOCK);
        fcntl(pfd[1], F_SETFL, fcntl(pfd[1], F_GETFL) | O_NONBLOCK);
    }
    if (flags & TFD_CLOEXEC) {
        fcntl(pfd[0], F_SETFD, FD_CLOEXEC);
        fcntl(pfd[1], F_SETFD, FD_CLOEXEC);
    }
    bsdos_tfd_t *s = calloc(1, sizeof(*s));
    if (!s) { close(pfd[0]); close(pfd[1]); return -1; }
    s->write_fd = pfd[1];
    s->cancelled = 0;
    pthread_mutex_init(&s->mu, NULL);
    pthread_cond_init(&s->cv, NULL);
    pthread_create(&s->thread, NULL, _tfd_thread, s);
    if (pfd[0] < BSDOS_TFD_MAX)
        _tfd_state[pfd[0]] = s;
    return pfd[0];
}

int timerfd_settime(int fd, int flags, const struct itimerspec *new_value,
                    struct itimerspec *old_value) {
    if (fd < 0 || fd >= BSDOS_TFD_MAX || !_tfd_state[fd]) {
        errno = EBADF;
        return -1;
    }
    bsdos_tfd_t *s = _tfd_state[fd];
    pthread_mutex_lock(&s->mu);
    if (old_value) *old_value = s->spec;
    if (new_value) s->spec = *new_value;
    s->abstime = (flags & TFD_TIMER_ABSTIME) ? 1 : 0;
    pthread_cond_signal(&s->cv);
    pthread_mutex_unlock(&s->mu);
    return 0;
}
