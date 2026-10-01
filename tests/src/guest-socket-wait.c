/* guest-socket-wait.c — does a SOCKET wait return on a SPAWNED guest thread?
 *
 * The merged verdict on the thread lane (§13's addendum in
 * build-freebsd/WORKAROUND-344.md) is that guest-thread resumption is ALIVE
 * for the waits that were measured — nanosleep, a pipe read and sem_wait all
 * come back on a spawned thread — while wl_display_roundtrip from a spawned
 * thread hangs (run №9-4). Roundtrip's blocking work is a SOCKET read
 * multiplexed with a readiness wait, so this probe asks the socket half of
 * that directly: on a spawned thread, does a blocking read() on a connected
 * socket return when it is released?
 *
 * THREE LEGS, each run twice — once with MAIN as the reader (the control)
 * and once with a SPAWNED thread as the reader (the lane):
 *
 *   1. blocking read() on a UNIX socketpair, released by write() at the
 *      other end;
 *   2. poll(POLLIN) on the same fd, released by the same write;
 *   3. connect() to a live local port and read() there. The probe opens
 *      that port itself — see the trap table below. When socketpair is
 *      unavailable (the guest) legs 1-2 run on this fallback source too,
 *      so the socket question is still asked; with neither a socketpair
 *      nor a bindable port the legs print "not exercised" and decide
 *      nothing.
 *
 * TRAP TABLE GROUND TRUTH (freebsd_syscall_trap.c's MACOS_SYS_* switch — this
 * probe is a macOS Mach-O, so its libc reaches that switch, not the Linux
 * one). DEFINED as passthroughs: 3 read, 4 write, 30 accept, 42 pipe,
 * 92 fcntl, 93 select, 97 socket, 98 connect, 101 send, 102 recv, 104 bind,
 * 106 listen, 133 sendto. NOT DEFINED — the default case prints "unhandled
 * macOS BSD syscall" and returns ENOSYS: 135 socketpair, 395 poll. So inside
 * the guest the socketpair source is EXPECTED to be refused and poll is
 * expected to fail; both refusals are results: they say the socket-wait
 * question is askable there only over connect+read, and that the readiness
 * half has no guest implementation at all — select (93) does, and select is
 * the follow-up probe, not this one.
 *
 * This differs from the lane law's "never call what the guest lacks". That
 * law was about CONSTRUCTORS — a wait built on a primitive nothing provides
 * measures the constructor's absence. Here the refusal IS part of the
 * measurement: which socket waits this guest has is half the question.
 *
 * Every wait records rc, errno and its own 228-measured duration INSIDE the
 * calling thread. rc < 0 is a REFUSAL and is never read as a return. A
 * release is only believed when the wait was STILL OUT RELEASE_MS after the
 * reached flag — a call that comes straight back has measured nothing. No
 * leg can hang the probe: spawned legs are bounded by an observation
 * deadline and print DID-NOT-RETURN, and the main-thread control's release
 * is doubled by a safety net (a separate write, not the one under test), so
 * even the sabotage control cannot park main() forever.
 *
 * Control and lane never share the resource they draw on: every run of a leg
 * builds a FRESH pair (socketpair or listener+connect). The lane law's
 * "half a peer is not a peer" one level up.
 */

#include <errno.h>
#include <netinet/in.h>
#include <poll.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>

#define RELEASE_MS    2000  /* a wait still out this long after the reached */
                            /* flag is believed to have PARKED */
#define SAFETY_MS     2000  /* the net's delay after the release under test */
#define BOUND_MS      6000  /* observation deadline for spawned legs, and */
                            /* the poll timeout (poll bounds itself) */
#define PEER_PORT_ENV "DARLING_THREAD_PEER_PORT"
#define PEER_SOCK_ENV "DARLING_THREAD_PEER_SOCK"
#define PORT_LO       47310 /* self-listener port range: getsockname (397) */
#define PORT_HI       47399 /* is undefined in the trap, so the port that */
                            /* bind() picked cannot be read back — a range
                             * and EADDRINUSE-advance stand in for it */

/* ONE WAIT'S WHOLE OUTCOME, recorded where it happened. rc says
 * refused-or-not, err says which refusal, ms is the measurement. `parked` is
 * an outside observation — still out RELEASE_MS after the reached flag with
 * nobody having released it. */
struct wait_result {
	int rc;
	int err;
	long entered_ms;
	long left_ms;
	volatile int parked;
};

static long now_ms(void)
{
	struct timespec ts;

	if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) return 0;
	return (long)ts.tv_sec * 1000L + ts.tv_nsec / 1000000L;
}

/* Bounded spin on 228 — the lane's answer to "wait two seconds" in a guest
 * whose nanosleep is not in the trap table either. */
static void spin_ms(long ms)
{
	long start = now_ms();

	if (start == 0) return;
	while (now_ms() - start < ms) { }
}

static int wait_flag(volatile int *flag)
{
	long start = now_ms();

	if (start == 0) return *flag ? 1 : 0;
	while (now_ms() - start < BOUND_MS) {
		if (*flag) return 1;
	}
	return *flag ? 1 : 0;
}

static void ms_str(const struct wait_result *r, char *buf, size_t n)
{
	if (r->left_ms != 0 && r->entered_ms != 0)
		snprintf(buf, n, "%ldms", r->left_ms - r->entered_ms);
	else if (r->parked)
		snprintf(buf, n, "still out");
	else
		snprintf(buf, n, "no clock");
}

/* THE release under test. One call site, on its own marked line: the
 * sabotage control compiles a COPY of this file with that line replaced by
 * (void)0, and the copy MUST print DID-NOT-RETURN — a verdict that cannot
 * fail is a constant. */
static void release_write(int wfd)
{
	char x = 'x';

	errno = 0;
	write(wfd, &x, 1);   /* RELEASE-WRITE */
	if (errno != 0)
		printf("  note: release write failed: %s\n", strerror(errno));
}

/* The safety net under the MAIN-THREAD CONTROL only. A separate write, so
 * gutting the release under test leaves the control able to finish and the
 * probe able to report. Which of the two woke main is measured by duration
 * and printed. */
static void net_write(int wfd)
{
	char x = 'n';

	write(wfd, &x, 1);
}

static void print_wait(const char *who, const char *leg,
                       const struct wait_result *r, int did_not_return,
                       const char *extra)
{
	char msbuf[32];
	const char *word;

	if (did_not_return) {
		/* The call is still out: there IS no rc, errno or duration yet.
		 * Printing zeros for them would report a missing measurement as
		 * if it were a fast one — the correction the lane probe needed. */
		printf("  %-6s %-10s %-16s rc=- errno=- over %s%s\n", who, leg,
		       "DID-NOT-RETURN", r->parked ? "still out" : "no clock",
		       extra ? extra : "");
		return;
	}
	if (r->rc < 0)
		word = "REFUSED";
	else if (r->parked)
		word = "RETURNED (parked)";
	else
		word = "RETURNED-NO-PARK";
	ms_str(r, msbuf, sizeof(msbuf));
	printf("  %-6s %-10s %-16s rc=%d errno=%s over %s%s\n", who, leg, word,
	       r->rc, r->err ? strerror(r->err) : "0", msbuf,
	       extra ? extra : "");
}

/* ---------- the read leg ---------- */

struct read_ctx {
	int rfd;
	int wfd;
	struct wait_result res;
	volatile int reached;
	volatile int returned;
};

/* File-scope, not stack: a sabotaged release leaves the reader thread
 * parked in read() forever, and closing its pair wakes it into a write of
 * its result. If that memory were the call frame of a function that has
 * already returned, the write would land in whatever reused the stack —
 * measured: the first sabotage run segfaulted exactly there. The lane
 * probe kept its wait_result static for the same reason. One context at a
 * time is in use, so a single static pair of contexts is enough. */
static struct read_ctx g_read;

static void *read_thread(void *p)
{
	struct read_ctx *c = (struct read_ctx *)p;
	char buf[64];

	c->reached = 1;              /* BEFORE the call: "reached" is the claim */
	c->res.entered_ms = now_ms();
	errno = 0;
	c->res.rc = (int)read(c->rfd, buf, sizeof(buf));
	c->res.err = errno;
	c->res.left_ms = now_ms();
	c->returned = 1;             /* reaching THIS line is the finding */
	return NULL;
}

static void *release_then_net(void *p)
{
	struct read_ctx *c = (struct read_ctx *)p;

	wait_flag(&c->reached);
	spin_ms(RELEASE_MS);         /* give the read time to REALLY park */
	c->res.parked = !c->returned; /* still out this long in => it parked */
	release_write(c->wfd);
	spin_ms(SAFETY_MS);
	net_write(c->wfd);
	return NULL;
}

/* MAIN as the reader; a releaser thread does release-then-net. The net is
 * why this control cannot hang the probe however the release is sabotaged. */
static int read_on_main(int rfd, int wfd, struct wait_result *out,
                        char *netword, size_t nw)
{
	struct read_ctx *c = &g_read;
	pthread_t tr;

	memset(c, 0, sizeof(*c));
	c->rfd = rfd;
	c->wfd = wfd;
	if (pthread_create(&tr, NULL, release_then_net, c) != 0) {
		printf("  note: releaser thread not created: %s\n", strerror(errno));
		return -1;
	}
	pthread_detach(tr);
	c->reached = 1;
	c->res.entered_ms = now_ms();
	{
		char buf[64];
		errno = 0;
		c->res.rc = (int)read(c->rfd, buf, sizeof(buf));
	}
	c->res.err = errno;
	c->res.left_ms = now_ms();
	c->returned = 1;
	/* The net fires RELEASE_MS+SAFETY_MS after the reached flag; a return
	 * later than that means the release under test did not take and only
	 * the net did. */
	if (c->res.left_ms - c->res.entered_ms >= RELEASE_MS + SAFETY_MS - 200)
		snprintf(netword, nw, " (only after the safety net)");
	else
		netword[0] = '\0';
	*out = c->res;
	pthread_join(tr, NULL);
	return 0;
}

/* A SPAWNED thread as the reader; main does the release under test and then
 * observes for BOUND_MS. No net here — main is the observer, and a
 * DID-NOT-RETURN print plus exit is the bounded outcome. */
static int read_on_spawned(int rfd, int wfd, struct wait_result *out)
{
	struct read_ctx *c = &g_read;
	pthread_t tr;

	memset(c, 0, sizeof(*c));
	c->rfd = rfd;
	c->wfd = wfd;
	if (pthread_create(&tr, NULL, read_thread, c) != 0) {
		printf("  note: reader thread not created: %s\n", strerror(errno));
		return -1;
	}
	wait_flag(&c->reached);
	spin_ms(RELEASE_MS);
	c->res.parked = (c->reached && !c->returned);
	release_write(c->wfd);
	if (!wait_flag(&c->returned)) {
		*out = c->res;            /* rc/errno not written: still out */
		pthread_detach(tr);
		return 1;                /* DID-NOT-RETURN */
	}
	pthread_join(tr, NULL);
	*out = c->res;
	return 0;
}

/* ---------- the poll leg ---------- */

struct poll_ctx {
	int fd;
	int wfd;
	struct wait_result res;
	volatile int reached;
	volatile int returned;
};

static struct poll_ctx g_poll;
static int g_poll_refused;   /* poll() drew rc<0 (ENOSYS expected in guest) */

static void *poll_thread(void *p)
{
	struct poll_ctx *c = (struct poll_ctx *)p;
	struct pollfd pfd;

	pfd.fd = c->fd;
	pfd.events = POLLIN;
	pfd.revents = 0;
	c->reached = 1;
	c->res.entered_ms = now_ms();
	errno = 0;
	c->res.rc = poll(&pfd, 1, BOUND_MS);   /* its own timeout = its bound */
	c->res.err = errno;
	c->res.left_ms = now_ms();
	c->returned = 1;
	return NULL;
}

static void *poll_release_after_flag(void *p)
{
	struct poll_ctx *c = (struct poll_ctx *)p;

	wait_flag(&c->reached);
	spin_ms(RELEASE_MS);
	release_write(c->wfd);
	return NULL;
}

static int poll_on_main(int fd, int wfd, struct wait_result *out)
{
	struct poll_ctx *c = &g_poll;
	pthread_t tr;
	struct pollfd pfd;

	memset(c, 0, sizeof(*c));
	c->fd = fd;
	c->wfd = wfd;
	if (pthread_create(&tr, NULL, poll_release_after_flag, c) != 0) {
		printf("  note: releaser thread not created: %s\n", strerror(errno));
		return -1;
	}
	pthread_detach(tr);
	pfd.fd = fd;
	pfd.events = POLLIN;
	pfd.revents = 0;
	c->reached = 1;
	c->res.entered_ms = now_ms();
	errno = 0;
	c->res.rc = poll(&pfd, 1, BOUND_MS);
	c->res.err = errno;
	c->res.left_ms = now_ms();
	c->returned = 1;
	pthread_join(tr, NULL);
	*out = c->res;
	return 0;
}

static int poll_on_spawned(int fd, int wfd, struct wait_result *out)
{
	struct poll_ctx *c = &g_poll;
	pthread_t tr;

	memset(c, 0, sizeof(*c));
	c->fd = fd;
	c->wfd = wfd;
	if (pthread_create(&tr, NULL, poll_thread, c) != 0) {
		printf("  note: poller thread not created: %s\n", strerror(errno));
		return -1;
	}
	wait_flag(&c->reached);
	spin_ms(RELEASE_MS);
	release_write(c->wfd);
	wait_flag(&c->returned);
	pthread_join(tr, NULL);
	*out = c->res;
	return 0;
}

/* ---------- fd sources ---------- */

struct pair {
	int rfd;      /* the end the reader blocks on */
	int wfd;      /* the end the releaser writes to */
	int kind;     /* PAIR_SOCKPAIR / PAIR_TCP */
	char note[256];
	int refused;
};

#define PAIR_SOCKPAIR 1
#define PAIR_TCP      2

struct listener {
	int lfd;
	int port;
	int acpt_fd;
	volatile int accepted;
	int accept_err;
};

static void *accept_thread(void *p)
{
	struct listener *L = (struct listener *)p;

	errno = 0;
	L->acpt_fd = accept(L->lfd, NULL, NULL);
	L->accept_err = errno;
	L->accepted = 1;
	return NULL;
}

static int connect_local(int port)
{
	struct sockaddr_in sa;
	int fd;

	fd = socket(AF_INET, SOCK_STREAM, 0);
	if (fd < 0) return -1;
	memset(&sa, 0, sizeof(sa));
	sa.sin_family = AF_INET;
	sa.sin_port = htons((unsigned short)port);
	sa.sin_addr.s_addr = htonl(0x7f000001);   /* 127.0.0.1 */
	if (connect(fd, (struct sockaddr *)&sa, sizeof(sa)) < 0) {
		int e = errno;
		close(fd);
		errno = e;
		return -1;
	}
	return fd;
}

/* One self-contained TCP pair: listener + accepted fd (the write end — its
 * send queue is the client's receive queue) + connected client fd (the read
 * end). The port comes from a range because getsockname cannot be used; the
 * trap defines bind/listen/accept on the macOS side, so the guest can be its
 * own peer and the run stays peerless. */
static int make_tcp_pair(struct pair *P)
{
	struct listener L;
	pthread_t ta;
	int port, cfd;

	memset(P, 0, sizeof(*P));
	P->kind = PAIR_TCP;
	memset(&L, 0, sizeof(L));
	for (port = PORT_LO; port <= PORT_HI; port++) {
		L.lfd = socket(AF_INET, SOCK_STREAM, 0);
		if (L.lfd < 0) {
			P->refused = 1;
			snprintf(P->note, sizeof(P->note),
			         "self-listener: socket() refused: %s",
			         strerror(errno));
			return -1;
		}
		{
			struct sockaddr_in sa;
			memset(&sa, 0, sizeof(sa));
			sa.sin_family = AF_INET;
			sa.sin_port = htons((unsigned short)port);
			sa.sin_addr.s_addr = htonl(0x7f000001);
			if (bind(L.lfd, (struct sockaddr *)&sa, sizeof(sa)) < 0) {
				close(L.lfd);
				if (errno == EADDRINUSE) continue;
				P->refused = 1;
				snprintf(P->note, sizeof(P->note),
				         "self-listener: bind(127.0.0.1) refused: %s"
				         " — EAFNOSUPPORT at the host-facing call with"
				         " CLEAN sockaddr bytes at the trap ([02 00 ...];"
				         " measured, §13 step 4): the break is at or below"
				         " the host bind",
				         strerror(errno));
				return -1;
			}
		}
		if (listen(L.lfd, 4) < 0) {
			P->refused = 1;
			snprintf(P->note, sizeof(P->note),
			         "self-listener: listen refused: %s",
			         strerror(errno));
			close(L.lfd);
			return -1;
		}
		L.port = port;
		break;
	}
	if (L.port == 0) {
		P->refused = 1;
		snprintf(P->note, sizeof(P->note),
		         "self-listener: every port %d..%d was in use",
		         PORT_LO, PORT_HI);
		return -1;
	}
	if (pthread_create(&ta, NULL, accept_thread, &L) != 0) {
		P->refused = 1;
		snprintf(P->note, sizeof(P->note), "accept thread not created");
		close(L.lfd);
		return -1;
	}
	pthread_detach(ta);
	cfd = connect_local(L.port);
	if (cfd < 0) {
		P->refused = 1;
		snprintf(P->note, sizeof(P->note),
		         "connect(127.0.0.1,%d) refused: %s", L.port,
		         strerror(errno));
		wait_flag((volatile int *)&L.accepted);
		close(L.lfd);
		return -1;
	}
	if (!wait_flag((volatile int *)&L.accepted) || L.acpt_fd < 0) {
		P->refused = 1;
		snprintf(P->note, sizeof(P->note),
		         "accept() never returned (errno=%s)",
		         strerror(L.accept_err));
		close(cfd);
		close(L.lfd);
		return -1;
	}
	P->rfd = cfd;
	P->wfd = L.acpt_fd;
	return 0;
}

static int make_sockpair(struct pair *P)
{
	int fds[2];

	memset(P, 0, sizeof(*P));
	P->kind = PAIR_SOCKPAIR;
	errno = 0;
	if (socketpair(AF_UNIX, SOCK_STREAM, 0, fds) < 0) {
		P->refused = 1;
		snprintf(P->note, sizeof(P->note),
		         "socketpair refused: %s (errno=%d) — overlay forwards to"
		         " LINUX 53; the trap carries that case on this branch",
		         strerror(errno), errno);
		return -1;
	}
	P->rfd = fds[0];
	P->wfd = fds[1];
	return 0;
}

/* The env-provided live port — the harness peer, same convention as the lane
 * probe (DARLING_THREAD_PEER_PORT). Measured in the guest first run: bind
 * (macOS 104) is ENOSYS there too, so the guest canNOT open its own port and
 * a live local port has to come from outside. The peer echoes, so the
 * release is a write to the same full-duplex fd and the echo is what the
 * read returns on. Read end and write end are the same fd here — unlike a
 * socketpair — and that is stated in the output rather than papered over. */
#define PAIR_ENVPEER 3

static int make_envpeer(struct pair *P)
{
	const char *port = getenv(PEER_PORT_ENV);
	struct sockaddr_in sa;
	int fd;

	memset(P, 0, sizeof(*P));
	P->kind = PAIR_ENVPEER;
	if (!port || !*port) {
		P->refused = 1;
		snprintf(P->note, sizeof(P->note),
		         "no %s in the environment: nothing live to connect to,"
		         " and the guest cannot bind one itself (measured: bind"
		         " is ENOSYS in the guest)", PEER_PORT_ENV);
		return -1;
	}
	fd = socket(AF_INET, SOCK_STREAM, 0);
	if (fd < 0) {
		P->refused = 1;
		snprintf(P->note, sizeof(P->note),
		         "socket() refused: %s", strerror(errno));
		return -1;
	}
	memset(&sa, 0, sizeof(sa));
	sa.sin_family = AF_INET;
	sa.sin_port = htons((unsigned short)atoi(port));
	sa.sin_addr.s_addr = htonl(0x7f000001);   /* 127.0.0.1 */
	if (connect(fd, (struct sockaddr *)&sa, sizeof(sa)) < 0) {
		P->refused = 1;
		snprintf(P->note, sizeof(P->note),
		         "connect(127.0.0.1,%s) refused: %s — the peer is dead or"
		         " never came up", port, strerror(errno));
		close(fd);
		return -1;
	}
	P->rfd = fd;
	P->wfd = fd;                  /* full duplex: the echo comes back on */
	return 0;                     /* the same fd the release was written to */
}

/* The AF_UNIX live socket — the SAME path the wayland window probe uses:
 * wl_display_connect reaches the compositor's UNIX socket in this guest, so
 * AF_UNIX is the one socket kind with a measured-working connect. Measured
 * mechanics behind the other sources (all in the overlay's own
 * bsd_syscall_table.c, which is the table the guest's libc actually
 * reaches — not the trap's switch):
 *
 *   socketpair: overlay sys_socketpair -> LINUX 134 -> trap has no 134 ->
 *               ENOSYS. The guest's table carries [135] but its body needs
 *               a Linux socketpair the trap never defines.
 *   AF_INET connect: overlay sys_connect -> sockaddr_fixup_from_bsd copies
 *               the macOS sockaddr UNCHANGED — sin_len stays as byte 0, so
 *               Linux reads sin_family = (sin_len | family<<8) = garbage
 *               -> host connect -> EAFNOSUPPORT. Measured live.
 *   bind/listen: overlay sys_bind/sys_listen -> LINUX 49/50 -> trap has
 *               neither -> ENOSYS. Measured live.
 *   AF_UNIX connect: overlay sys_connect -> sockaddr_fixup_from_bsd runs
 *               vchroot_expand over sun_path -> LINUX 42 -> trap -> host.
 *               This is the wayland socket's own path and it works.
 *
 * So a live local port for the guest has to be an AF_UNIX socket provided
 * from outside, at a path the guest's vchroot can see — /tmp, same as the
 * sway IPC sockets the window probe derives XDG_RUNTIME_DIR from. */
#define PAIR_ENVUNIX 4

static int make_envunix(struct pair *P)
{
	const char *path = getenv(PEER_SOCK_ENV);
	struct sockaddr_un sa;
	int fd;

	memset(P, 0, sizeof(*P));
	P->kind = PAIR_ENVUNIX;
	if (!path || !*path) {
		P->refused = 1;
		snprintf(P->note, sizeof(P->note),
		         "no %s in the environment: the AF_UNIX live socket is the"
		         " only source this guest can actually connect to", PEER_SOCK_ENV);
		return -1;
	}
	fd = socket(AF_UNIX, SOCK_STREAM, 0);
	if (fd < 0) {
		P->refused = 1;
		snprintf(P->note, sizeof(P->note),
		         "socket(AF_UNIX) refused: %s", strerror(errno));
		return -1;
	}
	memset(&sa, 0, sizeof(sa));
	sa.sun_family = AF_UNIX;
	snprintf(sa.sun_path, sizeof(sa.sun_path), "%s", path);
	if (connect(fd, (struct sockaddr *)&sa, sizeof(sa)) < 0) {
		P->refused = 1;
		snprintf(P->note, sizeof(P->note),
		         "connect(AF_UNIX,%s) refused: %s — vchroot-expand of the"
		         " path or the host socket itself", path, strerror(errno));
		close(fd);
		return -1;
	}
	P->rfd = fd;
	P->wfd = fd;                  /* full duplex, echo release like AF_INET */
	return 0;
}

/* Run the read leg twice (main control, then spawned) on fresh pairs.
 * spawn_out: 0 returned, 1 DID-NOT-RETURN, -1 not exercised / no thread. */
static void run_read_leg(const char *legname, int (*make)(struct pair *),
                         int *main_ret, int *spawn_out, int *refused)
{
	struct pair P;
	struct wait_result r;
	char netword[64];
	int rc;

	*main_ret = 0;
	*spawn_out = -1;
	*refused = 0;

	if (make(&P) < 0) {
		printf("  %-6s %-10s not exercised — %s\n", "main", legname, P.note);
		*refused = 1;
	} else {
		rc = read_on_main(P.rfd, P.wfd, &r, netword, sizeof(netword));
		if (rc == 0) {
			*main_ret = (r.rc >= 0);
			print_wait("main", legname, &r, 0, netword);
		}
		close(P.rfd);
		close(P.wfd);
	}

	if (make(&P) < 0) {
		printf("  %-6s %-10s not exercised — %s\n", "thread", legname,
		       P.note);
		*refused = 1;
	} else {
		rc = read_on_spawned(P.rfd, P.wfd, &r);
		if (rc < 0) {
			printf("  %-6s %-10s not exercised — no reader thread\n",
			       "thread", legname);
			close(P.rfd);
			close(P.wfd);
		} else if (rc == 1) {
			*spawn_out = 1;
			print_wait("thread", legname, &r, 1, NULL);
			/* The pair is NOT closed: the reader is still parked in
			 * read() on it, and closing the ends would wake it into a
			 * result write mid-leg. It stays parked until exit. */
		} else {
			*spawn_out = (r.rc >= 0) ? 0 : -1;
			print_wait("thread", legname, &r, 0, NULL);
			close(P.rfd);
			close(P.wfd);
		}
	}
}

static void run_poll_leg(const char *legname, int (*make)(struct pair *),
                         int *main_ret, int *spawn_ret, int *refused)
{
	struct pair P;
	struct wait_result r;
	int rc;

	*main_ret = *spawn_ret = 0;
	*refused = 0;
	g_poll_refused = 0;

	if (make(&P) < 0) {
		printf("  %-6s %-10s not exercised — %s\n", "main", legname, P.note);
		*refused = 1;
	} else {
		rc = poll_on_main(P.rfd, P.wfd, &r);
		if (r.rc < 0) g_poll_refused = 1;
		if (rc == 0) {
			*main_ret = (r.rc > 0);
			print_wait("main", legname, &r, r.rc == 0, NULL);
		}
		close(P.rfd);
		close(P.wfd);
	}

	if (make(&P) < 0) {
		printf("  %-6s %-10s not exercised — %s\n", "thread", legname,
		       P.note);
		*refused = 1;
	} else {
		rc = poll_on_spawned(P.rfd, P.wfd, &r);
		if (r.rc < 0) g_poll_refused = 1;
		if (rc == 0) {
			*spawn_ret = (r.rc > 0);
			print_wait("thread", legname, &r, r.rc == 0, NULL);
		}
		close(P.rfd);
		close(P.wfd);
	}
}

int main(void)
{
	int r_main, r_spawn, r_ref;
	int p_main, p_spawn, p_ref;
	int t_main, t_spawn, t_ref;
	struct pair probe;
	int sockpair_ok, have_env, have_sock;
	int (*read_src)(struct pair *);
	const char *src, *port, *sockpath;

	setvbuf(stdout, NULL, _IONBF, 0);
	puts("guest socket waits on spawned threads: does a blocking socket");
	puts("  wait RETURN when it is released? main-thread control + spawned");
	puts("  thread per leg; rc/errno/own-clock duration per call.");
	port = getenv(PEER_PORT_ENV);
	sockpath = getenv(PEER_SOCK_ENV);
	have_env = (port && *port);
	have_sock = (sockpath && *sockpath);
	printf("  peer env: unix=%s inet-port=%s\n",
	       have_sock ? sockpath : "<none>",
	       have_env ? port : "<none>");

	sockpair_ok = (make_sockpair(&probe) == 0);
	if (sockpair_ok) {
		close(probe.rfd);
		close(probe.wfd);
	}
	printf("  socketpair: %s\n",
	       sockpair_ok ? "available here"
	                   : "UNAVAILABLE (expected inside the guest: overlay"
	                     " sys_socketpair forwards to LINUX 134, which the"
	                     " trap does not define)");
	/* The source the read question is asked on when socketpair is out, in
	 * order of what this guest can actually reach: the AF_UNIX live socket
	 * first (the wayland socket's own path), then an AF_INET port (whose
	 * connect is measured broken in the guest), then the probe's own
	 * listener (bind is measured ENOSYS in the guest; works natively). */
	if (sockpair_ok) {
		read_src = make_sockpair;
		src = "socketpair";
	} else if (have_sock) {
		read_src = make_envunix;
		src = "env AF_UNIX connect+read";
	} else if (have_env) {
		read_src = make_envpeer;
		src = "env AF_INET connect+read";
	} else {
		read_src = make_tcp_pair;
		src = "self-TCP connect+read";
	}
	printf("  read source for legs 1-2: %s\n", src);
	puts("");

	puts("[leg 1] blocking read(), released by write() at the other end:");
	run_read_leg("read", read_src, &r_main, &r_spawn, &r_ref);
	puts("");

	puts("[leg 2] poll(POLLIN) on the same fd, released by the same write:");
	run_poll_leg("poll", read_src, &p_main, &p_spawn, &p_ref);
	puts("");

	puts("[leg 3] connect() to a live local port + read() (self-listener):");
	run_read_leg("conn+rd", make_tcp_pair, &t_main, &t_spawn, &t_ref);
	puts("");

	puts("VERDICT");
	if (r_ref) {
		puts("  (not exercised) neither a socketpair nor a connectable");
		puts("  local port could be had, so NO socket read was measured on");
		puts("  either thread kind. That is an instrument verdict: it says");
		puts("  nothing about whether socket waits return on spawned");
		puts("  threads.");
	} else {
		if (r_main && r_spawn == 0) {
			printf("  (B) via %s: BOTH the main-thread control and the\n", src);
			puts("  spawned thread came back from a released blocking");
			puts("  socket read, so socket READS return on spawned guest");
			puts("  threads. The roundtrip hang is therefore not about");
			puts("  socket-read resumption and narrows to the readiness/");
			puts("  multiplexing half of the event path (follows: select,");
			puts("  which the trap DOES define, and the epoll side).");
		} else if (r_main && r_spawn == 1) {
			printf("  (A) via %s: the main-thread control came back from\n", src);
			puts("  a released blocking socket read and the SPAWNED");
			puts("  thread did NOT (DID-NOT-RETURN at the deadline), so");
			puts("  socket reads specifically fail to resume on spawned");
			puts("  guest threads. That is the roundtrip hang's shape.");
		} else if (!r_main && r_spawn == 0) {
			printf("  (A') via %s: the SPAWNED thread came back and MAIN\n", src);
			puts("  did not from the same released read — an inverted");
			puts("  failure this design has no mechanism for; treat as");
			puts("  instrument trouble, not a lane verdict.");
		} else if (r_main && r_spawn < 0) {
			printf("  (unknown) via %s: the main-thread control returned\n", src);
			puts("  but the lane's reader thread could not even be created,");
			puts("  so nothing was measured on a spawned thread.");
		} else {
			printf("  (A) via %s: NEITHER main nor the spawned thread\n", src);
			puts("  came back from a released blocking socket read.");
		}
	}

	/* Supporting evidence, printed as evidence and not folded into the
	 * verdict: poll's own rc (its timeout is its bound, so rc=0 IS a
	 * DID-NOT-RETURN by construction) and the refusals that say which
	 * waits this guest does not have. */
	if (p_ref && !sockpair_ok)
		printf("  support: poll not exercised — the guest's poll(395) and"
		       " select(93) exist in the overlay table but forward to"
		       " Linux calls (pselect6/ppoll, select) the trap does not"
		       " define; runtime behaviour is only measured where a valid"
		       " fd existed\n");
	else if (g_poll_refused)
		printf("  support: poll REFUSED (rc<0) on a valid fd — the"
		       " overlay's poll/select forward to LINUX pselect6/ppoll/"
		       " select, none of which the trap defines; the readiness"
		       " half stays closed and the read legs above carry the"
		       " verdict\n");
	else
		printf("  support: poll main=%s thread=%s\n",
		       p_main ? "returned on readiness" : "timed out/DID-NOT-RETURN",
		       p_spawn ? "returned on readiness" : "timed out/DID-NOT-RETURN");
	if (!t_ref && !sockpair_ok)
		printf("  support: leg 3 (self-listener pair) main=%s thread=%s"
		       " — bind works HERE, so this is a native-side leg\n",
		       t_main ? "returned" : "did not",
		       t_spawn == 0 ? "returned" : "did not");
	else if (t_ref && !sockpair_ok)
		printf("  support: leg 3 (self-listener pair) not exercised —"
		       " bind(127.0.0.1) is ENOSYS in the guest: a live port has"
		       " to come from outside (the env peer above)\n");
	puts("  measured: socket read release on main + spawned (leg 1 source);");
	puts("  poll attempted (leg 2). NOT measured: select(93) — the in-guest");
	puts("  readiness substitute; the epoll side of the roundtrip path.");
	return 0;
}
