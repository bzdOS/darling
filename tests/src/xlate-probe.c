/* xlate-probe.c - guest check that constants crossing the host/guest boundary keep their meaning.
 *
 * Real paths only: signals delivered with kill(2) and pthread_kill(3) (guest syscall layer and the server's
 * thread signal), errno from raw syscalls answered by the host, and the server-initiated mmap/msync/mprotect
 * requests that mach_vm_allocate/mach_vm_protect/mach_vm_msync make the guest execute.
 *
 * Output: a line "XLATE-T <name> ok|bad ..." per check, then one verdict line
 *   XLATE-VERDICT pass|FAIL n=<checks> bad=<failed> [<names>]
 * A check that kills the process leaves its "XLATE-T begin <name>" line as the last one (see the runner). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <setjmp.h>
#include <pthread.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <mach/mach.h>
#include <mach/mach_vm.h>

extern kern_return_t mach_vm_msync(vm_map_t, mach_vm_address_t, mach_vm_size_t, vm_sync_t);

static int n, nbad;
static char badlist[512];

static void result(const char *name, int ok, const char *detail)
{
	n++;
	if (!ok) {
		nbad++;
		if (strlen(badlist) + strlen(name) + 2 < sizeof(badlist)) { strcat(badlist, name); strcat(badlist, ","); }
	}
	printf("XLATE-T %s %s %s\n", name, ok ? "ok" : "bad", detail ? detail : "");
	fflush(stdout);
}
static void begin(const char *name) { printf("XLATE-T begin %s\n", name); fflush(stdout); }

/* ---- signals ---- */
static volatile int got_sig;
static volatile pthread_t got_thread;
static void handler(int s) { got_sig = s; got_thread = pthread_self(); }

static void sig_kill(const char *name, int sig)
{
	char d[64];
	struct sigaction sa; memset(&sa, 0, sizeof sa);
	sa.sa_handler = handler; sigemptyset(&sa.sa_mask);
	sigaction(sig, &sa, NULL);
	got_sig = 0;
	begin(name);
	kill(getpid(), sig);
	for (int i = 0; i < 200 && !got_sig; i++) usleep(1000);
	snprintf(d, sizeof d, "sent=%d got=%d", sig, got_sig);
	result(name, got_sig == sig, d);
	signal(sig, SIG_DFL);
}

static volatile int target_ready, target_stop;
static void *target(void *a)
{
	target_ready = 1;
	while (!target_stop) usleep(1000);
	return NULL;
}

static void sig_pthread_kill(const char *name, int sig, int to_other)
{
	char d[96];
	pthread_t t = pthread_self();
	pthread_t th = 0;
	struct sigaction sa; memset(&sa, 0, sizeof sa);
	sa.sa_handler = handler; sigemptyset(&sa.sa_mask);
	sigaction(sig, &sa, NULL);
	got_sig = 0; got_thread = 0;
	if (to_other) {
		target_ready = 0; target_stop = 0;
		pthread_create(&th, NULL, target, NULL);
		while (!target_ready) usleep(1000);
		t = th;
	}
	begin(name);
	int rc = pthread_kill(t, sig);
	for (int i = 0; i < 300 && !got_sig; i++) usleep(1000);
	int where_ok = to_other ? (got_thread == th) : (got_thread == pthread_self());
	snprintf(d, sizeof d, "rc=%d sent=%d got=%d thread-ok=%d", rc, sig, got_sig, where_ok);
	result(name, rc == 0 && got_sig == sig && where_ok, d);
	if (to_other) { target_stop = 1; pthread_join(th, NULL); }
	signal(sig, SIG_DFL);
}

/* ---- errno ---- */
static void errno_check(const char *name, int rc, int want)
{
	char d[64];
	int e = errno;
	snprintf(d, sizeof d, "rc=%d errno=%d want=%d", rc, e, want);
	result(name, rc < 0 && e == want, d);
}

static int wd_fd = -1;
static void *watchdog(void *a)
{
	/* unblocks a read that should have returned EAGAIN, so a translation bug fails instead of hanging */
	sleep(3);
	if (wd_fd >= 0) { char c = 'x'; write(wd_fd, &c, 1); }
	return NULL;
}

static void errno_checks(void)
{
	int p[2], rc;
	char b;

	/* ENOTSOCK (host 38, Linux 88): a socket call on a pipe end */
	if (pipe(p) == 0) {
		begin("errno-ENOTSOCK");
		errno = 0; errno_check("errno-ENOTSOCK", shutdown(p[0], SHUT_RD), ENOTSOCK);
		close(p[0]); close(p[1]);
	}

	/* EAGAIN (host 35, Linux 11): non-blocking read of an empty pipe; also checks fcntl F_SETFL/F_GETFL */
	if (pipe(p) == 0) {
		int fl = fcntl(p[0], F_GETFL, 0);
		int sr = fcntl(p[0], F_SETFL, fl | O_NONBLOCK);
		int fl2 = fcntl(p[0], F_GETFL, 0);
		char d[64];
		snprintf(d, sizeof d, "setfl=%d getfl-nonblock=%d", sr, (fl2 & O_NONBLOCK) != 0);
		result("fcntl-nonblock-roundtrip", sr == 0 && (fl2 & O_NONBLOCK), d);
		pthread_t wt; wd_fd = p[1];
		pthread_create(&wt, NULL, watchdog, NULL);
		begin("errno-EAGAIN");
		errno = 0;
		rc = (int)read(p[0], &b, 1);
		int e = errno;
		wd_fd = -1;
		errno = e;
		errno_check("errno-EAGAIN", rc, EAGAIN);
		pthread_detach(wt);
		close(p[0]); close(p[1]);
	}

	/* ECONNREFUSED (host 61, Linux 111): connect to a closed loopback port */
	{
		int s = socket(AF_INET, SOCK_STREAM, 0);
		struct sockaddr_in sa; memset(&sa, 0, sizeof sa);
		sa.sin_family = AF_INET; sa.sin_port = htons(1); sa.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
		begin("errno-ECONNREFUSED");
		errno = 0;
		rc = connect(s, (struct sockaddr *)&sa, sizeof sa);
		errno_check("errno-ECONNREFUSED", rc, ECONNREFUSED);
		close(s);
	}
}

/* ---- server-initiated memory requests ---- */
static sigjmp_buf jb;
static void segv(int s) { siglongjmp(jb, 1); }

static void mem_checks(void)
{
	mach_vm_address_t a = 0;
	mach_vm_size_t ps = (mach_vm_size_t)getpagesize(), sz = 4 * ps;
	kern_return_t kr;
	char d[96];

	begin("s2c-mmap-anywhere");
	kr = mach_vm_allocate(mach_task_self(), &a, sz, VM_FLAGS_ANYWHERE);
	snprintf(d, sizeof d, "kr=%d", kr);
	result("s2c-mmap-anywhere", kr == KERN_SUCCESS && a != 0, d);
	if (kr != KERN_SUCCESS) return;
	memset((void *)a, 0x5a, sz);

	begin("s2c-msync-sync");
	kr = mach_vm_msync(mach_task_self(), a, sz, VM_SYNC_SYNCHRONOUS);
	snprintf(d, sizeof d, "kr=%d", kr);
	result("s2c-msync-sync", kr == KERN_SUCCESS, d);
	begin("s2c-msync-async");
	kr = mach_vm_msync(mach_task_self(), a, sz, VM_SYNC_ASYNCHRONOUS);
	snprintf(d, sizeof d, "kr=%d", kr);
	result("s2c-msync-async", kr == KERN_SUCCESS, d);
	begin("s2c-msync-invalidate");
	kr = mach_vm_msync(mach_task_self(), a, sz, VM_SYNC_SYNCHRONOUS | VM_SYNC_INVALIDATE);
	snprintf(d, sizeof d, "kr=%d", kr);
	result("s2c-msync-invalidate", kr == KERN_SUCCESS, d);

	begin("s2c-mprotect-ro");
	kr = mach_vm_protect(mach_task_self(), a + ps, ps, FALSE, VM_PROT_READ);
	int faulted = 0;
	if (kr == KERN_SUCCESS) {
		struct sigaction sa; memset(&sa, 0, sizeof sa);
		sa.sa_handler = segv; sigemptyset(&sa.sa_mask);
		sigaction(SIGSEGV, &sa, NULL); sigaction(SIGBUS, &sa, NULL);
		if (sigsetjmp(jb, 1) == 0) { *(volatile char *)(a + ps) = 1; } else { faulted = 1; }
		signal(SIGSEGV, SIG_DFL); signal(SIGBUS, SIG_DFL);
	}
	snprintf(d, sizeof d, "kr=%d write-faulted=%d", kr, faulted);
	result("s2c-mprotect-ro", kr == KERN_SUCCESS && faulted, d);
	begin("s2c-mprotect-rw");
	kr = mach_vm_protect(mach_task_self(), a + ps, ps, FALSE, VM_PROT_READ | VM_PROT_WRITE);
	if (kr == KERN_SUCCESS) *(volatile char *)(a + ps) = 2;
	snprintf(d, sizeof d, "kr=%d", kr);
	result("s2c-mprotect-rw", kr == KERN_SUCCESS, d);

	begin("s2c-munmap");
	kr = mach_vm_deallocate(mach_task_self(), a, sz);
	snprintf(d, sizeof d, "kr=%d", kr);
	result("s2c-munmap", kr == KERN_SUCCESS, d);

}

int main(void)
{
	printf("XLATE-T start\n"); fflush(stdout);
	mem_checks();
	errno_checks();
	sig_kill("sig-kill-SIGUSR1", SIGUSR1);
	sig_kill("sig-kill-SIGUSR2", SIGUSR2);
	sig_kill("sig-kill-SIGCHLD", SIGCHLD);
	sig_kill("sig-kill-SIGURG", SIGURG);
	sig_kill("sig-kill-SIGWINCH", SIGWINCH);
	sig_kill("sig-kill-SIGTERM", SIGTERM);
	sig_pthread_kill("sig-pthread_kill-self-SIGUSR2", SIGUSR2, 0);
	sig_pthread_kill("sig-pthread_kill-other-SIGUSR1", SIGUSR1, 1);
	sig_pthread_kill("sig-pthread_kill-other-SIGCHLD", SIGCHLD, 1);
	printf("XLATE-VERDICT %s n=%d bad=%d [%s]\n", nbad ? "FAIL" : "pass", n, nbad, badlist);
	fflush(stdout);
	return nbad != 0;
}
