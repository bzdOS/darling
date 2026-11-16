/* bootstrap-probe: bootstrap port of this task, then bootstrap_check_in of a unique name and bootstrap_look_up of it.
 * Prints BOOTCHK lines and a final "BOOTCHK RESULT pass|fail ..." line. Exit 0 on pass, 1 otherwise. No headers:
 * the few Mach prototypes are declared here (all exported by libSystem). */
#include <stdio.h>
#include <unistd.h>
typedef unsigned int mp_t;
extern mp_t mach_task_self_;
extern mp_t bootstrap_port;
extern int task_get_special_port(mp_t, int, mp_t *);
extern int bootstrap_check_in(mp_t, const char *, mp_t *);
extern int bootstrap_look_up(mp_t, const char *, mp_t *);
extern int mach_port_type(mp_t, mp_t, unsigned *);
extern const char *mach_error_string(int);
#define TASK_BOOTSTRAP_PORT 4
static const char *kind(mp_t p)
{
	unsigned t = 0;
	if (p == 0) return "NULL";
	if (p == (mp_t)-1) return "DEAD_NAME(-1)";
	if (mach_port_type(mach_task_self_, p, &t)) return "unknown-to-task";
	return (t & 0x100000) ? "dead-name" : "live";
}
int main(void)
{
	mp_t bp = 0, sp = 0, lp = 0;
	char name[64];
	int kr = task_get_special_port(mach_task_self_, TASK_BOOTSTRAP_PORT, &bp);
	printf("BOOTCHK task_get_special_port kr=%d port=0x%x state=%s global=0x%x\n", kr, bp, kind(bp), bootstrap_port);
	snprintf(name, sizeof name, ".BootstrapProbe.%d", (int)getpid());
	int c = bootstrap_check_in(bp, name, &sp);
	printf("BOOTCHK check_in kr=0x%x (%s) port=0x%x\n", c, mach_error_string(c), sp);
	int l = c ? -1 : bootstrap_look_up(bp, name, &lp);
	if (!c) printf("BOOTCHK look_up kr=0x%x (%s) port=0x%x\n", l, mach_error_string(l), lp);
	int ok = !c && !l && sp && lp;
	printf("BOOTCHK RESULT %s bootstrap=%s check_in=0x%x look_up=0x%x\n", ok ? "pass" : "fail", kind(bp), c, l);
	return ok ? 0 : 1;
}
