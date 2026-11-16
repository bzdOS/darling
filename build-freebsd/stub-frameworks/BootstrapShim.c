/* BootstrapShim - darling-bootstrap.dylib. There is no launchd in a harness-started prefix (darlingserver cannot
 * start it on the FreeBSD port, see bootstrap-check.sh), so a guest starts with TASK_BOOTSTRAP_PORT == MACH_PORT_NULL
 * and bootstrap_check_in()/bootstrap_look_up() fail with "(ipc/send) invalid destination port".
 * Inserted through DYLD_INSERT_LIBRARIES. Its constructor does nothing when the task already has a bootstrap
 * port (a child of a process that runs this server inherits that port and so talks to the same name table).
 * Otherwise it creates a receive right, installs it as the task bootstrap port and the bootstrap_port global and
 * runs a thread that answers the two launchd job-subsystem routines libbootstrap uses:
 *   check_in2 (402): fresh receive right for a new name (moved to the caller), the send right stays in the table;
 *   look_up2  (404): send right of a registered name, else BOOTSTRAP_UNKNOWN_SERVICE.
 * Every other routine gets MIG_BAD_ID. Layouts follow the generated jobUser.c (pack(4)). */
#include <mach/mach.h>
#include <mach/message.h>
#include <pthread.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

#define NAMELEN 128
#define MAXSVC 256
#define BOOTSTRAP_NAME_IN_USE 1101
#define BOOTSTRAP_UNKNOWN_SERVICE 1102
#pragma pack(push, 4)
typedef struct { mach_msg_header_t Head; NDR_record_t NDR; char servicename[NAMELEN]; uint64_t flags; } CheckInReq;
typedef struct { mach_msg_header_t Head; NDR_record_t NDR; char servicename[NAMELEN]; int targetpid; unsigned char instanceid[16]; uint64_t flags; } LookUpReq;
typedef struct { mach_msg_header_t Head; mach_msg_body_t body; mach_msg_port_descriptor_t port; NDR_record_t NDR; unsigned char instanceid[16]; } CheckInRep;
typedef struct { mach_msg_header_t Head; mach_msg_body_t body; mach_msg_port_descriptor_t port; } LookUpRep;
typedef struct { mach_msg_header_t Head; NDR_record_t NDR; kern_return_t RetCode; } ErrRep;
#pragma pack(pop)
extern mach_port_t bootstrap_port;
static struct { char name[NAMELEN]; mach_port_t send; } tab[MAXSVC];
static int ntab;
static mach_port_t srv;

static void err_reply(mach_msg_header_t *rq, int id, kern_return_t kr)
{
	ErrRep r;
	memset(&r, 0, sizeof r);
	r.Head.msgh_bits = MACH_MSGH_BITS(MACH_MSG_TYPE_MOVE_SEND_ONCE, 0);
	r.Head.msgh_size = sizeof r;
	r.Head.msgh_remote_port = rq->msgh_remote_port;
	r.Head.msgh_id = id;
	r.NDR = NDR_record;
	r.RetCode = kr;
	mach_msg(&r.Head, MACH_SEND_MSG, sizeof r, 0, MACH_PORT_NULL, MACH_MSG_TIMEOUT_NONE, MACH_PORT_NULL);
}
static int find(const char *n)
{
	for (int i = 0; i < ntab; i++) if (!strncmp(tab[i].name, n, NAMELEN)) return i;
	return -1;
}
static void port_reply(mach_msg_header_t *rq, int id, mach_port_t p, unsigned disp, int withndr)
{
	union { CheckInRep c; LookUpRep l; } u;
	memset(&u, 0, sizeof u);
	mach_msg_header_t *h = &u.c.Head;
	h->msgh_bits = MACH_MSGH_BITS_COMPLEX | MACH_MSGH_BITS(MACH_MSG_TYPE_MOVE_SEND_ONCE, 0);
	h->msgh_size = withndr ? sizeof(CheckInRep) : sizeof(LookUpRep);
	h->msgh_remote_port = rq->msgh_remote_port;
	h->msgh_id = id;
	u.c.body.msgh_descriptor_count = 1;
	u.c.port.name = p;
	u.c.port.disposition = disp;
	u.c.port.type = MACH_MSG_PORT_DESCRIPTOR;
	if (withndr) u.c.NDR = NDR_record;
	if (mach_msg(h, MACH_SEND_MSG, h->msgh_size, 0, MACH_PORT_NULL, MACH_MSG_TIMEOUT_NONE, MACH_PORT_NULL))
		if (withndr) mach_port_mod_refs(mach_task_self(), p, MACH_PORT_RIGHT_RECEIVE, -1);
}
static void *serve(void *arg)
{
	static union { mach_msg_header_t h; char b[2048]; } buf;
	(void)arg;
	for (;;) {
		memset(&buf, 0, sizeof buf);
		if (mach_msg(&buf.h, MACH_RCV_MSG | MACH_RCV_LARGE, 0, sizeof buf, srv, MACH_MSG_TIMEOUT_NONE, MACH_PORT_NULL))
			continue;
		if (buf.h.msgh_id == 402 && buf.h.msgh_size >= sizeof(CheckInReq) - 0) {
			CheckInReq *q = (CheckInReq *)&buf;
			q->servicename[NAMELEN - 1] = 0;
			if (find(q->servicename) >= 0 || ntab >= MAXSVC) { err_reply(&buf.h, 502, BOOTSTRAP_NAME_IN_USE); continue; }
			mach_port_t p;
			if (mach_port_allocate(mach_task_self(), MACH_PORT_RIGHT_RECEIVE, &p) ||
			    mach_port_insert_right(mach_task_self(), p, p, MACH_MSG_TYPE_MAKE_SEND)) { err_reply(&buf.h, 502, KERN_RESOURCE_SHORTAGE); continue; }
			strncpy(tab[ntab].name, q->servicename, NAMELEN - 1);
			tab[ntab++].send = p;
			port_reply(&buf.h, 502, p, MACH_MSG_TYPE_MOVE_RECEIVE, 1);
		} else if (buf.h.msgh_id == 404 && buf.h.msgh_size >= sizeof(LookUpReq) - 0) {
			LookUpReq *q = (LookUpReq *)&buf;
			q->servicename[NAMELEN - 1] = 0;
			int i = find(q->servicename);
			if (i < 0) err_reply(&buf.h, 504, BOOTSTRAP_UNKNOWN_SERVICE);
			else port_reply(&buf.h, 504, tab[i].send, MACH_MSG_TYPE_COPY_SEND, 0);
		} else
			err_reply(&buf.h, buf.h.msgh_id + 100, MIG_BAD_ID);
	}
	return 0;
}
__attribute__((constructor)) static void bootstrap_shim_init(void)
{
	mach_port_t cur = MACH_PORT_NULL;
	if (getenv("DARLING_NO_BOOTSTRAP_SHIM")) return;
	if (task_get_special_port(mach_task_self(), TASK_BOOTSTRAP_PORT, &cur) == KERN_SUCCESS && cur != MACH_PORT_NULL) {
		mach_port_type_t t = 0;
		if (mach_port_type(mach_task_self(), cur, &t) == KERN_SUCCESS && !(t & MACH_PORT_TYPE_DEAD_NAME)) return;
	}
	if (mach_port_allocate(mach_task_self(), MACH_PORT_RIGHT_RECEIVE, &srv)) return;
	if (mach_port_insert_right(mach_task_self(), srv, srv, MACH_MSG_TYPE_MAKE_SEND)) return;
	if (task_set_special_port(mach_task_self(), TASK_BOOTSTRAP_PORT, srv)) return;
	bootstrap_port = srv;
	pthread_t th;
	pthread_attr_t at;
	pthread_attr_init(&at);
	pthread_attr_setdetachstate(&at, PTHREAD_CREATE_DETACHED);
	pthread_create(&th, &at, serve, 0);
}
