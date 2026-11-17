/* vm-map-write-probe.c - writes after mach_vm_allocate / mach_vm_map (anonymous and named-entry,
 * several cur/max protection, copy and placement combinations) / mach_vm_protect / mach_vm_remap.
 * Each case prints "VMMAP-T <name> ok|BAD <why>"; the last line is the verdict. The first write that
 * faults is caught (SIGSEGV/SIGBUS); Darling treats a second one as fatal, so no negative tests. */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <signal.h>
#include <setjmp.h>
#include <mach/mach.h>
#include <mach/mach_vm.h>
#include <mach/memory_object_types.h>

static sigjmp_buf jb;
static void onsegv(int s) { siglongjmp(jb, 1); }
static int n, nbad;
static char badnames[1024];

/* 1 = the write (and read back) worked, 0 = it faulted */
static int try_write(volatile unsigned char *p, unsigned char v) {
    if (sigsetjmp(jb, 1)) return 0;
    *p = v;
    return *p == v;
}
static void result(const char *name, int ok, const char *why) {
    n++;
    printf("VMMAP-T %s %s %s\n", name, ok ? "ok" : "BAD", ok ? "" : why);
    if (!ok) { nbad++; if (strlen(badnames) + strlen(name) + 2 < sizeof badnames) { strcat(badnames, badnames[0] ? "," : ""); strcat(badnames, name); } }
}
#define TASK mach_task_self()
#define RW (VM_PROT_READ | VM_PROT_WRITE)
#define SZ (4 * 4096)

static mach_port_t mkentry(mach_vm_size_t sz, vm_prot_t prot) {
    mach_port_t e = MACH_PORT_NULL; memory_object_size_t s = sz;
    mach_make_memory_entry_64(TASK, &s, 0, MAP_MEM_NAMED_CREATE | prot, &e, MACH_PORT_NULL);
    return e;
}
static int wr(const char *name, mach_vm_address_t a, kern_return_t kr) {
    char why[96];
    if (kr != KERN_SUCCESS) { snprintf(why, sizeof why, "kr=%d", kr); result(name, 0, why); return 0; }
    int ok = try_write((unsigned char *)a, 0x5a) && try_write((unsigned char *)a + SZ - 1, 0x6b);
    result(name, ok, "write faulted");
    return ok;
}

int main(void) {
    setbuf(stdout, NULL);
    struct sigaction sa; memset(&sa, 0, sizeof sa);
    sa.sa_handler = onsegv; sa.sa_flags = SA_NODEFER; sigemptyset(&sa.sa_mask);
    sigaction(SIGSEGV, &sa, NULL); sigaction(SIGBUS, &sa, NULL);
    mach_vm_address_t a; kern_return_t kr;

    /* plain allocation */
    a = 0; kr = mach_vm_allocate(TASK, &a, SZ, VM_FLAGS_ANYWHERE); wr("alloc-rw", a, kr);
    a = 0; kr = mach_vm_allocate(TASK, &a, SZ, VM_FLAGS_ANYWHERE);
    if (kr == KERN_SUCCESS) kr = mach_vm_protect(TASK, a, SZ, FALSE, VM_PROT_READ);
    if (kr == KERN_SUCCESS) { kr = mach_vm_protect(TASK, a, SZ, FALSE, RW); } wr("alloc-r-then-rw", a, kr);
    a = 0; kr = mach_vm_allocate(TASK, &a, SZ, VM_FLAGS_ANYWHERE);
    if (kr == KERN_SUCCESS) kr = mach_vm_protect(TASK, a, SZ, FALSE, RW | VM_PROT_COPY); wr("alloc-prot-copy", a, kr);
    a = 0; kr = mach_vm_allocate(TASK, &a, SZ, VM_FLAGS_ANYWHERE);
    if (kr == KERN_SUCCESS) kr = mach_vm_protect(TASK, a, SZ, TRUE, VM_PROT_ALL); wr("alloc-setmax-all", a, kr);
    a = 0; kr = mach_vm_allocate(TASK, &a, SZ, VM_FLAGS_ANYWHERE);
    if (kr == KERN_SUCCESS) kr = mach_vm_protect(TASK, a + 4096, 4096, FALSE, VM_PROT_NONE);
    if (kr == KERN_SUCCESS) kr = mach_vm_protect(TASK, a + 4096, 4096, FALSE, RW);
    wr("alloc-none-then-rw-middle", a, kr);

    /* anonymous mach_vm_map */
    a = 0; kr = mach_vm_map(TASK, &a, SZ, 0, VM_FLAGS_ANYWHERE, MACH_PORT_NULL, 0, FALSE, RW, RW, VM_INHERIT_DEFAULT); wr("anonmap-rw-rw", a, kr);
    a = 0; kr = mach_vm_map(TASK, &a, SZ, 0, VM_FLAGS_ANYWHERE, MACH_PORT_NULL, 0, FALSE, RW, VM_PROT_ALL, VM_INHERIT_DEFAULT); wr("anonmap-rw-maxall", a, kr);
    a = 0; kr = mach_vm_map(TASK, &a, SZ, 0, VM_FLAGS_ANYWHERE, MACH_PORT_NULL, 0, TRUE, RW, RW, VM_INHERIT_DEFAULT); wr("anonmap-copy-rw", a, kr);
    a = 0; kr = mach_vm_map(TASK, &a, SZ, 0, VM_FLAGS_ANYWHERE, MACH_PORT_NULL, 0, FALSE, VM_PROT_READ, RW, VM_INHERIT_DEFAULT);
    if (kr == KERN_SUCCESS) kr = mach_vm_protect(TASK, a, SZ, FALSE, RW); wr("anonmap-r-maxrw-then-rw", a, kr);
    {   mach_vm_address_t f = 0; kr = mach_vm_allocate(TASK, &f, SZ, VM_FLAGS_ANYWHERE);
        if (kr == KERN_SUCCESS) { a = f; kr = mach_vm_map(TASK, &a, SZ, 0, VM_FLAGS_FIXED | VM_FLAGS_OVERWRITE, MACH_PORT_NULL, 0, FALSE, RW, RW, VM_INHERIT_DEFAULT); }
        wr("anonmap-fixed-overwrite", a, kr);
        if (kr == KERN_SUCCESS && a != f) result("anonmap-fixed-addr", 0, "address moved"); }
    {   mach_vm_address_t f = 0; kr = mach_vm_allocate(TASK, &f, SZ, VM_FLAGS_ANYWHERE);
        if (kr == KERN_SUCCESS) { mach_vm_deallocate(TASK, f, SZ); a = f; kr = mach_vm_map(TASK, &a, SZ, 0, VM_FLAGS_FIXED, MACH_PORT_NULL, 0, FALSE, RW, RW, VM_INHERIT_DEFAULT); }
        wr("anonmap-fixed-free", a, kr); }

    /* named entry mappings */
    mach_port_t e = mkentry(SZ, RW);
    if (e == MACH_PORT_NULL) result("entry-create", 0, "no entry"); else {
        a = 0; kr = mach_vm_map(TASK, &a, SZ, 0, VM_FLAGS_ANYWHERE, e, 0, FALSE, RW, RW, VM_INHERIT_NONE); wr("entry-rw-rw", a, kr);
        mach_vm_address_t a1 = a;
        a = 0; kr = mach_vm_map(TASK, &a, SZ, 0, VM_FLAGS_ANYWHERE, e, 0, FALSE, RW, RW, VM_INHERIT_DEFAULT); wr("entry-rw-rw-inherit-default", a, kr);
        a = 0; kr = mach_vm_map(TASK, &a, SZ, 0, VM_FLAGS_ANYWHERE, e, 0, FALSE, RW, RW, VM_INHERIT_COPY); wr("entry-rw-rw-inherit-copy", a, kr);
        a = 0; kr = mach_vm_map(TASK, &a, SZ, 0, VM_FLAGS_ANYWHERE, e, 0, FALSE, VM_PROT_READ, RW, VM_INHERIT_NONE);
        if (kr == KERN_SUCCESS) { kr = mach_vm_protect(TASK, a, SZ, FALSE, RW); } wr("entry-r-maxrw-then-rw", a, kr);
        a = 0; kr = mach_vm_map(TASK, &a, SZ, 0, VM_FLAGS_ANYWHERE, e, 0, FALSE, RW, RW, VM_INHERIT_NONE);
        if (kr == KERN_SUCCESS) kr = mach_vm_protect(TASK, a, SZ, FALSE, VM_PROT_READ);
        if (kr == KERN_SUCCESS) kr = mach_vm_protect(TASK, a, SZ, FALSE, RW); wr("entry-rw-r-rw", a, kr);
        a = 0; kr = mach_vm_map(TASK, &a, SZ, 0, VM_FLAGS_ANYWHERE, e, 0, FALSE, RW, RW, VM_INHERIT_NONE);
        if (kr == KERN_SUCCESS) kr = mach_vm_protect(TASK, a, SZ, TRUE, RW); wr("entry-setmax-rw", a, kr);
        a = 0; kr = mach_vm_map(TASK, &a, 4096, 0, VM_FLAGS_ANYWHERE, e, 4096, FALSE, RW, RW, VM_INHERIT_NONE);
        if (kr == KERN_SUCCESS) { int ok = try_write((unsigned char *)a, 0x33) && ((volatile unsigned char *)a1)[4096] == 0x33; result("entry-offset-rw", ok, "no write/alias"); }
        else result("entry-offset-rw", 0, "map failed");
        {   mach_vm_address_t f = 0; kr = mach_vm_allocate(TASK, &f, SZ, VM_FLAGS_ANYWHERE);
            if (kr == KERN_SUCCESS) { a = f; kr = mach_vm_map(TASK, &a, SZ, 0, VM_FLAGS_FIXED | VM_FLAGS_OVERWRITE, e, 0, FALSE, RW, RW, VM_INHERIT_NONE); }
            wr("entry-fixed-overwrite", a, kr); }
        {   mach_vm_address_t f = 0; kr = mach_vm_allocate(TASK, &f, SZ, VM_FLAGS_ANYWHERE);
            if (kr == KERN_SUCCESS) { mach_vm_deallocate(TASK, f, SZ); a = f; kr = mach_vm_map(TASK, &a, SZ, 0, VM_FLAGS_FIXED, e, 0, FALSE, RW, RW, VM_INHERIT_NONE); }
            wr("entry-fixed-free", a, kr); }
        /* VM_SHARE entry of a mapping, mapped RW, aliasing the original */
        mach_port_t s = MACH_PORT_NULL; memory_object_size_t ss = SZ;
        kr = mach_make_memory_entry_64(TASK, &ss, a1, RW | MAP_MEM_VM_SHARE, &s, MACH_PORT_NULL);
        if (kr == KERN_SUCCESS) { a = 0; kr = mach_vm_map(TASK, &a, ss, 0, VM_FLAGS_ANYWHERE, s, 0, FALSE, RW, RW, VM_INHERIT_NONE);
            wr("share-entry-rw", a, kr); } else result("share-entry-rw", 0, "make entry failed");
        /* remap */
        a = 0; kr = mach_vm_allocate(TASK, &a, SZ, VM_FLAGS_ANYWHERE);
        if (kr == KERN_SUCCESS) {
            mach_vm_address_t src = a, dst = 0; vm_prot_t cur, max;
            memset((void *)src, 0x11, SZ);
            kr = mach_vm_remap(TASK, &dst, SZ, 0, VM_FLAGS_ANYWHERE, TASK, src, TRUE, &cur, &max, VM_INHERIT_NONE);
            if (kr == KERN_SUCCESS) { int ok = try_write((unsigned char *)dst, 0x77) && ((volatile unsigned char *)src)[0] == 0x11; result("remap-copy-rw", ok, "write faulted or leaked to source"); }
            else { char w[40]; snprintf(w, sizeof w, "remap kr=%d", kr); result("remap-copy-rw", 0, w); }
        } else result("remap-copy-rw", 0, "alloc failed");
        a = 0; kr = mach_vm_map(TASK, &a, SZ, 0, VM_FLAGS_ANYWHERE, e, 0, FALSE, RW, RW, VM_INHERIT_NONE);
        if (kr == KERN_SUCCESS) {
            mach_vm_address_t src = a, dst = 0; vm_prot_t cur, max;
            kr = mach_vm_remap(TASK, &dst, SZ, 0, VM_FLAGS_ANYWHERE, TASK, src, FALSE, &cur, &max, VM_INHERIT_NONE);
            if (kr == KERN_SUCCESS) { int ok = try_write((unsigned char *)dst, 0x42) && ((volatile unsigned char *)src)[0] == 0x42; result("remap-share-rw", ok, "write faulted or not shared"); }
            else { char w[40]; snprintf(w, sizeof w, "remap kr=%d", kr); result("remap-share-rw", 0, w); }
        } else result("remap-share-rw", 0, "map failed");
    }
    printf("VMMAP-WRITE-VERDICT %s n=%d bad=[%s]\n", nbad ? "fail" : "pass", n, badnames);
    return nbad ? 1 : 0;
}
