/* rendezvous-bootstrap-probe.c - distinguishes Chrome r23 rendezvous hypotheses:
 * (A) guest getppid() mismatch: child does not see parent Darwin pid;
 * (B) bootstrap namespace not shared across forked guest processes.
 * Parent prints getpid, registers a bootstrap service name carrying its pid,
 * forks; child prints getppid and bootstrap_look_up of the same name.
 * Expected (healthy): child getppid == parent pid, lookup kr=0.
 * Output: RBP lines and final "RBP RESULT pass|fail ..." */
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <sys/wait.h>
#include <stdlib.h>
#include <errno.h>

typedef unsigned int mp_t;
typedef int kern_return_t;
extern mp_t mach_task_self_;
extern mp_t bootstrap_port;
extern kern_return_t task_get_special_port(mp_t task, int which, mp_t *port);
extern kern_return_t bootstrap_check_in(mp_t bp, const char *name, mp_t *sp);
extern kern_return_t bootstrap_look_up(mp_t bp, const char *name, mp_t *sp);
extern kern_return_t mach_port_allocate(mp_t task, int right, mp_t *name);
extern kern_return_t mach_port_insert_right(mp_t task, mp_t name, mp_t poly, int disp);
extern kern_return_t mach_port_type(mp_t task, mp_t name, unsigned *ptype);
extern const char *mach_error_string(kern_return_t kr);

#define TASK_BOOTSTRAP_PORT 4
#define MACH_PORT_RIGHT_RECEIVE 1
#define MACH_MSG_TYPE_MAKE_SEND 20

static const char *kind(mp_t p)
{
	unsigned t = 0;
	if (p == 0) return "NULL";
	if (p == (mp_t)-1) return "DEAD";
	if (mach_port_type(mach_task_self_, p, &t)) return "unknown";
	return (t & 0x100000) ? "dead-name" : "live";
}

int main(void)
{
	pid_t parent_pid = getpid();
	pid_t parent_ppid = getppid();
	mp_t bp = 0, svc = 0, looked = 0;
	char name[128];
	int status = 0;
	pid_t child;

	kern_return_t kr_bp = task_get_special_port(mach_task_self_, TASK_BOOTSTRAP_PORT, &bp);
	snprintf(name, sizeof name, ".RendezvousProbe.%d", (int)parent_pid);

	printf("RBP parent pid=%d ppid=%d bootstrap=0x%x state=%s global=0x%x bp_kr=%d\n",
	       (int)parent_pid, (int)parent_ppid, bp, kind(bp), bootstrap_port, kr_bp);
	printf("RBP name=%s\n", name);
	fflush(stdout);

	kern_return_t kr_alloc = mach_port_allocate(mach_task_self_, MACH_PORT_RIGHT_RECEIVE, &svc);
	kern_return_t kr_ins = kr_alloc ? kr_alloc : mach_port_insert_right(mach_task_self_, svc, svc, MACH_MSG_TYPE_MAKE_SEND);
	kern_return_t kr_reg = kr_ins ? kr_ins : bootstrap_check_in(bp, name, &svc);
	printf("RBP register alloc=0x%x insert=0x%x check_in=0x%x (%s) svc=0x%x\n",
	       kr_alloc, kr_ins, kr_reg, kr_reg ? mach_error_string(kr_reg) : "success", svc);
	fflush(stdout);

	child = fork();
	if (child < 0) {
		printf("RBP child fork=failed errno=%d\n", (int)child);
		printf("RBP RESULT fail fork\n");
		return 1;
	}
	if (child == 0) {
		pid_t cpid = getpid();
		pid_t cppid = getppid();
		mp_t cbp = 0, cport = 0;
		kern_return_t c_bp = task_get_special_port(mach_task_self_, TASK_BOOTSTRAP_PORT, &cbp);
		kern_return_t c_lu = bootstrap_look_up(cbp ? cbp : bp, name, &cport);
		int ppid_match = (cppid == parent_pid);
		int lookup_ok = (c_lu == 0 && cport != 0);
		printf("RBP child pid=%d ppid=%d expected_ppid=%d ppid_match=%s\n",
		       (int)cpid, (int)cppid, (int)parent_pid, ppid_match ? "yes" : "no");
		printf("RBP child bootstrap=0x%x state=%s bp_kr=%d\n", cbp, kind(cbp), c_bp);
		printf("RBP child look_up kr=0x%x (%s) port=0x%x state=%s\n",
		       c_lu, c_lu ? mach_error_string(c_lu) : "success", cport, kind(cport));
		printf("RBP child verdict=%s\n",
		       (!ppid_match) ? "getppid-mismatch" : (lookup_ok ? "ok" : "namespace-not-shared"));
		fflush(stdout);
		_exit((ppid_match && lookup_ok) ? 0 : 2);
	}

	while (waitpid(child, &status, 0) < 0) {
		if (errno == EINTR) continue;
		printf("RBP waitpid failed errno=%d\n", errno);
		printf("RBP RESULT fail waitpid\n");
		return 1;
	}
	{
		int exited = WIFEXITED(status);
		int code = exited ? WEXITSTATUS(status) : -1;
		printf("RBP parent child_pid=%d exited=%s code=%d raw=0x%x\n",
		       (int)child, exited ? "yes" : "no", code, status);
		if (exited && code == 0)
			printf("RBP RESULT pass\n");
		else
			printf("RBP RESULT fail child_code=%d\n", code);
		fflush(stdout);
		return (exited && code == 0) ? 0 : 1;
	}
}
