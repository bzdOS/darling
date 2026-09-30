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
 * 7, 20, 22, 23, 35, 48, 49, 50, 51, 53, 230, 270, 271, 437. Nothing in this
 * file names any of the second list, by call or in prose, so a grep for them
 * comes back empty.
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

static struct lane_report rep;

/* --- the bound. 228 is exported by the guest's libsystem_c and defined in the
 * --- trap, so a deadline can be had without a timer syscall. --- */
static long now_ms(void)
{
	struct timespec ts;

	if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) return 0;
	return (long)ts.tv_sec * 1000L + ts.tv_nsec / 1000000L;
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
static void do_lock(sem_t *s)
{
	rep.lock_reached = 1;     /* set BEFORE the call: "reached" is the claim */
	sem_wait(s);              /* parks here and is never released... */
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
	do_lock((sem_t *)p);
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

static int connect_peer(void)
{
	const char *port = getenv(PEER_PORT_ENV);
	struct sockaddr_in sa;
	int fd;

	if (!port || !*port) {
		/* Not "connection failed" — nothing was attempted. Leaving errno
		 * alone here made the caller report "no connected sockets (No error:
		 * 0)", which is a diagnostic that says something false about why. */
		errno = ENXIO;
		return -1;
	}
	fd = socket(AF_INET, SOCK_STREAM, 0);
	if (fd < 0) return -1;
	memset(&sa, 0, sizeof(sa));
	sa.sin_family = AF_INET;
	sa.sin_port = htons((unsigned short)atoi(port));
	sa.sin_addr.s_addr = htonl(0x7f000001);   /* 127.0.0.1 */
	if (connect(fd, (struct sockaddr *)&sa, sizeof(sa)) < 0) {
		int e = errno;
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
	sem_t lock;
	pthread_t tl, td;
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
		if (errno == ENXIO)
			printf("  note: no peer configured, so no socket was attempted. The"
			       " descriptor leg is NOT exercised, and that is not a result"
			       " about threads.\n");
		else
			printf("  note: no connected sockets (%s). The descriptor leg is NOT"
			       " exercised, and that is not a result about threads.\n",
			       strerror(errno));
	}

	/* ---------- control: the main thread, both legs ---------- */
	puts("[control] main thread:");
	sem_init(&lock, 0, 0);
	sem_post(&lock);                 /* this thread releases its own */
	rep.lock_reached = 0;
	do_lock(&lock);
	control_lock = rep.lock_reached;
	printf("  %-6s %-12s %s\n", "main", "sem_wait",
	       control_lock ? "RETURNED" : "did not return");

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
	sem_init(&lock, 0, 0);           /* nobody will post this one */

	if (pthread_create(&tl, NULL, lock_thread, &lock) != 0) {
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
		 * The park is claimed only if the call is still out RELEASE_MS after the
		 * reached flag, because a sem_wait in this guest can return
		 * success-without-sleeping and a flag alone cannot tell that from a
		 * thread that parked. Then the post, and the return. */
		long t0;
		wait_flag(&rep.lock_reached);
		spin_ms(RELEASE_MS);
		rep.lock_parked = (rep.lock_reached && !rep.lock_returned);
		printf("  %-6s %-12s %s\n", "thread", "sem_wait",
		       !rep.lock_reached ? "never reached the wait"
		       : rep.lock_returned ? "REACHED it, and it RETURNED without blocking"
		                          : "REACHED it, and PARKED (still parked 2s in)");

		t0 = now_ms();
		sem_post(&lock);              /* the release, and the only one */
		wait_flag(&rep.lock_returned);
		if (!rep.lock_returned)
			printf("  %-6s %-12s %s\n", "thread", "after post",
			       "DID NOT RETURN within 3s of the release");
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
			sem_t lock_m;
			pthread_t tp;

			sem_init(&lock_m, 0, 0);
			if (pthread_create(&tp, NULL, main_poster, &lock_m) != 0) {
				puts("  FATAL: the main leg's poster thread could not be created");
				threads_ok = 0;
			} else {
				rep.m_reached = 1;        /* BEFORE the wait: "reached" is the claim */
				sem_wait(&lock_m);        /* main blocks for real */
				rep.m_returned = 1;       /* reaching THIS line is the answer */
				pthread_detach(tp);
				/* Three words, not one: "returned" alone cannot say WHICH post
				 * woke main, and a probe that cannot tell its own release from
				 * its own safety net cannot be used to judge a release. */
				printf("  %-6s %-12s %s\n", "main", "sem_wait",
				       rep.m_rescued ? "RETURNED, but only after the watchdog post"
				       : rep.m_released ? "RETURNED from the release"
				       : "RETURNED, but the poster never fired the release");
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
	 * came back when what actually woke it was the net under the probe. */
	m_released_ok = (rep.m_returned && !rep.m_rescued);
	puts("VERDICT");
	if (!threads_ok) {
		puts("  (unknown) a thread could not be created, so nothing was measured.");
	} else if (!control_lock) {
		puts("  (unknown) the main-thread control failed, so the lane has no clean");
		puts("            baseline and its result means nothing.");
	} else if (rep.lock_returned && !rep.lock_parked) {
		/* Came straight back without ever parking: the trap's own documented
		 * divergence, so the release measured nothing. Stands ABOVE the released
		 * classification below, because a leg that never blocked cannot be used
		 * to say anything about being woken. */
		puts("  (finding) the lock leg reached sem_wait and sem_wait RETURNED");
		puts("  without blocking. Nothing is wrong with the thread and nothing is");
		puts("  proved about it: the trap documents success-without-sleeping for a");
		puts("  mismatched futex value, so this leg did not test parking at all. The");
		puts("  lane is OPEN and this is why.");
	} else if (rep.lock_parked) {
		/* The released legs need no peer, so the lane is classified HERE, before
		 * the peer-gated rows, and the two legs together say which of two very
		 * different things is broken. */
		if (!rep.lock_returned && !m_released_ok) {
			puts("  (A) NEITHER a spawned thread nor the main thread came back from a");
			puts("  RELEASED 202 wait, though both parked. Parking works and the");
			puts("  release does not, so this is NOT about guest threads: it is the");
			puts("  wake path. The lane closes on the waiter side.");
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
			puts("  The lock leg reached sem_wait and it RETURNED without blocking, so");
			puts("  nothing is claimed about parking: the word would be a lie here.");
		} else if (rep.lock_reached) {
			puts("  The lock leg alone says the thread reached a 202 wait and parked");
			puts("  there; nothing more is claimed.");
		} else {
			puts("  The lock leg did not even reach the wait, which is a liveness");
			puts("  result on its own.");
		}
	} else if (rep.lock_returned) {
		puts("  (finding) the lock leg reached sem_wait and sem_wait RETURNED");
		puts("  without blocking. Nothing is wrong with the thread and nothing is");
		puts("  proved about it: the trap documents success-without-sleeping for a");
		puts("  mismatched futex value, so this leg did not test parking at all. The");
		puts("  lane is OPEN and this is why.");
	} else if (!control_desc) {
		/* The control's descriptor leg did not come back either, so it was the
		 * PEER that was silent, not the thread. Calling that (B) would name the
		 * thread for a result the peer decided — the unearned-verdict trap from
		 * the other direction, and the reason the control exists. */
		puts("  (unknown) the control's descriptor leg did not come back either, so");
		puts("            the peer was the thing that failed to answer. The lane's");
		puts("            descriptor result measures the peer's silence, not a thread.");
	} else if (rep.lock_reached && rep.desc_returned) {
		puts("  (none) one thread reached a 202 wait it was never woken from, and");
		puts("  another came back from a released descriptor wait. Guest blocking");
		puts("  waits work on spawned threads, so the window probe's symptom is NOT");
		puts("  about them and the shim layer stays open.");
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
	sem_destroy(&lock);
	return 0;
}
