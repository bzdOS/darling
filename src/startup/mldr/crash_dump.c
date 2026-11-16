/*
 * crash_dump.c — a guest stack dump that cannot take the process with it.
 *
 * purpose:    Print the interrupted context's stack for a crash handler,
 *             without ever faulting while collecting it. The handler this
 *             exists for re-raises the original signal afterwards; a
 *             diagnostic that dies first replaces a real crash with a
 *             crash inside the diagnostic and loses the whole dump.
 * input:      see crash_dump.h.
 * output:     Lines on stderr, one per word, in the
 *             `[gstack+NNNN] 0xVVVVVVVVVVVVVVVV` shape that every crash-log
 *             reader (build-freebsd/decode-crash.py) already parses. Slots
 *             that are not safely readable are printed as `(unreadable)`
 *             rather than skipped, so the dump is always as long as it was
 *             asked to be and the gaps are visible instead of inferred.
 * sideEffects:
 *   - Installs SIGSEGV/SIGBUS handlers for the duration of each guarded read
 *     and restores the previous ones afterwards.
 *
 * Why the unguarded walk this replaces was fatal
 * ----------------------------------------------
 * It was a plain `for (i = 0; i < 80; i++) print(sp[i])` from the raw
 * interrupted `rsp`, guarded by a clamp against `__mldr_stack_map_base` — a
 * symbol declared `weak` and defined nowhere in the tree, so it linked to 0
 * and the clamp never fired. When the guest faults inside a host mapping,
 * `rsp` often points somewhere the walk cannot read: the fault that motivated
 * this file was `si_code=SEGV_ACCERR`, meaning the address WAS mapped and the
 * access was NOT permitted — a guard page, not a hole. The walk took SIGSEGV
 * inside the handler, never reached its own `fflush`, and the process died of
 * the diagnostic's own fault with zero words written. The header had no
 * `fflush` of its own either, so not even the header survived.
 *
 * Two independent reasons a read is refused here:
 *   1. mincore(2) says the page is not resident in any mapping. It does not
 *      touch the address, so it cannot fault, and it rules out holes and
 *      swapped pages without entering the kernel's fault path at all.
 *   2. mincore's answer is not a permission answer. A `PROT_NONE` page is
 *      resident and mincore reports it present; only touching it separates
 *      "readable" from "mapped and forbidden". So the read itself runs inside
 *      sigsetjmp(3) with a temporary handler that siglongjmp(3)s out.
 *
 * Both are needed: mincore alone still walks into the guard page this file
 * was written for, and the fault guard alone pays a synchronous fault per bad
 * word. Refusal is not limited to faults either — `rsp` is data taken from a
 * context that is already known to be wrecked, so the address is range-checked
 * before any of this; see MLDR_STACK_MAX_ADDR.
 *
 * The jmp_buf is one per process, not per thread, so two threads dumping at
 * the same instant could jump into each other's probe. The failure mode is a
 * slot reported `(unreadable)` that was not — a wrong line in a dump printed
 * while the process is already dying — and it costs one sigaction pair per word
 * to paper over with a lock, which is a bad trade in a crash path.
 */

#include <setjmp.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <unistd.h>

#include "crash_dump.h"

/* Top of the x86_64 user address space on both hosts this runs on (Linux and
 * FreeBSD both use 0x0000800000000000). A stack pointer above it is not a
 * stack pointer, it is garbage read out of a wrecked context — refuse the
 * whole walk rather than probe the kernel once per word. */
#define MLDR_STACK_MAX_ADDR 0x0000800000000000ULL

/* Where the temporary fault handler jumps back to, and whether a probe read is
 * running. The probe handler is installed only while `dump_probe_active` is
 * 1, and a fault that arrives while it is 0 is not ours — it must not be
 * swallowed, because swallowing a real fault turns it into a loop inside a
 * crash handler. The window is a handful of instructions per word; both
 * signals are still blocked across it, because that is what a signal handler
 * installed from inside one gets. */
static sigjmp_buf           dump_probe_jmp;
static volatile sig_atomic_t dump_probe_active;

/* One probe at a time, process-wide.
 *
 * The probe swaps the process-wide SIGSEGV/SIGBUS disposition and uses the
 * one dump_probe_jmp above, and mldr runs guarded reads on any guest thread
 * (the trap handlers call them on every emulated xchg/call site). Two things
 * went wrong when two threads overlapped, and both end in a SIGSEGV with no
 * handler output at all:
 *   1. A genuine guest fault on another thread, taken while a probe window
 *      was open, was delivered to dump_probe_handler. With the probe flag set
 *      it longjmp'd into the OTHER thread's jmp_buf; with it clear it reset
 *      the disposition to SIG_DFL and re-raised. Either way the mldr crash
 *      handler never ran and the process died of the guest's own fault.
 *   2. Two overlapping probes each "restore the previous handler": the second
 *      one saved dump_probe_handler as its "previous" and put it back last,
 *      leaving the probe handler installed for good.
 * Fix: a spin lock serialises the windows (so exactly one saved disposition
 * exists and is restored), the probe records which thread owns it, and a
 * fault on any other thread is passed to the disposition that was installed
 * before the window opened. */
static volatile int          dump_probe_lock;
static volatile long         dump_probe_owner;
static struct sigaction      dump_probe_prev_segv, dump_probe_prev_bus;

static long
dump_self_tid(void)
{
    long tid = 0;
    /* raw syscall: no TLS, no libthr, safe on a thread whose fsbase is the guest's */
    syscall(SYS_thr_self, &tid);
    return tid;
}

/*
 * purpose:  Catch a fault raised by a probe read, and jump out of it; hand a
 *           fault that is not this thread's probe to the previous handler.
 * input:    signo, si, uctx — as delivered.
 * output:   siglongjmp back into mldr_dump_read_guarded for the owner thread's
 *           probe; otherwise calls the disposition that was installed before
 *           the window opened (or, if that was SIG_DFL/SIG_IGN, the process
 *           dies of the signal it was given).
 * sideEffects: does not return for the owner's probe.
 */
static void
dump_probe_handler(int signo, siginfo_t *si, void *uctx)
{
    if (dump_probe_active && dump_probe_owner == dump_self_tid()) {
        dump_probe_active = 0;
        siglongjmp(dump_probe_jmp, 1);
    }

    {
        const struct sigaction *prev =
            (signo == SIGBUS) ? &dump_probe_prev_bus : &dump_probe_prev_segv;

        if ((prev->sa_flags & SA_SIGINFO) && prev->sa_sigaction != NULL &&
            prev->sa_sigaction != dump_probe_handler) {
            prev->sa_sigaction(signo, si, uctx);
            return;
        }
        if (!(prev->sa_flags & SA_SIGINFO) && prev->sa_handler != SIG_DFL &&
            prev->sa_handler != SIG_IGN) {
            prev->sa_handler(signo);
            return;
        }
    }

    {
        struct sigaction sa_dfl;
        memset(&sa_dfl, 0, sizeof(sa_dfl));
        sa_dfl.sa_handler = SIG_DFL;
        sigemptyset(&sa_dfl.sa_mask);
        sigaction(signo, &sa_dfl, NULL);
        raise(signo);
    }
}

/*
 * purpose:  Read one 8-byte word, refusing the read if it would fault.
 * input:    addr — the address to read; out — receives the word.
 * output:   1 if the word was read, 0 if the address was refused as
 *           unreadable. Never faults, never aborts.
 * sideEffects: installs SIGSEGV/SIGBUS handlers for the duration of the read.
 *
 * `out` is volatile because the siglongjmp out of a faulting read leaves the
 * compiler free to treat this function's other locals as indeterminate; the
 * caller must not act on `*out` when 0 is returned, and marking the pointer
 * keeps that contract from being optimised into a stale value.
 */
int
mldr_dump_read_guarded(uintptr_t addr, volatile uint64_t *out)
{
    static long page_size; /* 0 until the first call */
    char resident = 0;      /* mincore's vec is a char*, and bit 0 = resident */
    struct sigaction sa_probe;
    int saved_segv_ok, saved_bus_ok;

    if (addr == 0 || addr >= MLDR_STACK_MAX_ADDR)
        return 0;

    if (page_size == 0) {
        page_size = sysconf(_SC_PAGESIZE);
        if (page_size <= 0)
            page_size = 4096;
    }

    /* (1) Page probe: mincore returns -1/ENOMEM for a hole and a cleared
     * residency bit for a page in no mapping, without touching the page. */
    if (mincore((void *)(addr & ~((uintptr_t)page_size - 1)),
                (size_t)page_size, &resident) != 0)
        return 0;
    if ((resident & 1) == 0)
        return 0;

    /* (2) Permission probe. Residency says nothing about readability, so the
     * read itself is guarded. Both handlers are saved and restored: this runs
     * inside the crash handler, and leaving a probe handler installed would
     * make every later fault in the process jump into a dead jmp_buf. */
    while (__sync_lock_test_and_set(&dump_probe_lock, 1))
        ; /* another thread's window is open; it is a handful of instructions */
    dump_probe_owner = dump_self_tid();
    memset(&sa_probe, 0, sizeof(sa_probe));
    sa_probe.sa_sigaction = dump_probe_handler;
    sigemptyset(&sa_probe.sa_mask);
    sa_probe.sa_flags = SA_SIGINFO | SA_ONSTACK;
    saved_segv_ok = (sigaction(SIGSEGV, &sa_probe, &dump_probe_prev_segv) == 0);
    saved_bus_ok = (sigaction(SIGBUS, &sa_probe, &dump_probe_prev_bus) == 0);
    if (!saved_segv_ok && !saved_bus_ok) {
        __sync_lock_release(&dump_probe_lock);
        return 0;
    }

    if (sigsetjmp(dump_probe_jmp, 1) != 0) {
        /* Faulted. dump_probe_handler already cleared the active flag. */
        if (saved_segv_ok)
            sigaction(SIGSEGV, &dump_probe_prev_segv, NULL);
        if (saved_bus_ok)
            sigaction(SIGBUS, &dump_probe_prev_bus, NULL);
        __sync_lock_release(&dump_probe_lock);
        return 0;
    }

    dump_probe_active = 1;
    *out = *(volatile uint64_t *)addr;
    dump_probe_active = 0;

    if (saved_segv_ok)
        sigaction(SIGSEGV, &dump_probe_prev_segv, NULL);
    if (saved_bus_ok)
        sigaction(SIGBUS, &dump_probe_prev_bus, NULL);
    __sync_lock_release(&dump_probe_lock);
    return 1;
}

/*
 * purpose:  Print a stack window, one line per word, never faulting.
 * input:    label — printed with the start address; start — the address of
 *           the first word; words — how many 8-byte words to print.
 * output:   The header line is flushed BEFORE the first word is probed, so a
 *           dump cut short for any reason still leaves behind the one line
 *           that says which address was being walked.
 * sideEffects: writes to stderr; briefly swaps SIGSEGV/SIGBUS handlers.
 */
void
mldr_dump_guarded_stack(const char *label, uintptr_t start, unsigned words)
{
    unsigned i;

    fprintf(stderr, "  %s=0x%016llx:\n", label,
            (unsigned long long)start);
    fflush(stderr); /* before the walk, not after it — see the file header */

    if (start == 0 || start >= MLDR_STACK_MAX_ADDR) {
        fprintf(stderr,
                "    (refusing to walk: 0x%016llx is not a user-space"
                " address)\n", (unsigned long long)start);
        fflush(stderr);
        return;
    }

    for (i = 0; i < words; i++) {
        uintptr_t addr = start + (uintptr_t)i * sizeof(uint64_t);
        unsigned long long off = (unsigned long long)i * sizeof(uint64_t);
        volatile uint64_t word = 0;

        if (addr >= MLDR_STACK_MAX_ADDR) {
            fprintf(stderr, "  [gstack+%4llu] (past the end of user space)\n",
                    off);
            continue;
        }
        if (mldr_dump_read_guarded(addr, &word))
            fprintf(stderr, "  [gstack+%4llu] 0x%016llx\n", off,
                    (unsigned long long)word);
        else
            fprintf(stderr, "  [gstack+%4llu] (unreadable)\n", off);
    }
    fflush(stderr);
}
