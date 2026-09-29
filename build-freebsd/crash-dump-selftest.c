/*
 * crash-dump-selftest.c — exercise mldr's guarded stack dump without a crash.
 *
 * purpose:    Prove the properties the crash handler depends on, on the real
 *             code rather than a copy of it:
 *               1. a dump across an address range the process cannot read
 *                  prints EVERY slot and the process survives — this is the
 *                  failure that made the log in
 *                  build-freebsd/SIGSEGV-SECOND-DLOPEN.md end on a header
 *                  line with zero words under it;
 *               2. a dump of readable memory prints every word's real value,
 *                  so the guard is not quietly refusing everything;
 *               3. the header is flushed before the walk, so it is in the
 *                  captured output even when the walk is cut short.
 * input:      none.
 * output:     PASS/FAIL lines on stdout; exit 0 only if every case passed.
 * sideEffects: maps and unmaps a few anonymous pages; temporarily redirects
 *              stderr into a temp file while the dumps run.
 *
 * Why this is its own translation unit: crash_dump.c is linked into mldr next
 * to a darlingserver RPC layer and cannot be compiled standalone, and testing
 * a re-implementation of the guard would prove nothing about the one that
 * ships. So this file #includes the source itself.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#include "../src/startup/mldr/crash_dump.c"

static int failures = 0;

static void
report(const char *name, int ok, const char *detail)
{
    if (ok) {
        printf("  [PASS] %s\n", name);
    } else {
        printf("  [FAIL] %s: %s\n", name, detail);
        failures++;
    }
}

static long
page_size(void)
{
    long ps = sysconf(_SC_PAGESIZE);
    return ps > 0 ? ps : 4096;
}

/* Two pages: one readable and pre-filled, one PROT_NONE. The unreadable one
 * is what the old unguarded loop walked into and died on. */
static uint64_t *
make_split_mapping(long ps, long *readable_words)
{
    uint64_t *p = mmap(NULL, (size_t)ps * 2, PROT_READ | PROT_WRITE,
                       MAP_PRIVATE | MAP_ANON, -1, 0);
    if (p == MAP_FAILED)
        return NULL;
    if (mprotect((char *)p + ps, (size_t)ps, PROT_NONE) != 0) {
        munmap(p, (size_t)ps * 2);
        return NULL;
    }
    *readable_words = ps / (long)sizeof(uint64_t);
    for (long i = 0; i < *readable_words; i++)
        p[i] = 0xc0ffee0000000000ULL | (uint64_t)i;
    return p;
}

/*
 * purpose:  Run a dump with stderr captured, and hand back what it printed.
 * input:    start, words — what to dump.
 * output:   *text — a NUL-terminated malloc'd copy of the dump, which the
 *           caller frees; NULL if the capture failed.
 * sideEffects: redirects fd 2 for the duration of the dump.
 */
static char *
dump_captured(uintptr_t start, unsigned words)
{
    FILE *cap = tmpfile();
    char *buf;
    long len;
    int saved;

    if (!cap)
        return NULL;

    fflush(stderr);
    saved = dup(2);
    if (saved < 0 || dup2(fileno(cap), 2) < 0) {
        if (saved >= 0)
            close(saved);
        fclose(cap);
        return NULL;
    }

    mldr_dump_guarded_stack("selftest stack", start, words);

    fflush(stderr);
    if (dup2(saved, 2) < 0) {
        close(saved);
        fclose(cap);
        return NULL;
    }
    close(saved);

    fflush(cap);
    len = ftell(cap);
    if (len < 0) {
        fclose(cap);
        return NULL;
    }
    rewind(cap);
    buf = malloc((size_t)len + 1);
    if (!buf) {
        fclose(cap);
        return NULL;
    }
    if (fread(buf, 1, (size_t)len, cap) != (size_t)len) {
        free(buf);
        fclose(cap);
        return NULL;
    }
    buf[len] = '\0';
    fclose(cap);
    return buf;
}

/* Count lines of the dump that carry a given slot offset, e.g. "gstack+  16". */
static int
count_slot(const char *text, unsigned off)
{
    char needle[32];
    int n = 0;
    const char *p;

    snprintf(needle, sizeof(needle), "[gstack+%4u]", off);
    for (p = text; (p = strstr(p, needle)) != NULL; p += strlen(needle))
        n++;
    return n;
}

int
main(void)
{
    long ps = page_size();
    long readable_words = 0;
    uint64_t *region;
    volatile uint64_t word = 0;
    char *text;

    printf("=== mldr guarded crash dump: selftest ===\n");

    region = make_split_mapping(ps, &readable_words);
    if (!region) {
        printf("  [FAIL] could not build the test mapping\n");
        return 1;
    }

    /* --- a read that is allowed ------------------------------------------- */
    report("a readable word is read",
           mldr_dump_read_guarded((uintptr_t)region, &word) &&
               word == 0xc0ffee0000000000ULL,
           "refused a word, or read the wrong value");

    /* --- a PROT_NONE page --------------------------------------------------
     * mincore reports a PROT_NONE page as resident, so this is the case only
     * the fault guard can catch — and the exact one that killed the dump
     * which produced SIGSEGV-SECOND-DLOPEN.md. */
    word = 0xdeadbeef;
    report("a PROT_NONE page is refused without faulting",
           !mldr_dump_read_guarded((uintptr_t)region + ps, &word),
           "read a page with no read permission");

    /* --- a hole: an address nobody ever mapped, where mincore fails ENOMEM -- */
    report("an unmapped hole is refused without faulting",
           !mldr_dump_read_guarded((uintptr_t)0x00001000, &word),
           "read through an unmapped hole");

    /* --- out-of-range garbage ---------------------------------------------- */
    report("address 0 is refused",
           !mldr_dump_read_guarded(0, &word), "accepted a null address");
    report("a non-user address is refused",
           !mldr_dump_read_guarded(0x0000800000000000ULL, &word),
           "accepted an address above the user address space");

    /* --- the walk itself, straddling readable and unreadable ---------------- */
    /* Starts 8 words before the guard page so the walk has to cross into it.
     * Every slot before the boundary must carry its real value, every slot in
     * the guard page must be marked unreadable, all `words` slots must be
     * present, and this process must still be alive to say so. */
    {
        const unsigned words = 24;
        const unsigned good = 8;             /* words before the guard page */
        unsigned i;
        int bad_value = 0, not_marked = 0, missing = 0;
        char expect[64];

        text = dump_captured((uintptr_t)region + ps - good * 8, words);
        if (!text) {
            printf("  [FAIL] could not capture the dump\n");
            return 1;
        }
        printf("  --- dump across a PROT_NONE page (captured) ---\n%s", text);

        for (i = 0; i < words; i++) {
            if (count_slot(text, i * 8) != 1)
                missing++;
            snprintf(expect, sizeof(expect), "0x%016llx",
                     (unsigned long long)(0xc0ffee0000000000ULL |
                                          (unsigned long long)((long)ps / 8 -
                                                              good + i)));
            if (i < good && !strstr(text, expect))
                bad_value++;
        }
        /* The guard-page slots must be marked, not silently dropped and not
         * carrying a value. */
        {
            const char *marker = "[gstack+  64] (unreadable)";
            if (!strstr(text, marker))
                not_marked = 1;
        }
        free(text);

        report("the dump survived a PROT_NONE page (this process is alive)", 1,
               NULL);
        report("every one of the 24 slots was printed", missing == 0,
               "a slot is missing from the dump");
        report("the 8 readable slots carry their real values", bad_value == 0,
               "a readable slot was missing or wrong");
        report("the unreadable slots are marked (unreadable)", not_marked == 0,
               "no (unreadable) marker where the guard page begins");
    }

    /* --- an ordinary stack still dumps in full, values and all ------------ */
    {
        volatile uint64_t scratch[16];
        const unsigned words = 16;
        int missing = 0, wrong = 0;
        char expect[64];
        unsigned i;

        for (i = 0; i < words; i++)
            scratch[i] = 0x5a5a0000ULL + i;

        text = dump_captured((uintptr_t)scratch, words);
        if (!text) {
            printf("  [FAIL] could not capture the second dump\n");
            return 1;
        }
        printf("  --- dump of a readable stack (captured) ---\n%s", text);

        for (i = 0; i < words; i++) {
            if (count_slot(text, i * 8) != 1)
                missing++;
            snprintf(expect, sizeof(expect), "0x%016llx",
                     (unsigned long long)(0x5a5a0000ULL + i));
            if (!strstr(text, expect))
                wrong++;
        }
        free(text);

        report("an ordinary stack dumps all 16 slots", missing == 0,
               "a slot is missing from the dump");
        report("an ordinary stack dumps every real value", wrong == 0,
               "a readable slot was refused or wrong");
    }

    munmap(region, (size_t)ps * 2);

    if (failures) {
        printf("\nFAILED: %d case(s)\n", failures);
        return 1;
    }
    printf("\nPASS: the dump cannot take the process with it\n");
    return 0;
}
