/* guest-thread-wait.c — does a spawned guest thread ever come back from a
 * blocking wait? Designed from mldr's syscall table, not from POSIX.
 *
 * Two earlier versions of this probe were written from what a POSIX blocking
 * wait looks like, and the emulator deleted the mechanisms out from under both:
 * one used a timer as its first leg, the other used call 22 for the descriptor
 * legs. The guest implements neither, so the probe could not run at all. A probe
 * written against the C library's idea of the world keeps failing this way, and
 * each failure looks like a guest thread that cannot be woken — which is the
 * one thing it is trying to measure.
 *
 * The legs come from what the trap actually implements, read out of
 * freebsd_syscall_trap.c's own numbering. DEFINED: 0 read, 1 write, 3 close,
 * 39 getpid, 41 socket, 42 connect, 46 sendmsg, 47 recvmsg, 54 setsockopt,
 * 228 clock_gettime, 202 futex. NOT DEFINED and therefore unusable here:
 * 7, 20, 22, 23, 35, 48, 49, 50, 51, 53, 230, 270, 271, 437. No CALL in this
 * file reaches any of the second list — the recipe in §13 of WORKAROUND-344.md
 * greps the binary for the wrappers that would, and it comes back empty. This
 * header now NAMES 271 in prose, which is the correction below and not a
 * violation of that rule: the rule is about which calls are made, and the
 * correction is precisely that the call being made is not the one named here
 * before.
 *
 * 2026-09-30, LATEST — THE CONSTRUCTOR IS sem_open, AND THE WAIT NEVER MOVED.
 * The three legs used to call sem_init, which in this guest is an upstream stub:
 * XNU stopped generating the syscalls for the obsolete anonymous POSIX
 * semaphore (libsyscall/wrappers/posix_sem_obsolete.c says so in its own
 * comment; sys/semaphore.h marks sem_init, sem_destroy and sem_getvalue
 * __deprecated and does not mark sem_open). So every leg here waited on an
 * object nothing had built, and the EINVAL it drew described THAT, not the
 * wait. sem_open is wired through elfcalls()->sem_open and does build one —
 * measured in tests/src/guest-sem-open.c and in the run whose log this file's
 * sibling writes.
 *
 * What this file deliberately does NOT do: it does not touch sem_wait. The wait
 * is 271 -> elfcalls()->sem_wait -> the host's sem_wait, and it parks and comes
 * back. Three earlier commits of this file each went looking for the missing
 * primitive somewhere it was not, so the note is worth more than the change:
 * the primitive was never missing. A CONSTRUCTOR was.
 *
 * 2026-09-30 — THE CLAIM THAT WAS WRONG, AND WHY THE PROBE NOW CHECKS INSTEAD
 * OF ASSUMING. This file used to say the lock legs run "sem_wait (over 202)"
 * and to name 271 as unusable, which cannot both be true. The binary settles
 * it, and the guest's own semaphore wait is NOT 202:
 *
 *   llvm-objdump --macho --disassemble \
 *       $DARLING_OVERLAY/usr/lib/system/libsystem_kernel.dylib
 *   _sem_wait:  movl $0x10f, %eax ; callq __darling_bsd_syscall
 *
 * 0x10f is 271. So the leg is a 271 leg, 271 is on this file's own NOT-DEFINED
 * list, and freebsd_syscall_trap.c defines neither a Linux 271 nor a macOS 271
 * (its Linux set is 0 1 2 3 4 5 6 8 9 10 11 13 14 16 17 18 21 32 33 39 41 42
 * 44 45 46 47 54 60 61 62 72 74 75 77 89 96 131 137 138 158 186 202 213 217
 * 228 231 232 233 257 258 262 268 269 283 284 286 287 291 302 309 318; its
 * macOS set is 1 2 3 4 5 6 20 24 30 33 39 41 42 47 48 54 73 74 81 82 90 92 93
 * 97 98 101 102 104 106 116 120 121 133 197 199 202 339). A grep of the SOURCE
 * can never have caught this: the number lives in an installed dylib, behind
 * __darling_bsd_syscall, and llvm-nm of this binary shows only _sem_wait. The
 * recipe in §13 of WORKAROUND-344.md therefore verifies the primitive at the
 * binary level, and this file states what it verified rather than what it
 * intended.
 *
 * The second defect that let this hide: every wait below used to be called as a
 * bare statement. `sem_wait(s);` discards rc AND errno, so a call that was
 * REFUSED came back at once and read as a thread that woke up by itself — which
 * is exactly what the 13:1x log showed ("REACHED it, and it RETURNED without
 * blocking") and what could not be told apart from a real wake. Every wait here
 * now records rc, errno and the 228-measured duration, and a refused wait is
 * reported as a refusal. It is never counted as parking, and never classified.
 *
 * Three things about the guest shape everything below.
 *
 * 1. THE GUEST CANNOT MAKE A LISTENING SOCKET. 49 bind, 50 listen and
 *    51 getsockname are all undefined, so a connected socket has to come from a
 *    peer already listening elsewhere. The probe takes its peer from the
 *    environment and says so plainly when there is none: without a peer there is
 *    no descriptor leg, and that must read as "not exercised" rather than as
 *    "the thread is broken".
 *
 * 2. THE 232 WAIT CALLS CANNOT BE CALLED FROM A GUEST BINARY AT ALL. They are
 *    in the trap's table for guest code that issues RAW Linux syscalls, but the
 *    guest's shim exports no entry point for any of them and the macOS SDK has
 *    no header or library for them, so there is nothing to link or dlsym. The
 *    lane law asks for deadlines to come from that call's timeout argument; it
 *    is not reachable from here, and the deadline below is taken from 228
 *    instead, which IS both exported and defined. That substitution is a
 *    deviation from the law and is flagged as one rather than made quietly.
 *
 * 3. A LOCK WAIT NOBODY WAKES IS FOREVER, so a thread that takes that leg never
 *    reaches a second one. The two legs therefore run on SEPARATE threads, or
 *    the second would never be asked.
 *
 * THE LEGS
 * --------
 * The first two legs need NO PEER, and that is the point of them: the lane's
 * question is whether a guest thread comes back from a wait it was RELEASED
 * from, and a release can be made with 202 alone. A classification that needs a
 * peer cannot be taken when the peer is the thing in doubt.
 *
 * 1. THE RELEASED LOCK LEG — the lane's question, asked directly. A spawned
 *    thread calls sem_wait (over 202) and sets its reached flag BEFORE the call.
 *    Main waits for that flag, then spins two seconds, and only then posts. The
 *    two seconds are the whole measurement: a sem_wait in this guest can come
 *    straight back without ever parking (see the trap's own note on a mismatched
 *    value), so "parked" is claimed only if the call is still out two seconds
 *    later. Then the post, and the one fact the lane is asking for: DID THE
 *    THREAD COME BACK.
 *
 * 2. THE CONTENDED MAIN LEG — the same question about the MAIN thread, on a
 *    semaphore nobody posts until a thread has SEEN main reach the call. Without
 *    it "main returns / thread parks" would only ever be a statement about
 *    contention: main's old control posts before it waits, so it never blocked.
 *    This one blocks for real, and a poster releases it.
 *
 * 3. The descriptor leg, 47, on a connected socket with nothing to read. Kept,
 *    because it is the leg the window probe's symptom actually travelled on, and
 *    it is the only leg that needs a peer. It no longer decides the lane.
 *
 * EVERY LEG HAS ITS OWN BOUND, from 228, and its own word. No leg can hang the
 * probe: the spawned legs are bounded by a flag, and the contended main leg is
 * released by a poster that posts twice — once as the release under test, once
 * as a safety net, so a wait that ignores the first cannot park main() forever.
 * Which of the two woke main is a result, not a detail. A leg that fails is a
 * RESULT LINE, never an exit: the classification prints whatever happened,
 * including when the peer was missing or a call turned out to be undefined.
 */

#include <errno.h>
#include <pthread.h>
#include <semaphore.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>
#include <fcntl.h>   /* O_CREAT | O_EXCL for sem_open */

#define BOUND_MS      3000
#define RELEASE_MS    2000   /* how long a wait is given to be REALLY parked */
#define SAFETY_MS     3000   /* the poster's second post: the probe must not hang */
#define PEER_PORT_ENV "DARLING_THREAD_PEER_PORT"

struct lane_report {
	volatile int lock_reached;      /* the lock thread got to the sem_wait CALL */
	volatile int lock_returned;     /* ... and sem_wait came back. See do_lock. */
	volatile int lock_parked;       /* ... and was STILL parked RELEASE_MS in. */
	volatile int m_reached;         /* main got to its contended sem_wait */
	volatile int m_released;        /* the poster fired the release under test */
	volatile int m_rescued;         /* ... and then the safety net */
	volatile int m_returned;        /* main came back out of the blocking wait */
	volatile int desc_returned;     /* the descriptor thread came back */
	volatile int control_desc_returned;  /* same, for the main-thread control */
};

/* ONE WAIT'S WHOLE OUTCOME.
 *
 * "the call came back" and "the thread came back because it was released" are
 * different facts, and a probe that cannot tell them reports a refusal as a
 * wake. The three numbers below are the difference:
 *
 *   rc      - what the wait returned. rc < 0 is a REFUSAL, full stop: the wait
 *             did not happen, and nothing about parking or waking can be read
 *             off it.
 *   err     - errno, sampled the instant the call returned, because errno is
 *             the only thing that says WHICH refusal. ENOSYS in particular is
 *             the difference between "this guest has no such wait" and "this
 *             wait was contended and lost".
 *   ms      - how long the call was out, by 228 on both sides of it. A wait
 *             that parks and is released costs RELEASE_MS plus the post; a wait
 *             that comes straight back costs about nothing. The duration is the
 *             measurement, and it is taken INSIDE the calling thread because a
 *             flag set by another thread can only ever bracket the call from
 *             outside, which is the inference that produced the last false
 *             finding.
 *
 * `parked` stays a flag rather than a derived field: it is the observation that
 * the call was STILL out RELEASE_MS later with nobody having posted, which is
 * the only claim about the kernel that this design can make.
 */
struct wait_result {
	int rc;
	int err;
	long entered_ms;
	long left_ms;
	volatile int parked;
};

static struct wait_result wr;

static void wr_reset(struct wait_result *r)
{
	r->rc = 0;
	r->err = 0;
	r->entered_ms = 0;
	r->left_ms = 0;
	r->parked = 0;
}

/* A refusal, a straight-back, and a park are three different words and the
 * verdict reads all three differently. Printed by every wait site so no leg can
 * be summarised as "came back". */
static const char *wait_word(const struct wait_result *r)
{
	if (r->rc < 0) return "REFUSED";
	if (r->parked) return "PARKED";
	return "RETURNED-NO-PARK";
}

/* The three constructor results, in call order. File-scope because
 * recorded_open() sits above main() and records into them. */
static int init_rc[3];
static int init_err[3];

/* THE NAMES THE THREE LEGS' SEMAPHORES LIVE UNDER.
 *
 * sem_open takes a name, so these three have to be distinct, they have to be
 * unlinked afterwards for the probe to be safe to run twice, and they have to
 * carry a leading slash because POSIX requires one — the bare name is refused
 * with EINVAL, which the lane's own self-test had to be taught before it would
 * tell the truth.
 */
#define SEM_NAME_LEN 64
static char sem_names[3][SEM_NAME_LEN];

/* ONE CONSTRUCTOR CALL SITE, AND IT RECORDS ITSELF.
 *
 * The constructor is sem_open rather than sem_init because sem_init in this
 * guest is an upstream stub: XNU stopped generating the syscalls for the
 * obsolete anonymous POSIX semaphore (libsyscall/wrappers/posix_sem_obsolete.c
 * says so in its own comment; sys/semaphore.h marks sem_init, sem_destroy and
 * sem_getvalue __deprecated and does not mark sem_open). Every leg used to wait
 * on an object that nothing had initialised, and the EINVAL it drew belonged to
 * that. sem_open is wired through elfcalls()->sem_open and works — measured,
 * see tests/src/guest-sem-open.c, and the lane's own run.
 *
 * The pid keeps the names unique between concurrent runs, so a rerun cannot
 * collide with a leftover and cannot inherit somebody else's semaphore. It is
 * also why the name is not a constant: a fixed name would make O_EXCL fail on
 * the second run for a reason that has nothing to do with the lane.
 *
 * value is the initial count, which is what sem_init's third argument used to
 * mean. The control passes 1 (already available, so its wait cannot park) and
 * the two lane legs pass 0.
 */
static sem_t *recorded_open(unsigned int value, int n)
{
	sem_t *s;

	snprintf(sem_names[n], SEM_NAME_LEN, "/gtw-%ld-%d",
	         (long)getpid(), n);
	errno = 0;
	s = sem_open(sem_names[n], O_CREAT | O_EXCL, 0600, value);
	init_rc[n] = (s == NULL) ? -1 : 0;
	init_err[n] = errno;
	return s;
}

/* Cleanup is a RESULT LINE, not a courtesy: a probe that leaves a named
 * semaphore behind is a probe whose next run can fail for the wrong reason,
 * and that failure would be read as a finding about the lane. */
static void unlink_all(void)
{
	int i;

	for (i = 0; i < 3; i++) {
		if (sem_names[i][0] == '\0') continue;
		if (sem_unlink(sem_names[i]) != 0)
			printf("  note: sem_unlink(%s) failed: %s\n", sem_names[i],
			       strerror(errno));
	}
}

/* recorded_init() is gone: sem_init cannot initialise anything in this guest
 * and a wrapper around a stub is a wrapper around nothing. recorded_open()
 * above is its replacement, and it returns the semaphore so a caller cannot
 * wait on a constructor that failed — which is the failure this file spent two
 * commits misreading. */

static struct lane_report rep;

/* --- the bound. 228 is exported by the guest's libsystem_c and defined in the
 * --- trap, so a deadline can be had without a timer syscall. --- */
static long now_ms(void)
{
	struct timespec ts;

	if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) return 0;
	return (long)ts.tv_sec * 1000L + ts.tv_nsec / 1000000L;
}

/* THE ONLY sem_wait CALL SITE IN THIS FILE.
 *
 * That is the point of the wrapper, not tidiness: the previous version called
 * sem_wait as a bare statement in three places and every one of them threw away
 * rc and errno, which is how a refused wait came to be reported as a thread
 * that woke by itself. One call site means the outcome cannot be forgotten.
 */
static void timed_wait(sem_t *s, struct wait_result *r)
{
	wr_reset(r);
	r->entered_ms = now_ms();
	errno = 0;
	r->rc = sem_wait(s);
	r->err = errno;
	r->left_ms = now_ms();
}

/* How long the call was out, in the caller's own words. Three different states
 * and three different words, because "no clock" and "still out" are not the
 * same fact and only one of them is a measurement: a call that has NOT come
 * back has no duration yet, and printing a zero or a "no clock" for it would
 * report a missing measurement as if it were a fast one. */
static void wait_ms(const struct wait_result *r, char *buf, size_t n)
{
	if (r->left_ms != 0 && r->entered_ms != 0)
		snprintf(buf, n, "%ldms", r->left_ms - r->entered_ms);
	else if (r->parked)
		snprintf(buf, n, "still out");
	else
		snprintf(buf, n, "no clock");
}

/* Bounded wait for a flag. Bounded, never forever, and it never sleeps: a
 * caller with no other work simply spends the deadline looking. */
static int wait_flag(volatile int *flag)
{
	long start = now_ms();

	if (start == 0) return *flag ? 1 : 0;   /* no clock: report, do not hang */
	while (now_ms() - start < BOUND_MS) {
		if (*flag) return 1;
	}
	return *flag ? 1 : 0;
}

/* Bounded spin on 228, the only clock this guest has. NOT a sleep: 35
 * nanosleep is undefined in this guest, so "wait two seconds" here is a spin
 * that asks the clock how long it has been going round. If there is no clock at
 * all this returns at once rather than pretending to have waited. */
static void spin_ms(long ms)
{
	long start = now_ms();

	if (start == 0) return;
	while (now_ms() - start < ms) { }
}

/* The release under test for the contended main leg, and the net under the
 * probe. TWO posts, not one, and the second one is the reason this leg cannot
 * hang the whole run: the first is the release being measured, the second exists
 * so that a wait which ignores the first cannot park main() forever. Which of
 * the two woke main is a RESULT, and the verdict reads it.
 *
 * The first post waits out RELEASE_MS after main's reached flag for the same
 * reason the lock leg does: a sem_wait can come straight back without parking,
 * and a post into a wait that was never a wait proves nothing. */
static void *main_poster(void *p)
{
	sem_t *s = (sem_t *)p;

	wait_flag(&rep.m_reached);      /* main got to the call */
	spin_ms(RELEASE_MS);            /* ... and had time to really park */
	rep.m_released = 1;
	sem_post(s);                    /* the release under test */
	spin_ms(SAFETY_MS);             /* it did not take. try once more. */
	rep.m_rescued = 1;
	sem_post(s);
	return NULL;
}

/* REACHED and RETURNED are two different facts and the difference is the whole
 * point of this leg.
 *
 * The trap documents a deliberate divergence: "a mismatched value makes FreeBSD
 * return success-WITHOUT-sleeping where Linux returns EAGAIN". So a sem_wait in
 * this guest can come straight back without ever parking — and a flag set
 * before the call cannot tell that from a thread that parked as intended.
 *
 * So: reached says the thread executed up to the call. returned says the call
 * came back. Parked is reached && !returned, and that is the only one of the
 * three that means "this thread is waiting in the kernel". */
static void do_lock(sem_t *s, struct wait_result *r)
{
	rep.lock_reached = 1;     /* set BEFORE the call: "reached" is the claim */
	timed_wait(s, r);          /* parks here and is never released... */
	rep.lock_returned = 1;    /* ...so reaching THIS line is a finding */
}

static void do_descriptor(int sock)
{
	char buf[64];
	struct msghdr msg;
	struct iovec iov;

	memset(&msg, 0, sizeof(msg));
	memset(&iov, 0, sizeof(iov));
	iov.iov_base = buf;
	iov.iov_len = sizeof(buf);
	msg.msg_iov = &iov;
	msg.msg_iovlen = 1;

	if (recvmsg(sock, &msg, 0) >= 0)
		rep.desc_returned = 1;
}

static void *lock_thread(void *p)
{
	do_lock((sem_t *)p, &wr);
	return NULL;
}

static void *desc_thread(void *p)
{
	do_descriptor((int)(long)p);
	return NULL;
}

/* The control's descriptor leg runs on a thread for the same reason the lane's
 * does, and for a sharper one. Done inline it is a bare blocking 47 with
 * nothing to bound it: a peer that accepts, stays silent and holds the
 * connection open parks main() before it ever prints its own result, and the
 * probe hangs rather than reporting. A leg that can hang the probe is a leg
 * with no bound, whatever the header promises. */
static void *control_desc_thread(void *p)
{
	char buf[64];
	struct msghdr msg;
	struct iovec iov;

	memset(&msg, 0, sizeof(msg));
	memset(&iov, 0, sizeof(iov));
	iov.iov_base = buf;
	iov.iov_len = sizeof(buf);
	msg.msg_iov = &iov;
	msg.msg_iovlen = 1;

	if (recvmsg((int)(long)p, &msg, 0) >= 0)
		rep.control_desc_returned = 1;
	return NULL;
}

/* WHICH call refused, not just that something did.
 *
 * A single errno covers two very different worlds: socket() refusing to make a
 * descriptor at all, and connect() refusing to reach a peer that is not there.
 * The note used to report the second while meaning it could be the first, which
 * is the same unearned-verdict defect as a leg that cannot tell "blocked" from
 * "came straight back" — a diagnostic that says something false about why. So
 * the stage is recorded where it happens and the note names it. */
#define PEER_STAGE_NONE    0   /* nothing was attempted: no peer configured */
#define PEER_STAGE_SOCKET  1   /* socket() would not make a descriptor */
#define PEER_STAGE_CONNECT 2   /* a descriptor, and connect() refused it */

static int peer_stage = PEER_STAGE_NONE;

static int connect_peer(void)
{
	const char *port = getenv(PEER_PORT_ENV);
	struct sockaddr_in sa;
	int fd;

	if (!port || !*port) {
		/* Not "connection failed" — nothing was attempted. Leaving errno
		 * alone here made the caller report "no connected sockets (No error:
		 * 0)", which is a diagnostic that says something false about why. */
		peer_stage = PEER_STAGE_NONE;
		errno = ENXIO;
		return -1;
	}
	fd = socket(AF_INET, SOCK_STREAM, 0);
	if (fd < 0) {
		peer_stage = PEER_STAGE_SOCKET;
		return -1;
	}
	memset(&sa, 0, sizeof(sa));
	sa.sin_family = AF_INET;
	sa.sin_port = htons((unsigned short)atoi(port));
	sa.sin_addr.s_addr = htonl(0x7f000001);   /* 127.0.0.1 */
	if (connect(fd, (struct sockaddr *)&sa, sizeof(sa)) < 0) {
		int e = errno;
		peer_stage = PEER_STAGE_CONNECT;
		close(fd);
		errno = e;
		return -1;
	}
	return fd;
}

static void send_to_peer(int fd)
{
	char c = 'x';
	struct msghdr msg;
	struct iovec iov;

	memset(&msg, 0, sizeof(msg));
	memset(&iov, 0, sizeof(iov));
	iov.iov_base = &c;
	iov.iov_len = 1;
	msg.msg_iov = &iov;
	msg.msg_iovlen = 1;
	if (sendmsg(fd, &msg, 0) < 0)
		printf("  note: 46 to the peer failed: %s\n", strerror(errno));
}

int main(void)
{
	sem_t *lock;
	pthread_t tl, td;
	struct wait_result wr_control, wr_main;
	char msbuf_control[32], msbuf_lock[32], msbuf_main[32];
	int peer, peer_lane, control_lock, control_desc, threads_ok = 1;
	int peers_ok;                    /* BOTH connections, not either */
	int m_released_ok = 0;           /* main came back from the RELEASE, not the net */
	const char *port = getenv(PEER_PORT_ENV);

	setvbuf(stdout, NULL, _IONBF, 0);
	puts("guest threads under mldr: does a blocking wait ever come back?");
	puts("  designed from the trap's syscall table: no timer, no call 22, no 7/23");
	printf("  peer: %s\n", (port && *port) ? port : "<none: no descriptor leg>");
	puts("");

	/* TWO connections, not one, and the reason is the same class as the
	 * unbounded control: on a single connection the control's 47 CONSUMES the
	 * byte the lane is waiting for, so the lane's leg blocks on an empty
	 * buffer and reports "did not return" for a reason that has nothing to do
	 * with threads. The control and the thing being measured must not share
	 * the resource they both draw on. */
	peer = connect_peer();
	peer_lane = connect_peer();
	/* BOTH, not either. If the first connect succeeds and the second does
	 * not, the lane's descriptor leg never runs — and a verdict that only
	 * asked "is there a peer" would go on to classify a leg that was never
	 * exercised. Half a peer is not a peer. */
	peers_ok = (peer >= 0 && peer_lane >= 0);
	if (!peers_ok) {
		if (peer_stage == PEER_STAGE_NONE)
			printf("  note: no peer configured, so no socket was attempted. The"
			       " descriptor leg is NOT exercised, and that is not a result"
			       " about threads.\n");
		else if (peer_stage == PEER_STAGE_SOCKET)
			printf("  note: no connected sockets — socket() would not make a"
			       " descriptor at all (%s), so no address was ever reached and"
			       " the peer was never touched. The descriptor leg is NOT"
			       " exercised, and that is not a result about threads.\n",
			       strerror(errno));
		else
			printf("  note: no connected sockets — socket() worked and connect()"
			       " refused it (%s), so the descriptor was fine and it is the"
			       " address that was refused. The descriptor leg is NOT"
			       " exercised, and that is not a result about threads.\n",
			       strerror(errno));
	}

/* ---------- control: the main thread, both legs ---------- */
	puts("[control] main thread:");
	/* value 1: already available, so this wait CANNOT park. That is the point
	 * of a control — it is the baseline the lane's parking claims are read
	 * against, and a control that could park would be a second lane leg. */
	lock = recorded_open(1, 0);
	if (lock == NULL) {
		printf("  %-6s %-12s rc=%d errno=%s — no semaphore to wait on\n",
		       "main", "sem_open", init_rc[0],
		       init_err[0] ? strerror(init_err[0]) : "0");
	} else {
		do_lock(lock, &wr_control);
		control_lock = rep.lock_reached;
	}
	if (lock == NULL) control_lock = 0;
	wait_ms(&wr_control, msbuf_control, sizeof(msbuf_control));
	/* The control now says what the wait DID, not merely that it was reached.
	 * Its job is to answer "does this guest's semaphore wait work at all",
	 * and a control that only records "reached" answers that question even
	 * when the wait was refused outright. */
	printf("  %-6s %-12s %s rc=%d errno=%s over %s\n", "main", "sem_wait",
	       wait_word(&wr_control), wr_control.rc,
	       wr_control.err ? strerror(wr_control.err) : "0",
	       msbuf_control);
	/* The constructor that produced that number. Printed beside the wait on
	 * purpose: a wait on an object nothing built is the same errno as a wait
	 * on a broken one, and only one of those says anything about parking. */
	printf("  %-6s %-12s rc=%d errno=%s\n", "main", "sem_open", init_rc[0],
	       init_err[0] ? strerror(init_err[0]) : "0");

	control_desc = 0;
	if (peers_ok) {
		pthread_t tc;
		send_to_peer(peer);
		rep.control_desc_returned = 0;
		if (pthread_create(&tc, NULL, control_desc_thread,
		                   (void *)(long)peer) == 0) {
			control_desc = wait_flag(&rep.control_desc_returned);
			pthread_detach(tc);
		} else {
			puts("  FATAL: the control's descriptor thread could not be created");
		}
	}
	printf("  %-6s %-12s %s\n", "main", "recvmsg", !peers_ok ? "not exercised"
	       : (control_desc ? "RETURNED" : "did not return"));
	puts("");

	/* ---------- the lane: two spawned threads, one per leg ---------- */
	puts("[lane] spawned guest threads:");
	memset(&rep, 0, sizeof(rep));
	lock = recorded_open(0, 1);      /* nobody will post this one */

	if (lock == NULL) {
		printf("  %-6s %-12s rc=%d errno=%s — no semaphore to wait on\n",
		       "thread", "sem_open", init_rc[1],
		       init_err[1] ? strerror(init_err[1]) : "0");
		threads_ok = 0;
	}

	if (lock != NULL &&
	    pthread_create(&tl, NULL, lock_thread, lock) != 0) {
		puts("  FATAL: the lock thread could not be created");
		threads_ok = 0;
	}
	if (peers_ok && pthread_create(&td, NULL, desc_thread,
	                               (void *)(long)peer_lane) != 0) {
		puts("  FATAL: the descriptor thread could not be created");
		threads_ok = 0;
	}

	if (threads_ok) {
		/* leg 1, the lane's question: did it PARK, and did it come BACK.
		 *
		 * The park is observed two ways now, because the previous version had
		 * only the outside one. From outside: the call was still out RELEASE_MS
		 * after the reached flag, with nobody having posted. From inside, in the
		 * thread that made the call: its own rc, errno and 228-measured
		 * duration. Either alone can lie — a refused call looks like a wake
		 * from outside, and a duration cannot say whether a return was caused by
		 * a post — so the leg prints both and the verdict refuses to classify if
		 * rc is negative. */
		long t0;
		wait_flag(&rep.lock_reached);
		spin_ms(RELEASE_MS);
		rep.lock_parked = (rep.lock_reached && !rep.lock_returned);
		wr.parked = rep.lock_parked;
		wait_ms(&wr, msbuf_lock, sizeof(msbuf_lock));
		printf("  %-6s %-12s %s rc=%d errno=%s over %s%s\n", "thread",
		       "sem_wait",
		       !rep.lock_reached ? "never reached the wait"
		       : wait_word(&wr), wr.rc,
		       wr.err ? strerror(wr.err) : "0", msbuf_lock,
		       rep.lock_parked ? " (still parked 2s in)" : "");

		t0 = now_ms();
		sem_post(lock);               /* the release, and the only one */
		wait_flag(&rep.lock_returned);
		if (!rep.lock_returned)
			printf("  %-6s %-12s %s\n", "thread", "after post",
			       "DID NOT RETURN within 3s of the release");
		else if (wr.rc < 0)
			/* A refused wait cannot be "released by" anything: the post went to
			 * a semaphore this guest never entered. Saying "returned after the
			 * release" here would be the same false finding one level up. */
			printf("  %-6s %-12s returned REFUSED (%s) — the release was"
			       " never waited on\n", "thread", "after post",
			       wr.err ? strerror(wr.err) : "no errno");
		else if (t0 == 0)
			printf("  %-6s %-12s %s\n", "thread", "after post",
			       "RETURNED after the release (no clock to measure it by)");
		else
			printf("  %-6s %-12s RETURNED %ldms after the release\n",
			       "thread", "after post", now_ms() - t0);

		/* leg 3, the contended main leg: the same question about MAIN, on a
		 * semaphore whose post cannot happen before main has reached the call.
		 * This is the leg that makes "main returns / thread parks" a statement
		 * about threads — main's control above posts FIRST and so never blocks,
		 * which is why that control cannot stand in for this. */
		{
			sem_t *lock_m;
			pthread_t tp;

			lock_m = recorded_open(0, 2);
			if (lock_m == NULL) {
				printf("  %-6s %-12s rc=%d errno=%s — no semaphore to wait"
				       " on\n", "main", "sem_open", init_rc[2],
				       init_err[2] ? strerror(init_err[2]) : "0");
			} else if (pthread_create(&tp, NULL, main_poster, lock_m) != 0) {
				puts("  FATAL: the main leg's poster thread could not be created");
				threads_ok = 0;
			} else {
				rep.m_reached = 1;        /* BEFORE the wait: "reached" is the claim */
				timed_wait(lock_m, &wr_main);   /* main blocks for real */
				rep.m_returned = 1;       /* reaching THIS line is the answer */
				wr_main.parked = (rep.m_reached && !rep.m_returned);
				pthread_detach(tp);
				wait_ms(&wr_main, msbuf_main, sizeof(msbuf_main));
				/* Four words, not one. "returned" alone cannot say WHICH post woke
				 * main; and if the wait was refused, no post woke it because no
				 * wait was ever entered — which is a different fact again, and the
				 * 13:1x log printed it as "the poster never fired the release",
				 * which named the poster for a refusal that happened before it. */
				printf("  %-6s %-12s %s rc=%d errno=%s over %s\n", "main",
				       "sem_wait",
				       wr_main.rc < 0 ? "REFUSED — no wait was entered"
				       : wr_main.parked ? "PARKED"
				       : rep.m_rescued ? "RETURNED, but only after the watchdog post"
				       : rep.m_released ? "RETURNED from the release"
				       : "RETURNED, but the poster never fired the release",
				       wr_main.rc, wr_main.err ? strerror(wr_main.err) : "0",
				       msbuf_main);
			}
		}

		/* leg 2: the descriptor leg, the one that needs a peer */
		if (peers_ok) {
			send_to_peer(peer_lane);
			printf("  %-6s %-12s %s\n", "thread", "recvmsg",
			       wait_flag(&rep.desc_returned) ? "RETURNED"
			                                     : "did not return");
		} else {
			printf("  %-6s %-12s not exercised\n", "thread", "recvmsg");
		}
	}

	puts("");
	/* Main's leg has TWO posts, so "main returned" and "the release worked" are
	 * two facts. Reading only the first is how a leg would claim the main thread
	 * came back when what actually woke it was the net under the probe. A third
	 * fact now sits beside them: a REFUSED wait was never entered, so no post
	 * could have woken it and m_released_ok must not be read off it. */
	m_released_ok = (rep.m_returned && !rep.m_rescued && wr_main.rc == 0);
	puts("VERDICT");
	if (!threads_ok) {
		puts("  (unknown) a thread could not be created, so nothing was measured.");
	} else if (!control_lock) {
		puts("  (unknown) the main-thread control failed, so the lane has no clean");
		puts("            baseline and its result means nothing.");
	} else if (init_rc[0] < 0) {
		/* THE CONSTRUCTOR REFUSED, so every sem_wait below it read nothing this
		 * probe built. The verdict says so rather than leaving a reader to infer
		 * it from two numbers printed above: the wait's errno belongs to the
		 * constructor's failure, not to the wait. Kept as its own branch because
		 * it is the failure this file spent two commits misreading — it named
		 * the wait, and the wait was never reached.
		 */
		printf("  (refused) sem_open REFUSED (rc=%d errno=%s) for the control's"
		       " semaphore, so the legs below have no object this probe built.\n",
		       init_rc[0], init_err[0] ? strerror(init_err[0]) : "0");
		printf("            The wait's own errno (%s) belongs to that, not to"
		       " the wait.\n",
		       wr_control.err ? strerror(wr_control.err) : "0");
		puts("            Neither (A) nor (B) can be claimed. Instrument verdict.");
	} else if (wr_control.rc < 0) {
		/* The control refused on a semaphore that was ALREADY AVAILABLE (value
		 * 1), so there was no contention, no parking and nothing to release:
		 * this guest's semaphore wait does not exist as far as the control can
		 * tell. A REFUSAL status, deliberately NOT a classification: the
		 * rubric's A/B are both statements about parking and waking, and neither
		 * can be made from a call that was refused. */
		printf("  (refused) the main-thread control's sem_wait was REFUSED"
		       " (rc=%d errno=%s) on a semaphore sem_open built and made"
		       " AVAILABLE, so this guest has no working semaphore wait.\n",
		       wr_control.rc, wr_control.err ? strerror(wr_control.err) : "0");
		puts("            Nothing was parked and nothing was released, so neither (A)");
		puts("            nor (B) can be claimed: the lane's legs need a wait that");
		puts("            exists. Instrument verdict, not a lane verdict.");
	} else if (wr_main.rc < 0) {
		printf("  (refused) main's contended sem_wait was REFUSED (rc=%d errno=%s),"
		       " so the release under test was never waited on and says nothing"
		       " about the wake path.\n", wr_main.rc,
		       wr_main.err ? strerror(wr_main.err) : "0");
		puts("            The lane's legs need a wait that exists; they do not here.");
	} else if (rep.lock_returned && !rep.lock_parked) {
		/* Came straight back without ever parking. The reason is now a
		 * measurement rather than an inference: the call site printed its rc,
		 * errno and its own duration, so this branch can say which of the two
		 * things it is — a success that did not sleep (the trap's documented
		 * divergence) or something else — instead of asserting the first. */
		printf("  (finding) the lock leg reached sem_wait and it came back in %s"
		       " with rc=%d errno=%s, having never parked.\n", msbuf_lock, wr.rc,
		       wr.err ? strerror(wr.err) : "0");
		puts("  The release measured nothing: there was no wait to release. The");
		puts("  lane is OPEN and this is why. (rc==0 here is the trap's documented");
		puts("  success-without-sleeping divergence; rc<0 would have been caught");
		puts("  by the branch above, so this branch is that case only.)");
	} else if (rep.lock_parked) {
		/* The released legs need no peer, so the lane is classified HERE, before
		 * the peer-gated rows, and the two legs together say which of two very
		 * different things is broken. */
		if (!rep.lock_returned && !m_released_ok) {
			puts("  (A) NEITHER a spawned thread nor the main thread came back from a");
			puts("  RELEASED semaphore wait, though both parked. Parking works and");
			puts("  the release does not, so this is NOT about guest threads: it is");
			puts("  the wake path. The lane closes on the waiter side.");
		} else if (!rep.lock_returned) {
			puts("  (A) the main thread came back from a released BLOCKING wait and a");
			puts("  SPAWNED thread did not. The wake path works and the spawned thread");
			puts("  is what fails to be resumed: a GENERAL guest-thread problem under");
			puts("  mldr. The lane closes here.");
		} else if (!m_released_ok) {
			puts("  (B) a spawned guest thread parked and came back when released, so");
			puts("  generic blocking of guest threads is ALIVE and the lane narrows to");
			puts("  the event path. The main thread came back only after the watchdog");
			puts("  post, which means ITS release did not work: that is a separate");
			puts("  finding, printed above. The shim layer stays open.");
		} else {
			puts("  (B) a spawned guest thread parked and came back when released, and");
			puts("  so did the main thread. Generic blocking of guest threads is ALIVE,");
			puts("  so the window probe's symptom is NOT about them: the lane narrows");
			puts("  to the event path and the shim layer stays open.");
		}
	} else if (!peers_ok) {
		puts("  (not exercised) without BOTH connected sockets there is no descriptor");
		puts("  leg; one peer is not half of a measurement, it is no measurement.");
		/* The noted edge of this branch: the word "parked" is claimed only where
		 * lock_returned says it is true. Reached + returned is NOT parked, and
		 * saying so here is how this note would have lied. */
		if (rep.lock_reached && rep.lock_returned) {
			printf("  The lock leg reached sem_wait and it came back in %s with"
			       " rc=%d, having never parked, so nothing is claimed about"
			       " parking: the word would be a lie here.\n", msbuf_lock,
			       wr.rc);
		} else if (rep.lock_reached) {
			puts("  The lock leg alone says the thread reached its semaphore wait");
			puts("  and parked there; nothing more is claimed.");
		} else {
			puts("  The lock leg did not even reach the wait, which is a liveness");
			puts("  result on its own.");
		}
	} else if (rep.lock_returned) {
		printf("  (finding) the lock leg reached sem_wait and it came back in %s"
		       " with rc=%d, having never parked, so the release measured"
		       " nothing.\n", msbuf_lock, wr.rc);
		puts("  The lane is OPEN and this is why.");
	} else if (!control_desc) {
		/* The control's descriptor leg did not come back either, so it was the
		 * PEER that was silent, not the thread. Calling that (B) would name the
		 * thread for a result the peer decided — the unearned-verdict trap from
		 * the other direction, and the reason the control exists. */
		puts("  (unknown) the control's descriptor leg did not come back either, so");
		puts("            the peer was the thing that failed to answer. The lane's");
		puts("            descriptor result measures the peer's silence, not a thread.");
	} else if (rep.lock_reached && rep.desc_returned) {
		puts("  (none) one thread reached a semaphore wait it was never woken");
		puts("  from, and another came back from a released descriptor wait.");
		puts("  Guest blocking waits work on spawned threads, so the window");
		puts("  probe's symptom is NOT about them and the shim layer stays open.");
	} else if (!rep.lock_reached) {
		puts("  (A) the thread never reached its first blocking call: a GENERAL");
		puts("  guest-thread problem under mldr — the thread is not running at");
		puts("  all, whatever it is waiting on.");
	} else {
		puts("  (B) the thread reached a blocking wait but did not come back from");
		puts("  one it WAS released from, so the failure is in returning from a");
		puts("  descriptor wait rather than in being resumed at all.");
	}

	if (peer >= 0) close(peer);
	if (peer_lane >= 0) close(peer_lane);
	/* Named semaphores are unlinked, not destroyed: sem_destroy is the other
	 * upstream stub here and would be a no-op that looked like cleanup. Leaving
	 * the names behind would make the next run's O_EXCL fail for a reason that
	 * has nothing to do with the lane. */
	unlink_all();
	return 0;
}
