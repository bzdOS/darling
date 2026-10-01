/* guest-wl-empty-buffer-dispatch.c — isolate the park's CONDITION: is it
 * "empty buffer at dispatch entry", is it "guest-created thread", or both?
 *
 * Run 0f00018b named the park: the decomposed roundtrip (marshal+flush ->
 * own poll -> dispatch) COMPLETES from a spawned guest thread, while the
 * opaque wl_display_roundtrip PARKS there — on a stateful session AND on a
 * fresh display — and the disassembly says the dylib's roundtrip is a lazy
 * trampoline into the NATIVE libwayland, so the park sits in native
 * libwayland's blocking read path as it runs on a guest-created thread.
 * One uncertainty remained, and this probe isolates it: the decomposed
 * lane always entered dispatch with the reply ALREADY WAITING (its own
 * guest poll did the waiting), so "guest thread" and "empty at entry"
 * were never separated. The 2x2 below separates them; every cell is
 * measured, none is assumed:
 *
 *   cell 4  full    x main    — the 0f00018b control, one line of the run.
 *   cell 3  pre-buf x thread  — own poll until readable, then dispatch:
 *                               PREDICTS return (waiting done by the probe).
 *   cell 1  empty   x thread  — marshal+flush, dispatch IMMEDIATELY, no
 *                               own poll: PREDICTS parks at the same place
 *                               0f00018b parked (the native blocking read).
 *   cell 2  empty   x main    — same as cell 1 on the main thread:
 *                               PREDICTS return. If main parks instead,
 *                               the "guest thread" attribution is WRONG
 *                               and this delivery becomes the analysis.
 *
 * "Empty at entry" means: a sync was flushed so a reply IS in flight, and
 * dispatch is entered before it can arrive (marshal+flush+dispatch take
 * microseconds; sway answers in milliseconds — the duration column
 * separates the race's winner honestly: ~0ms means the data was already
 * there, N ms means the call waited).
 *
 * The vendored backend is loaded by its guest path and its C surface is
 * dlsym'd — the same route every probe since step 5 used; native
 * libwayland's internals are NOT opened here (that is the host-side
 * slice). Fresh display per cell (a parked thread poisons its display).
 * Cell 2 runs LAST: if main parks there, the harness timeout ends the run
 * and the last unbuffered marker names the cell — №9-4's own methodology.
 *
 * Negative control kept: a connect to a name that cannot exist must
 * return NULL. File-scope contexts for every spawned lane (the abandoned
 * thread writes its result when it wakes — into static memory, never into
 * a returned stack frame). Unbuffered output; the process exits whenever
 * main does.
 */

#include <dlfcn.h>
#include <errno.h>
#include <poll.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define BOUND_MS 8000

static const char *kBackendPath =
	"/System/Library/Frameworks/AppKit.framework/Versions/C/Resources/"
	"Backends/Wayland.backend/Contents/MacOS/Wayland";

#define WL_DISPLAY_SYNC 0

struct wl_display;
struct wl_proxy;
struct wl_interface;

typedef void *(*wl_connect_fn)(const char *);
typedef int (*wl_flush_fn)(void *);
typedef int (*wl_dispatch_fn)(void *);
typedef int (*wl_get_fd_fn)(void *);
typedef void *(*wl_marshal_fn)(void *, unsigned int, const void *, ...);
typedef void *(*wl_marshal_flags_fn)(void *, uint32_t, const void *,
                                     uint32_t, int, ...);
typedef int (*wl_add_listener_fn)(void *, void (**)(void), void *);

static wl_connect_fn p_connect;
static wl_flush_fn p_flush;
static wl_dispatch_fn p_dispatch;
static wl_get_fd_fn p_get_fd;
static wl_marshal_fn p_marshal;
static wl_marshal_flags_fn p_marshal_flags;
static wl_add_listener_fn p_add_listener;
static const void *p_cb_iface;

static int load_surface(void)
{
	void *h = dlopen(kBackendPath, RTLD_LAZY);
	char *err = dlerror();

	if (h == NULL) {
		printf("  dlopen FAILED: %s\n", err ? err : "?");
		return -1;
	}
#define RES(var, name)                                                       \
	do {                                                                \
		var = (void *)dlsym(h, name);                               \
		printf("  dlsym %-24s = %p\n", name, (void *)var);          \
		if ((var) == NULL) return -1;                               \
	} while (0)
	RES(p_connect, "wl_display_connect");
	RES(p_flush, "wl_display_flush");
	RES(p_dispatch, "wl_display_dispatch");
	RES(p_get_fd, "wl_display_get_fd");
	RES(p_marshal, "wl_proxy_marshal_constructor");
	RES(p_marshal_flags, "wl_proxy_marshal_flags");
	RES(p_add_listener, "wl_proxy_add_listener");
#undef RES
	p_cb_iface = dlsym(h, "wl_callback_interface");
	printf("  dlsym %-24s = %p\n", "wl_callback_interface", p_cb_iface);
	if (p_cb_iface == NULL) return -1;
	return 0;
}

static long now_ms(void)
{
	struct timespec ts;

	if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) return 0;
	return (long)ts.tv_sec * 1000L + ts.tv_nsec / 1000000L;
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

static void cb_done(void *data, void *proxy, uint32_t callback_data)
{
	(void)proxy;
	(void)callback_data;
	*(volatile int *)data = 1;
}

/* One cell's spawned lane, file-scope for the abandoned case. */
#define ST_FLUSH    1
#define ST_POLL     2
#define ST_DISPATCH 3

struct cell {
	void *wl;
	int fd;
	int prebuffer;              /* 1: own poll before dispatch, 0: none */
	int use_flags;              /* 1: sync via wl_proxy_marshal_flags (the
	                              * form wl_display_sync uses internally),
	                              * 0: via wl_proxy_marshal_constructor */
	volatile int stage;
	volatile int returned;
	volatile int done;
	struct { int rc; int err; long ms; } s[ST_DISPATCH + 1];
};

static struct cell g_c1;   /* empty x thread */
static struct cell g_c3;   /* pre-buf x thread */
static struct cell g_c5;   /* empty x thread, flags-marshal (the sync form
                            * wl_display_sync uses — 0f00018b's roundtrip
                            * goes through it) */

static void *cell_body(void *p)
{
	struct cell *C = (struct cell *)p;
	void *impl[1];
	long t0;

	/* stage: marshal a sync + flush — a reply is now in flight */
	C->stage = ST_FLUSH;
	t0 = now_ms();
	{
		void *cb;
		if (C->use_flags)
			/* the form wl_display_sync uses internally: opcode 0
			 * (sync), interface wl_callback_interface, version 0,
			 * flags 0, trailing NULL — same call shape libwayland's
			 * own wl_display_sync makes */
			cb = p_marshal_flags(C->wl, WL_DISPLAY_SYNC,
			                      p_cb_iface, 0, 0, NULL);
		else
			cb = p_marshal(C->wl, WL_DISPLAY_SYNC, p_cb_iface, NULL);
		if (cb == NULL) {
			C->s[ST_FLUSH].rc = -1;
			C->s[ST_FLUSH].err = errno;
			C->returned = 1;
			return NULL;
		}
		impl[0] = (void (*)(void))cb_done;
		if (p_add_listener != NULL)
			p_add_listener(cb, impl, (void *)&C->done);
		C->s[ST_FLUSH].rc = p_flush(C->wl);
		C->s[ST_FLUSH].err = errno;
		C->s[ST_FLUSH].ms = now_ms() - t0;
	}

	if (C->prebuffer) {
		/* cell 3: the probe's OWN poll does the waiting — dispatch
		 * below should find the reply already readable */
		struct pollfd pfd;
		C->stage = ST_POLL;
		t0 = now_ms();
		pfd.fd = C->fd;
		pfd.events = POLLIN;
		pfd.revents = 0;
		C->s[ST_POLL].rc = poll(&pfd, 1, BOUND_MS);
		C->s[ST_POLL].err = errno;
		C->s[ST_POLL].ms = now_ms() - t0;
	}

	/* stage: dispatch — cell 1 enters it with the buffer EMPTY by
	 * construction (no poll happened; the reply is still on the wire) */
	C->stage = ST_DISPATCH;
	t0 = now_ms();
	C->s[ST_DISPATCH].rc = p_dispatch(C->wl);
	C->s[ST_DISPATCH].err = errno;
	C->s[ST_DISPATCH].ms = now_ms() - t0;

	C->returned = 1;
	return NULL;
}

static void print_cell(const char *name, struct cell *C, int completed,
                       int parked_at)
{
	printf("  %-6s marshal+flush rc=%d errno=%s over %ldms\n", name,
	       C->s[ST_FLUSH].rc,
	       C->s[ST_FLUSH].err ? strerror(C->s[ST_FLUSH].err) : "0",
	       C->s[ST_FLUSH].ms);
	if (C->prebuffer)
		printf("  %-6s own-poll      rc=%d errno=%s over %ldms\n", name,
		       C->s[ST_POLL].rc,
		       C->s[ST_POLL].err ? strerror(C->s[ST_POLL].err) : "0",
		       C->s[ST_POLL].ms);
	if (completed)
		printf("  %-6s dispatch      RETURNED rc=%d errno=%s over %ldms\n",
		       name, C->s[ST_DISPATCH].rc,
		       C->s[ST_DISPATCH].err ? strerror(C->s[ST_DISPATCH].err) : "0",
		       C->s[ST_DISPATCH].ms);
	else
		printf("  %-6s dispatch      DID-NOT-RETURN — parked at stage %d"
		       " (%s)\n", name, parked_at,
		       parked_at == ST_DISPATCH ? "inside dispatch" : "earlier");
}

/* returns 1 returned, 0 parked, -1 refused/not taken */
static int run_thread_cell(const char *name, struct cell *C, int prebuffer,
                           int use_flags)
{
	pthread_t th;

	memset(C, 0, sizeof(*C));
	C->prebuffer = prebuffer;
	C->use_flags = use_flags;
	C->wl = p_connect(NULL);
	if (C->wl == NULL) {
		printf("  %-6s connect REFUSED errno=%s — cell not taken\n", name,
		       strerror(errno));
		return -1;
	}
	C->fd = p_get_fd(C->wl);
	printf("  %-6s display=%p fd=%d%s\n", name, C->wl, C->fd,
	       prebuffer ? " (pre-buffered: own poll before dispatch)" :
	                   " (empty at entry: NO poll before dispatch)");
	if (pthread_create(&th, NULL, cell_body, C) != 0) {
		printf("  %-6s FATAL: lane not created\n", name);
		return -1;
	}
	pthread_detach(th);
	if (wait_flag(&C->returned) && C->returned) {
		print_cell(name, C, 1, 0);
		return 1;
	}
	print_cell(name, C, 0, C->stage);
	return 0;
}

/* cell 4 and cell 2 run on MAIN. Cell 2 is last and deliberately
 * unbounded: if main parks in the empty-buffer dispatch, the harness
 * timeout ends the run and this marker is the last line — the finding. */
static int run_main_cell(const char *name, int prebuffer, int use_flags)
{
	struct cell C;
	struct pollfd pfd;
	void *impl[1];
	long t0;
	int rc;

	memset(&C, 0, sizeof(C));
	C.prebuffer = prebuffer;
	C.use_flags = use_flags;
	C.wl = p_connect(NULL);
	if (C.wl == NULL) {
		printf("  %-6s connect REFUSED errno=%s — cell not taken\n", name,
		       strerror(errno));
		return -1;
	}
	C.fd = p_get_fd(C.wl);
	printf("  %-6s display=%p fd=%d (%s)\n", name, C.wl, C.fd,
	       prebuffer ? "pre-buffered" : "empty at entry");

	C.stage = ST_FLUSH;
	t0 = now_ms();
	{
		void *cb;
		if (C.use_flags)
			cb = p_marshal_flags(C.wl, WL_DISPLAY_SYNC, p_cb_iface,
			                     0, 0, NULL);
		else
			cb = p_marshal(C.wl, WL_DISPLAY_SYNC, p_cb_iface, NULL);
		if (cb == NULL) {
			printf("  %-6s marshal REFUSED errno=%s\n", name,
			       strerror(errno));
			return -1;
		}
		impl[0] = (void (*)(void))cb_done;
		if (p_add_listener != NULL)
			p_add_listener(cb, impl, (void *)&C.done);
		C.s[ST_FLUSH].rc = p_flush(C.wl);
		C.s[ST_FLUSH].err = errno;
		C.s[ST_FLUSH].ms = now_ms() - t0;
	}

	if (prebuffer) {
		C.stage = ST_POLL;
		t0 = now_ms();
		pfd.fd = C.fd;
		pfd.events = POLLIN;
		pfd.revents = 0;
		C.s[ST_POLL].rc = poll(&pfd, 1, BOUND_MS);
		C.s[ST_POLL].err = errno;
		C.s[ST_POLL].ms = now_ms() - t0;
		if (C.s[ST_POLL].rc == 0) {
			printf("  %-6s own-poll TIMED OUT — the reply never came;"
			       " dispatch would be trivially blocking\n", name);
			print_cell(name, &C, 0, ST_POLL);
			return 0;
		}
	}

	C.stage = ST_DISPATCH;
	printf("  %-6s entering dispatch (%s)\n", name,
	       prebuffer ? "reply readable, buffered path" :
	                   "buffer empty, no own poll before this");
	t0 = now_ms();
	rc = p_dispatch(C.wl);
	C.s[ST_DISPATCH].rc = rc;
	C.s[ST_DISPATCH].err = errno;
	C.s[ST_DISPATCH].ms = now_ms() - t0;
	C.returned = 1;
	print_cell(name, &C, 1, 0);
	return 1;
}

int main(void)
{
	int neg, c1, c3, c4, c2, c5, c6;

	setvbuf(stdout, NULL, _IONBF, 0);
	puts("empty-buffer x thread: the 2x2 that separates the park's condition");
	printf("  WAYLAND_DISPLAY=%s XDG_RUNTIME_DIR=%s\n",
	       getenv("WAYLAND_DISPLAY") ? getenv("WAYLAND_DISPLAY") : "<unset>",
	       getenv("XDG_RUNTIME_DIR") ? getenv("XDG_RUNTIME_DIR") : "<unset>");

	if (load_surface() != 0) {
		puts("VERDICT");
		puts("  (instrument) the backend's C surface did not resolve —"
		     " nothing measured.");
		return 0;
	}

	/* NEGATIVE CONTROL: a name that cannot exist must refuse. */
	{
		void *dead;
		printf("[negative control]\n");
		errno = 0;
		dead = p_connect("gsw-dead-nope-0000");
		if (dead == NULL) {
			neg = 1;
			printf("  connect(gsw-dead-nope-0000) REFUSED errno=%s —"
			       " the instrument distinguishes refusal from hang\n",
			       strerror(errno));
		} else {
			neg = 0;
			printf("  connect SUCCEEDED to a dead name?! %p — the"
			       " negative control FAILED\n", dead);
		}
	}

	/* cell 4 first: the control 0f00018b already answered, one line. */
	printf("[cell 4] full x main — control from 0f00018b, one line\n");
	c4 = run_main_cell("cell4", 1, 0);

	/* cell 3: pre-buffered x thread — PREDICTS return. */
	printf("[cell 3] pre-buffered x thread — PREDICTS return\n");
	c3 = run_thread_cell("cell3", &g_c3, 1, 0);

	/* cell 1: empty x thread — PREDICTS parks at dispatch. */
	printf("[cell 1] empty x thread — PREDICTS parks at dispatch\n");
	c1 = run_thread_cell("cell1", &g_c1, 0, 0);

	/* cells 5/6 (analysis additions, not in the 2x2 spec): the SAME empty
	 * shape but with the sync built via wl_proxy_marshal_flags — the exact
	 * form wl_display_sync uses internally and therefore the form 0f00018b's
	 * opaque roundtrip goes through. If cell 5 parks where cell 1 returned,
	 * the difference between the lanes is the marshal PATH, not the buffer. */
	printf("[cell 5] empty x thread, flags-marshal (the wl_display_sync"
	       " form)\n");
	c5 = run_thread_cell("cell5", &g_c5, 0, 1);
	printf("[cell 6] empty x main, flags-marshal (control for cell 5)\n");
	c6 = run_main_cell("cell6", 0, 1);

	/* cell 2 LAST and unbounded on main: PREDICTS return. If main parks
	 * here, the harness timeout ends the run and "entering dispatch" is
	 * the last marker — which IS the finding (attribution wrong). */
	printf("[cell 2] empty x main — PREDICTS return; main unbounded by"
	       " design\n");
	c2 = run_main_cell("cell2", 0, 0);

	puts("VERDICT");
	printf("  matrix: cell4(full x main)=%d cell3(prebuf x thread)=%d"
	       " cell1(empty x thread)=%d cell2(empty x main)=%d\n"
	       "          (1=returned 0=parked -1=not taken)\n",
	       c4, c3, c1, c2);
	printf("  analysis: cell5(flags x thread)=%d cell6(flags x main)=%d\n",
	       c5, c6);
	if (!neg) {
		puts("  (instrument) the negative control did not refuse, so no");
		puts("  cell below can be trusted.");
	} else if (c2 == 0) {
		puts("  (attribution WRONG) MAIN parks in the empty-buffer");
		puts("  dispatch too — the condition is NOT the guest thread.");
		puts("  The measurements above replace the 0f00018b attribution;");
		puts("  the host-side slice must explain a park that hits both");
		puts("  thread kinds.");
	} else if (c5 == 0 && c1 == 1 && c6 == 1) {
		puts("  (marshal-path) the 2x2's empty-buffer condition is NOT");
		puts("  the trigger — empty x thread returned (cell 1) — but the");
		puts("  same empty shape built via wl_proxy_marshal_flags PARKS");
		puts("  on a guest thread while returning on main (cells 5/6).");
		puts("  That is the form wl_display_sync uses internally, i.e.");
		puts("  the form 0f00018b's opaque roundtrip goes through: the");
		puts("  park lives in the VARIADIC marshal path (the assembly");
		puts("  trampoline the backend ships for it), not in the read and");
		puts("  not in the buffer. The 'guest thread' half of the");
		puts("  attribution HOLDS; the 'empty buffer' half is refuted.");
	} else if (c1 == 0 && c2 == 1 && c3 == 1 && c4 == 1) {
		puts("  (isolated — original prediction) the park needs BOTH: an");
		puts("  EMPTY buffer at dispatch entry AND a guest-created");
		puts("  thread; the 'guest-thread' attribution holds at call");
		puts("  level and the empty-at-entry half is isolated.");
	} else if (c1 == 1 && c5 == 1) {
		puts("  (refuted at call level) both marshal forms return from");
		puts("  guest threads in the empty shape — 0f00018b's park does");
		puts("  not reproduce at call level in ANY cell here. What the");
		puts("  opaque roundtrip still does that no cell does: the");
		puts("  wl_proxy_set_queue on the callback and the dispatch LOOP");
		puts("  until done. Those live inside roundtrip's own body and");
		puts("  are as far as the exported surface can name them; the");
		puts("  host-side slice takes it from there.");
	} else if (c3 == 0) {
		puts("  (mutex-shaped) pre-buffered x thread PARKED: the park");
		puts("  is not the blocking wait — dispatch takes something");
		puts("  (a lock, a booking) that fails on a guest thread even");
		puts("  when data is ready. That widens the host-side slice.");
	} else {
		puts("  (mixed) the cells disagree with every prediction; the");
		puts("  per-cell lines above ARE the measurement and no");
		puts("  summary can stand in for them.");
	}
	return 0;
}
