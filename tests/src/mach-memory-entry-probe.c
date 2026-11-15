/* mach-memory-entry-probe.c - the shared-memory sequence of Chromium's
 * platform_shared_memory_region_apple.cc, as a guest program.
 *
 *   1. mach_make_memory_entry_64(MAP_MEM_NAMED_CREATE | RW)   (create)
 *   2. mach_vm_map the entry twice (VM_FLAGS_ANYWHERE, inherit NONE)
 *   3. write through one mapping, read through the other
 *   4. mach_make_memory_entry_64(MAP_MEM_VM_SHARE | READ) of a mapping, map it
 *      read-only and read the data (the "convert to read-only" step)
 *   5. unsupported combinations return an error code
 *   6. deallocate, release the ports
 * Prints one line per step and "PROBE-OK" on success; exit code 0 / 1. */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <mach/mach.h>
#include <mach/mach_vm.h>
#include <mach/memory_object_types.h>

static int failures;
#define CHECK(cond, ...) do { if (!(cond)) { printf("FAIL: " __VA_ARGS__); printf("\n"); failures++; } } while (0)

int main(void) {
    setbuf(stdout, NULL);
    mach_vm_size_t size = 3 * 4096 + 100;      /* Chrome passes an unrounded size */
    mach_port_t entry = MACH_PORT_NULL;
    memory_object_size_t esize = size;
    kern_return_t kr = mach_make_memory_entry_64(mach_task_self(), &esize, 0,
        MAP_MEM_NAMED_CREATE | VM_PROT_READ | VM_PROT_WRITE, &entry, MACH_PORT_NULL);
    printf("create: kr=%d (%s) entry=0x%x size=%llu\n", kr, mach_error_string(kr), entry, (unsigned long long)esize);
    if (kr != KERN_SUCCESS) { printf("PROBE-FAIL\n"); return 1; }
    CHECK(esize == 4 * 4096, "size rounded to %llu", (unsigned long long)esize);

    mach_vm_address_t a1 = 0, a2 = 0;
    kr = mach_vm_map(mach_task_self(), &a1, esize, 0, VM_FLAGS_ANYWHERE, entry, 0, FALSE,
        VM_PROT_READ | VM_PROT_WRITE, VM_PROT_READ | VM_PROT_WRITE, VM_INHERIT_NONE);
    printf("map 1: kr=%d (%s) addr=0x%llx\n", kr, mach_error_string(kr), (unsigned long long)a1);
    CHECK(kr == KERN_SUCCESS, "map 1");
    kr = mach_vm_map(mach_task_self(), &a2, esize, 0, VM_FLAGS_ANYWHERE, entry, 0, FALSE,
        VM_PROT_READ | VM_PROT_WRITE, VM_PROT_READ | VM_PROT_WRITE, VM_INHERIT_NONE);
    printf("map 2: kr=%d (%s) addr=0x%llx\n", kr, mach_error_string(kr), (unsigned long long)a2);
    CHECK(kr == KERN_SUCCESS, "map 2");
    if (failures) { printf("PROBE-FAIL\n"); return 1; }
    CHECK(a1 != a2, "mappings must differ");

    /* zero filled, shared */
    CHECK(((volatile char*)a1)[0] == 0 && ((volatile char*)a2)[esize - 1] == 0, "not zero filled");
    strcpy((char*)a1, "hello through mapping one");
    ((char*)a1)[esize - 1] = 0x5a;
    printf("read via mapping 2: \"%s\" last=0x%x\n", (char*)a2, ((unsigned char*)a2)[esize - 1]);
    CHECK(strcmp((char*)a2, "hello through mapping one") == 0, "write not visible in second mapping");
    CHECK(((unsigned char*)a2)[esize - 1] == 0x5a, "last byte not visible");
    strcpy((char*)a2 + 100, "and back");
    CHECK(strcmp((char*)a1 + 100, "and back") == 0, "reverse direction");

    /* read-only copy of an existing mapping, as ConvertToReadOnly does */
    mach_port_t ro = MACH_PORT_NULL;
    memory_object_size_t rsize = esize;
    kr = mach_make_memory_entry_64(mach_task_self(), &rsize, a1,
        VM_PROT_READ | MAP_MEM_VM_SHARE, &ro, MACH_PORT_NULL);
    printf("share ro: kr=%d (%s) entry=0x%x size=%llu\n", kr, mach_error_string(kr), ro, (unsigned long long)rsize);
    CHECK(kr == KERN_SUCCESS, "VM_SHARE read-only entry");
    if (kr == KERN_SUCCESS) {
        mach_vm_address_t a3 = 0;
        kr = mach_vm_map(mach_task_self(), &a3, rsize, 0, VM_FLAGS_ANYWHERE, ro, 0, FALSE,
            VM_PROT_READ, VM_PROT_READ, VM_INHERIT_NONE);
        printf("map ro: kr=%d (%s) addr=0x%llx data=\"%s\"\n", kr, mach_error_string(kr), (unsigned long long)a3,
            kr == KERN_SUCCESS ? (char*)a3 : "");
        CHECK(kr == KERN_SUCCESS && strcmp((char*)a3, "hello through mapping one") == 0, "ro mapping data");
        mach_vm_address_t a4 = 0;
        kr = mach_vm_map(mach_task_self(), &a4, rsize, 0, VM_FLAGS_ANYWHERE, ro, 0, FALSE,
            VM_PROT_READ | VM_PROT_WRITE, VM_PROT_READ | VM_PROT_WRITE, VM_INHERIT_NONE);
        printf("map ro as rw (must fail): kr=%d (%s)\n", kr, mach_error_string(kr));
        CHECK(kr != KERN_SUCCESS, "rw map of a read-only entry succeeded");
        if (a3) mach_vm_deallocate(mach_task_self(), a3, rsize);
        mach_port_deallocate(mach_task_self(), ro);
    }

    /* offset mapping (second page) sees the same bytes */
    mach_vm_address_t a5 = 0;
    kr = mach_vm_map(mach_task_self(), &a5, 4096, 0, VM_FLAGS_ANYWHERE, entry, 4096, FALSE,
        VM_PROT_READ, VM_PROT_READ, VM_INHERIT_NONE);
    printf("map page 1: kr=%d (%s)\n", kr, mach_error_string(kr));
    if (kr == KERN_SUCCESS) {
        ((char*)a1)[4096] = 0x77;
        CHECK(((volatile unsigned char*)a5)[0] == 0x77, "offset mapping does not alias");
        mach_vm_deallocate(mach_task_self(), a5, 4096);
    } else failures++;

    /* unsupported combinations: an error code, and the server must survive */
    mach_port_t bad = MACH_PORT_NULL;
    memory_object_size_t bsize = 4096;
    kr = mach_make_memory_entry_64(mach_task_self(), &bsize, 0, MAP_MEM_VM_COPY | VM_PROT_READ, &bad, MACH_PORT_NULL);
    printf("VM_COPY: kr=%d (%s)\n", kr, mach_error_string(kr));
    CHECK(kr != KERN_SUCCESS, "VM_COPY must be refused");
    kr = mach_make_memory_entry_64(mach_task_self(), &bsize, 0, MAP_MEM_PURGABLE | VM_PROT_READ, &bad, MACH_PORT_NULL);
    printf("PURGABLE: kr=%d (%s)\n", kr, mach_error_string(kr));
    CHECK(kr != KERN_SUCCESS, "PURGABLE must be refused");
    mach_vm_address_t anon = 0;
    kr = mach_vm_allocate(mach_task_self(), &anon, 4096, VM_FLAGS_ANYWHERE);
    bsize = 4096;
    kr = mach_make_memory_entry_64(mach_task_self(), &bsize, anon, MAP_MEM_VM_SHARE | VM_PROT_READ, &bad, MACH_PORT_NULL);
    printf("VM_SHARE of plain memory: kr=%d (%s)\n", kr, mach_error_string(kr));
    CHECK(kr != KERN_SUCCESS, "VM_SHARE of unbacked memory must be refused");
    mach_vm_address_t a6 = 0;
    kr = mach_vm_map(mach_task_self(), &a6, 4096, 0, VM_FLAGS_ANYWHERE, entry, 0, TRUE,
        VM_PROT_READ, VM_PROT_READ, VM_INHERIT_NONE);
    printf("map with copy=TRUE: kr=%d (%s)\n", kr, mach_error_string(kr));
    CHECK(kr != KERN_SUCCESS, "copy map must be refused");
    kr = mach_vm_map(mach_task_self(), &a6, 8 * 4096, 0, VM_FLAGS_ANYWHERE, entry, 0, FALSE,
        VM_PROT_READ, VM_PROT_READ, VM_INHERIT_NONE);
    printf("map beyond entry size: kr=%d (%s)\n", kr, mach_error_string(kr));
    CHECK(kr != KERN_SUCCESS, "oversized map must be refused");
    if (anon) mach_vm_deallocate(mach_task_self(), anon, 4096);

    /* the mapping outlives the port */
    kr = mach_port_deallocate(mach_task_self(), entry);
    printf("deallocate entry port: kr=%d\n", kr);
    CHECK(kr == KERN_SUCCESS, "port deallocate");
    CHECK(strcmp((char*)a2, "hello through mapping one") == 0, "mapping lost after port release");

    kr = mach_vm_deallocate(mach_task_self(), a1, esize);
    CHECK(kr == KERN_SUCCESS, "deallocate 1");
    kr = mach_vm_deallocate(mach_task_self(), a2, esize);
    CHECK(kr == KERN_SUCCESS, "deallocate 2");

    /* after deallocation a fresh entry and mapping still work (stale bookkeeping) */
    mach_port_t e2 = MACH_PORT_NULL;
    memory_object_size_t s2 = 4096;
    kr = mach_make_memory_entry_64(mach_task_self(), &s2, 0, MAP_MEM_NAMED_CREATE | VM_PROT_READ | VM_PROT_WRITE, &e2, MACH_PORT_NULL);
    mach_vm_address_t b1 = 0;
    if (kr == KERN_SUCCESS)
        kr = mach_vm_map(mach_task_self(), &b1, s2, 0, VM_FLAGS_ANYWHERE, e2, 0, FALSE,
            VM_PROT_READ | VM_PROT_WRITE, VM_PROT_READ | VM_PROT_WRITE, VM_INHERIT_NONE);
    printf("second entry: kr=%d (%s) addr=0x%llx\n", kr, mach_error_string(kr), (unsigned long long)b1);
    CHECK(kr == KERN_SUCCESS, "second entry");
    if (kr == KERN_SUCCESS) { *(volatile int*)b1 = 42; mach_vm_deallocate(mach_task_self(), b1, s2); mach_port_deallocate(mach_task_self(), e2); }

    printf(failures ? "PROBE-FAIL (%d)\n" : "PROBE-OK\n", failures);
    return failures ? 1 : 0;
}
