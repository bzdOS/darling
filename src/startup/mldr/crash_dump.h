/*
 * crash_dump.h — the guarded stack dump used by mldr's crash handler.
 *
 * Split out of freebsd_syscall_trap.c so the guard can be exercised on its
 * own: build-freebsd/crash-dump-selftest.c includes crash_dump.c directly and
 * provokes the failures the dump has to survive, rather than testing a copy
 * of the logic that could drift from the one that ships.
 */

#ifndef MLDR_CRASH_DUMP_H
#define MLDR_CRASH_DUMP_H

#include <stdint.h>

/* Words printed per dump. 80 is the count the unguarded walk this replaced
 * used, and it comfortably holds a Darwin x86_64 frame chain plus the
 * arguments pushed at the faulting call. */
#define MLDR_STACK_DUMP_WORDS 80

/*
 * purpose:  Read one 8-byte word, refusing the read if it would fault.
 * input:    addr — the address to read; out — receives the word.
 * output:   1 if the word was read, 0 if the address was refused as
 *           unreadable. Does not fault and does not abort.
 */
int mldr_dump_read_guarded(uintptr_t addr, volatile uint64_t *out);

/*
 * purpose:  Print a stack window, one line per word, never faulting.
 * input:    label — printed as `  <label>=0xADDR:`; start — the address of
 *           the first word; words — how many 8-byte words to print. The
 *           header text is what build-freebsd/decode-crash.py looks for, so
 *           the `=` is load-bearing.
 * output:   The header is flushed before the first probe, so a dump cut
 *           short still leaves the line saying which address was walked.
 */
void mldr_dump_guarded_stack(const char *label, uintptr_t start, unsigned words);

#endif /* MLDR_CRASH_DUMP_H */
