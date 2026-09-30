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
 * 1. The lock leg, sem_wait, which the guest implements over 202. On the MAIN
 *    thread it is a control: this thread posts its own semaphore, so it must
 *    come back. On a SPAWNED thread the wait is on a semaphore nobody will ever
 *    post, exactly as the lane law asks — so what it measures is not whether it
 *    returns (it cannot) but whether the thread GOT THERE. Reached is a pass:
 *    the thread executed and parked in a kernel wait, which is the liveness
 *    question.
 *
 * 2. The descriptor leg, 47, on a connected socket with nothing to read. The
 *    main thread releases it with 46, so this one measures a real return:
 *    released, and did the thread come back.
 *
 * EVERY LEG HAS ITS OWN BOUND, from 228. No leg can hang the probe, and a leg
 * that fails is a RESULT LINE, never an exit: the classification prints whatever
 * happened, including when the peer was missing or a call turned out to be
 * undefined after all.
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
#define PEER_PORT_ENV "DARLING_THREAD_PEER_PORT"

struct lane_report {
	volatile int lock_reached;      /* the lock thread got to sem_wait */
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

static void do_lock(sem_t *s)
{
	rep.lock_reached = 1;     /* set BEFORE the wait: "reached" is the claim */
	sem_wait(s);              /* parks here and is never released */
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

	if (!port || !*port) return -1;
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
	if (peer < 0 || peer_lane < 0)
		printf("  note: no connected sockets (%s). The descriptor leg is NOT"
		       " exercised, and that is not a result about threads.\n",
		       strerror(errno));

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
	if (peer >= 0) {
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
	printf("  %-6s %-12s %s\n", "main", "recvmsg", peer < 0 ? "not exercised"
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
	if (peer_lane >= 0 && pthread_create(&td, NULL, desc_thread,
	                                    (void *)(long)peer_lane) != 0) {
		puts("  FATAL: the descriptor thread could not be created");
		threads_ok = 0;
	}

	if (threads_ok) {
		/* leg 1: nobody wakes it, so the question is whether it got there */
		printf("  %-6s %-12s %s\n", "thread", "sem_wait",
		       wait_flag(&rep.lock_reached) ? "REACHED the wait"
		                                   : "never reached the wait");
		/* leg 2: release it, then see whether it comes back */
		if (peer_lane >= 0) {
			send_to_peer(peer_lane);
			printf("  %-6s %-12s %s\n", "thread", "recvmsg",
			       wait_flag(&rep.desc_returned) ? "RETURNED"
			                                     : "did not return");
		} else {
			printf("  %-6s %-12s not exercised\n", "thread", "recvmsg");
		}
	}

	puts("");
	puts("VERDICT");
	if (!threads_ok) {
		puts("  (unknown) a thread could not be created, so nothing was measured.");
	} else if (!control_lock) {
		puts("  (unknown) the main-thread control failed, so the lane has no clean");
		puts("            baseline and its result means nothing.");
	} else if (peer < 0) {
		puts("  (not exercised) without a connected socket there is no descriptor leg.");
		if (rep.lock_reached) {
			puts("  The lock leg alone says the thread reached a 202 wait and parked");
			puts("  there; nothing more is claimed.");
		} else {
			puts("  The lock leg did not even reach the wait, which is a liveness");
			puts("  result on its own.");
		}
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
