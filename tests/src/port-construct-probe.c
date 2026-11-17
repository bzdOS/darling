/* port-construct-probe.c - mach_port_construct (RPC dserver_rpc_mach_port_construct) must always return: flag combos, explicit
 * name (mach_port_allocate_name incl. collision), many threads concurrently, with S2C pressure (vm_allocate from other threads)
 * and a mutex held across the call (as seen in the Chrome main-thread stall). A watchdog counts hung workers.
 * Output: PC-T lines and one PC-SUMMARY n=<checks> hung=<workers> bad=<names|-> */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <pthread.h>
#include <mach/mach.h>
#include <mach/mach_vm.h>
#define NT 24
#define ITERS 300
static int n, nbad; static char bad[256];
static pthread_mutex_t M = PTHREAD_MUTEX_INITIALIZER, Big = PTHREAD_MUTEX_INITIALIZER;
static volatile int done[NT], running;
static volatile long progress[NT];
static void chk(const char *nm, int ok, long v) { pthread_mutex_lock(&M); n++; if (!ok) { nbad++; if (strlen(bad) < 200) { strncat(bad, nm, 24); strcat(bad, ","); } } printf("PC-T %s %s v=%ld\n", nm, ok ? "ok" : "bad", v); fflush(stdout); pthread_mutex_unlock(&M); }
typedef struct { uint32_t flags; uint32_t qlimit; uint64_t reserved[2]; uint64_t pad[2]; } opts_t; /* >= sizeof(mach_port_options_t) */
static const uint32_t FL[] = { 0, 0x2, 0x4, 0x8, 0x10, 0x1, 0x1|0x10, 0x2|0x10, 0x4|0x10, 0x20, 0x20|0x10, 0x40, 0x80, 0x80|0x10, 0x1|0x2|0x4|0x8|0x10 };
static int do_construct(uint32_t fl, mach_port_name_t *out)
{
	opts_t o; memset(&o, 0, sizeof o); o.flags = fl; o.qlimit = 5;
	uint64_t ctx = (fl & 1) ? 0x1122334455667788ull ^ (uint64_t)(uintptr_t)out : 0;
	return mach_port_construct(mach_task_self(), (mach_port_options_t *)&o, ctx, out);
}
typedef struct { mach_msg_header_t h; mach_msg_body_t b; mach_msg_ool_descriptor_t d; } ool_msg_t;
/* queue an out-of-line message on a fresh port and destroy the port with it still queued: the server must free the OOL
 * copy (kernel-side async work that may S2C-signal an arbitrary guest thread) without wedging an in-flight RPC */
static void ool_churn(void)
{
	mach_port_name_t p; static char buf[65536]; ool_msg_t m; kern_return_t kr;
	if (do_construct(0x10, &p) != KERN_SUCCESS) return;
	memset(&m, 0, sizeof m);
	m.h.msgh_bits = MACH_MSGH_BITS_COMPLEX | MACH_MSGH_BITS(MACH_MSG_TYPE_COPY_SEND, 0);
	m.h.msgh_size = sizeof m; m.h.msgh_remote_port = p; m.h.msgh_id = 77;
	m.b.msgh_descriptor_count = 1;
	m.d.address = buf; m.d.size = sizeof buf; m.d.copy = MACH_MSG_VIRTUAL_COPY; m.d.deallocate = 0; m.d.type = MACH_MSG_OOL_DESCRIPTOR;
	kr = mach_msg(&m.h, MACH_SEND_MSG | MACH_SEND_TIMEOUT, sizeof m, 0, MACH_PORT_NULL, 200, MACH_PORT_NULL);
	(void)kr;
	mach_port_destruct(mach_task_self(), p, -1, 0);
}
static void *worker(void *a)
{
	long id = (long)a; mach_port_name_t p; int i, k;
	for (i = 0; i < ITERS; i++) {
		k = (int)((id + i) % (sizeof FL / sizeof FL[0]));
		uint32_t fl = FL[k];
		if (id % 3 == 0) pthread_mutex_lock(&Big);      /* lock held across the RPC like the Chrome stall */
		kern_return_t kr = do_construct(fl, &p);
		if (kr == KERN_SUCCESS) {
			if (fl & 1) mach_port_destruct(mach_task_self(), p, (fl & 0x10) ? -1 : 0, 0x1122334455667788ull ^ (uint64_t)(uintptr_t)&p);
			else mach_port_destruct(mach_task_self(), p, (fl & 0x10) ? -1 : 0, 0);
		}
		if (id % 3 == 0) pthread_mutex_unlock(&Big);
		if (id % 4 == 1) { mach_vm_address_t va = 0; if (mach_vm_allocate(mach_task_self(), &va, 65536, VM_FLAGS_ANYWHERE) == KERN_SUCCESS) mach_vm_deallocate(mach_task_self(), va, 65536); }
		if (id % 4 == 2) ool_churn();
		progress[id] = i + 1;
	}
	done[id] = 1; return NULL;
}
int main(void)
{
	pthread_t th[NT]; long i; int w, hung;
	/* single-thread flag matrix first, each call must return and a port must come back for non-error flags */
	for (i = 0; i < (long)(sizeof FL / sizeof FL[0]); i++) {
		mach_port_name_t p = 0; char nm[32]; kern_return_t kr = do_construct(FL[i], &p);
		snprintf(nm, sizeof nm, "flags-%x", FL[i]);
		chk(nm, kr == KERN_SUCCESS ? p != 0 : 1, (long)kr);
		if (kr == KERN_SUCCESS) mach_port_destruct(mach_task_self(), p, (FL[i] & 0x10) ? -1 : 0, (FL[i] & 1) ? (0x1122334455667788ull ^ (uint64_t)(uintptr_t)&p) : 0);
	}
	{ /* explicit name: allocate_name then collision must return KERN_NAME_EXISTS (not hang) */
		mach_port_name_t nm1 = 0x7e03, nm2 = 0x7e03; kern_return_t k1 = mach_port_allocate_name(mach_task_self(), MACH_PORT_RIGHT_RECEIVE, nm1);
		kern_return_t k2 = mach_port_allocate_name(mach_task_self(), MACH_PORT_RIGHT_RECEIVE, nm2);
		chk("name-first", k1 == KERN_SUCCESS || k1 == KERN_INVALID_VALUE || k1 == KERN_NO_SPACE, k1);
		chk("name-collide", k1 != KERN_SUCCESS || k2 == KERN_NAME_EXISTS, k2);
		if (k1 == KERN_SUCCESS) mach_port_mod_refs(mach_task_self(), nm1, MACH_PORT_RIGHT_RECEIVE, -1);
	}
	{ /* invalid options pointer / bad target must fail, not hang */
		mach_port_name_t p = 0; kern_return_t kr = mach_port_construct(mach_task_self(), (mach_port_options_t *)0, 0, &p);
		chk("bad-options-ptr", kr != KERN_SUCCESS, kr);
		kr = mach_port_construct(MACH_PORT_NULL, (mach_port_options_t *)&(opts_t){0}, 0, &p);
		chk("bad-target", kr != KERN_SUCCESS, kr);
	}
	running = 1;
	for (i = 0; i < NT; i++) pthread_create(&th[i], NULL, worker, (void *)i);
	for (w = 0; w < 40; w++) { int d = 0; for (i = 0; i < NT; i++) d += done[i]; if (d == NT) break; usleep(500000); }
	hung = 0; for (i = 0; i < NT; i++) if (!done[i]) { hung++; printf("PC-HUNG worker=%ld progress=%ld\n", i, progress[i]); }
	chk("concurrent-no-hang", hung == 0, hung);
	printf("PC-SUMMARY n=%d hung=%d bad=%s\n", n, hung, nbad ? bad : "-"); fflush(stdout);
	_exit(hung ? 3 : 0);
}
