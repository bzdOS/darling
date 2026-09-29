/*
 * crash-probe.c — a Mach-O that crashes on purpose, for exercising mldr's
 * crash handler.
 *
 * purpose:    Produce one deterministic SIGSEGV inside a guest process, from
 *             a binary small enough that running it needs nothing but mldr,
 *             darlingserver and one staged libSystem — no overlay, no 59-image
 *             closure walk, no Wayland seat, no conjure process. That is what
 *             makes the crash path testable at all: the handler used to die
 *             inside its own stack dump, and the only symptom was a crash log
 *             that stopped one line after its own header, which the big
 *             window probe's log could not tell apart from the real bug.
 * input:      argv[1] selects which way to fault ("nop" for a mapped-but-
 *             unreadable page, anything else for a null write).
 * output:     never; it faults.
 * sideEffects: writes one line to stdout before faulting, so the log shows the
 *             guest really got as far as running this code.
 *
 * Build it with build-freebsd/build-crash-probe.sh. The resulting
 * tests/crash-probe-macho is a build product and is not committed.
 */

#include <stdio.h>

/* <sys/mman.h> is deliberately NOT included. The SDK header declares these
 * through __DARWIN_ALIAS, which asks the linker for `mmap$UNIX2003` and
 * `mprotect$UNIX2003`; the overlay's libc exports the unversioned `_mmap` and
 * `_mprotect` and no `$UNIX2003` variant, so a probe built against that header
 * does not link at all. The values below are the ones the header itself
 * defines (usr/include/sys/mman.h: PROT_NONE 0, PROT_READ 1, PROT_WRITE 2,
 * MAP_PRIVATE 2, MAP_ANON 0x1000, MAP_FAILED -1) — they are xnu's, not
 * FreeBSD's, and copying them is the whole reason this file still describes
 * what it asks for. */
extern void *mmap(void *, unsigned long, int, int, int, long);
extern int mprotect(void *, unsigned long, int);

#define PROT_NONE   0x00
#define PROT_READ   0x01
#define PROT_WRITE  0x02
#define MAP_PRIVATE 0x0002
#define MAP_ANON    0x1000
#define MAP_FAILED  ((void *)-1)

int
main(int argc, char **argv)
{
    const char *how = (argc > 1) ? argv[1] : "null";

    printf("crash-probe: about to fault via %s\n", how);
    fflush(stdout);

    if (how[0] == 'n' && how[1] == 'o' && how[2] == 'p') {
        /* Map a page, then throw the permission away, then write to it.
         *
         * This is the interesting one: the address is MAPPED and the access is
         * still forbidden, so the kernel raises SEGV_ACCERR rather than
         * SEGV_MAPERR, and a diagnostic that decides readability by asking
         * whether the page is resident gets the answer "yes, of course" — it
         * has to actually be touched to find out. It is the fault shape from
         * build-freebsd/SIGSEGV-SECOND-DLOPEN.md, where the walk read straight
         * into a range the process could not read and died there.
         */
        char *p = (char *)mmap((void *)0x20000, 4096,
                               PROT_READ | PROT_WRITE,
                               MAP_PRIVATE | MAP_ANON, -1, 0);
        if (p == MAP_FAILED) {
            printf("crash-probe: mmap failed, falling back to a null write\n");
            fflush(stdout);
            *(volatile int *)0 = 1;
            return 1;
        }
        mprotect(p, 4096, PROT_NONE);
        printf("crash-probe: page %p is mapped and PROT_NONE now\n", (void *)p);
        fflush(stdout);
        *(volatile int *)p = 1;   /* SEGV_ACCERR */
        return 1;
    }

    /* Plain write through an address nothing has ever mapped: SEGV_MAPERR. */
    *(volatile int *)0x10 = 1;
    return 1;
}
