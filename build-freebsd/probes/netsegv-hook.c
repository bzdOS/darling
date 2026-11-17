/* netsegv-hook: proof probe for SEGV (SEGV_ACCERR) at ChromeFW+0x1f00f95 (movl $0x408305dc,(%rax), rax = fault addr,
 * first store into a freshly mapped shared-memory histogram allocator of 0x80000 bytes). Records mach_make_memory_entry_64,
 * mach_vm_map, mach_vm_remap, mach_vm_protect calls in a ring buffer and dumps it plus mach_vm_region of the fault
 * address from the SIGSEGV handler. Active only with DARLING_NETSEGV_PROBE=1. Lines "NETSEGV ..." on stderr. Not a fix. */
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdio.h>
#include <stdint.h>
#include <stdarg.h>
#include <errno.h>

typedef unsigned int u32;
typedef uint64_t u64;
extern int sigaction(int, const void *, void *);
extern int pthread_threadid_np(void *, uint64_t *);
extern u32 mach_task_self_;
extern int mach_vm_region(u32, u64 *, u64 *, int, int *, u32 *, u32 *);
extern int mach_vm_map(u32, u64 *, u64, u64, int, u32, u64, int, int, int, int);
extern int mach_make_memory_entry_64(u32, u64 *, u64, int, u32 *, u32);
extern int mach_vm_protect(u32, u64, u64, int, int);
extern int mach_vm_remap(u32, u64 *, u64, u64, int, u32, u64, int, int *, int *, int);
struct my_sa { void (*h)(int, void *, void *); unsigned mask; int flags; };
static int g_on;
#define NEV 96
static struct { char k[12]; u64 a, b, c; int d, e, r; u32 t; } ev[NEV];
static volatile int nev;

static void out(const char *f, ...)
{
	char b[600]; va_list ap; va_start(ap, f);
	int n = vsnprintf(b, sizeof b, f, ap); va_end(ap);
	if (n > 0) write(2, b, n < (int)sizeof b ? n : (int)sizeof b - 1);
}
static uint64_t tid(void) { uint64_t t = 0; pthread_threadid_np(0, &t); return t; }
static void rec(const char *k, u64 a, u64 b, u64 c, int d, int e, int r)
{
	int i = __sync_fetch_and_add(&nev, 1) % NEV;
	strncpy(ev[i].k, k, 11); ev[i].k[11] = 0;
	ev[i].a = a; ev[i].b = b; ev[i].c = c; ev[i].d = d; ev[i].e = e; ev[i].r = r; ev[i].t = (u32)tid();
}
static int my_make(u32 t, u64 *sz, u64 off, int perm, u32 *h, u32 par)
{
	u64 s0 = sz ? *sz : 0;
	int r = mach_make_memory_entry_64(t, sz, off, perm, h, par);
	if (g_on) rec("make", s0, off, h ? *h : 0, perm, par, r);
	return r;
}
static int my_map(u32 t, u64 *addr, u64 size, u64 mask, int flags, u32 port, u64 off, int copy, int cur, int max, int inh)
{
	int r = mach_vm_map(t, addr, size, mask, flags, port, off, copy, cur, max, inh);
	if (g_on && port) rec("map", addr ? *addr : 0, size, port, cur, max, r);
	return r;
}
static int my_remap(u32 t, u64 *addr, u64 size, u64 mask, int flags, u32 st, u64 sa, int copy, int *cur, int *max, int inh)
{
	int r = mach_vm_remap(t, addr, size, mask, flags, st, sa, copy, cur, max, inh);
	if (g_on) rec("remap", addr ? *addr : 0, size, sa, cur ? *cur : -1, max ? *max : -1, r);
	return r;
}
static int my_prot(u32 t, u64 a, u64 sz, int setmax, int prot)
{
	int r = mach_vm_protect(t, a, sz, setmax, prot);
	if (g_on && (setmax || !(prot & 2))) rec("protect", a, sz, setmax, prot, 0, r);
	return r;
}
static void on_segv(int sig, void *info, void *uc)
{
	uintptr_t *mc = *(uintptr_t **)((char *)uc + 48);
	u64 fa = (u64)mc[2]; int i, n = nev;
	out("NETSEGV segv tid=%llu rip=%p rax=%p rsi=%p r8=%p rdi=%p r13=%p r10=%p\n", (unsigned long long)tid(), (void *)mc[18], (void *)mc[2], (void *)mc[7], (void *)mc[10], (void *)mc[6], (void *)mc[15], (void *)mc[12]);
	for (i = (n > NEV ? n - NEV : 0); i < n; i++) {
		int j = i % NEV;
		out("NETSEGV ev %s a=%llx b=%llx c=%llx d=%d e=%d r=%d t=%u\n", ev[j].k, ev[j].a, ev[j].b, ev[j].c, ev[j].d, ev[j].e, ev[j].r, ev[j].t);
	}
	{
		u64 a = fa, sz = 0; int info5[16], cnt = 9; u32 on = 0; int kr;
		memset(info5, 0, sizeof info5);
		kr = mach_vm_region(mach_task_self_, &a, &sz, 9, info5, (u32 *)&cnt, &on);
		out("NETSEGV region kr=%d start=%llx size=%llx prot=%d maxprot=%d inh=%d shared=%d\n", kr, a, sz, info5[0], info5[1], info5[2], info5[3]);
	}
	{ struct my_sa sa = { 0, 0, 0 }; sigaction(11, &sa, 0); }
}
__attribute__((constructor)) static void init(void)
{
	g_on = getenv("DARLING_NETSEGV_PROBE") != 0;
	if (!g_on) return;
	struct my_sa sa = { on_segv, 0, 0x40 };
	sigaction(11, &sa, 0);
	out("NETSEGV init pid=%d\n", (int)getpid());
}
struct interpose { void *replacement, *replacee; };
__attribute__((used, section("__DATA,__interpose"))) static struct interpose interposers[] = {
	{ (void *)my_make, (void *)mach_make_memory_entry_64 },
	{ (void *)my_map, (void *)mach_vm_map },
	{ (void *)my_remap, (void *)mach_vm_remap },
	{ (void *)my_prot, (void *)mach_vm_protect },
};
