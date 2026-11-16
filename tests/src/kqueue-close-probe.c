/* kqueue-close-probe: two threads block in kevent64 on one kqueue with an EVFILT_MACHPORT for a receive right;
 * the port gets a message, so one of them goes through the machport copyout (the other can be left waiting inside
 * it with the kqueue lock held, for as long as the server does not answer). Meanwhile three threads open pipes and
 * close()/fclose() fds. A close that cannot finish because it needs that kqueue lock is caught by a watchdog.
 * Prints KQCHK lines and a final "KQCHK RESULT pass|fail ..." line. Exit 0 on pass, 1 otherwise. No headers: the
 * few Mach/kevent prototypes are declared here (all exported by libSystem). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <pthread.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include <poll.h>
static void msleep(int ms) { poll(0, 0, ms); }
typedef unsigned int mp_t;
extern mp_t mach_task_self_;
extern int mach_port_allocate(mp_t, int, mp_t *);
extern int mach_port_insert_right(mp_t, mp_t, mp_t, int);
extern int mach_msg(void *, int, unsigned, unsigned, mp_t, unsigned, mp_t);
struct kev64 { uint64_t ident; int16_t filter; uint16_t flags; uint32_t fflags; int64_t data; uint64_t udata; uint64_t ext[2]; };
extern int kevent64(int, const struct kev64 *, int, struct kev64 *, int, unsigned, const struct timespec *);
extern int kqueue(void);
#define EVFILT_MACHPORT (-8)
#define EV_ADD 1
#define EV_ENABLE 4
#define MACH_RCV_MSG 2
static mp_t port;
static volatile int a_done, b_done, stop, got, closers_done;
static int kq;

static void *waiter(void *arg)
{
	struct kev64 ch, out;
	static char buf[4096];
	memset(&ch, 0, sizeof ch);
	ch.ident = port; ch.filter = EVFILT_MACHPORT; ch.flags = EV_ADD | EV_ENABLE; ch.fflags = MACH_RCV_MSG;
	ch.ext[0] = (uint64_t)(uintptr_t)buf; ch.ext[1] = sizeof buf;
	int n = kevent64(kq, &ch, 1, &out, 1, 0, NULL);
	printf("KQCHK waiter first kevent64 n=%d\n", n);
	while (!stop) {
		n = kevent64(kq, NULL, 0, &out, 1, 0, NULL);
		if (n > 0) got++;
	}
	a_done = 1;
	return NULL;
}
static void *waiter2(void *arg)
{
	struct kev64 out;
	int n = kevent64(kq, NULL, 0, &out, 1, 0, NULL);
	printf("KQCHK waiter2 kevent64 n=%d\n", n);
	return NULL;
}
static void *watchdog(void *arg)
{
	msleep((int)(long)arg * 1000);
	printf("KQCHK RESULT fail close blocked while a thread sits in kevent64 (closers done=%d/3, waiter events=%d)\n", closers_done, got);
	fflush(stdout);
	_exit(1);
	return NULL;
}
static void send_one(void)
{
	struct { uint32_t bits, size, remote, local, voucher; int32_t id; } m = { 19, sizeof m, port, 0, 0, 77 };
	mach_msg(&m, 1, sizeof m, 0, 0, 0, 0);
}
static void *sender(void *arg)
{
	int i;
	for (i = 0; i < 1 && !b_done; i++) {
		send_one();
		if ((i & 15) == 0) msleep(1);
	}
	return NULL;
}
static void *closer(void *arg)
{
	int i;
	for (i = 0; i < 300; i++) {
		int pp[2];
		FILE *f;
		if (pipe(pp) < 0) { printf("KQCHK RESULT fail pipe\n"); _exit(1); }
		close(pp[1]);
		if (i & 1) close(pp[0]);
		else if ((f = fdopen(pp[0], "r"))) fclose(f);
		else close(pp[0]);
	}
	__sync_fetch_and_add(&closers_done, 1);
	return NULL;
}
int main(void)
{
	pthread_t a, a2, w, s, c[3];
	int i, kr;
	setvbuf(stdout, NULL, _IOLBF, 0);
	kr = mach_port_allocate(mach_task_self_, 1, &port);
	if (kr || !port) { printf("KQCHK RESULT fail port_allocate kr=0x%x\n", kr); return 1; }
	kr = mach_port_insert_right(mach_task_self_, port, port, 20);
	if (kr) { printf("KQCHK RESULT fail insert_right kr=0x%x\n", kr); return 1; }
	kq = kqueue();
	if (kq < 0) { printf("KQCHK RESULT fail kqueue\n"); return 1; }
	pthread_create(&w, NULL, watchdog, (void *)30L);
	pthread_create(&a, NULL, waiter, NULL);
	msleep(100);
	pthread_create(&a2, NULL, waiter2, NULL);
	msleep(300);
	pthread_create(&s, NULL, sender, NULL);
	for (i = 0; i < 3; i++) pthread_create(&c[i], NULL, closer, NULL);
	for (i = 0; i < 3; i++) pthread_join(c[i], NULL);
	b_done = 1;
	pthread_join(s, NULL);
	printf("KQCHK closers finished (3 x 300 fds), waiter handled %d events\n", got);
	printf("KQCHK RESULT pass\n");
	return 0;
}
