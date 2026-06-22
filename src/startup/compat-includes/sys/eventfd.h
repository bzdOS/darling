/* FreeBSD shim: sys/eventfd.h — implements Linux eventfd using a socketpair.
 *
 * eventfd is a Linux-specific "event notification" fd whose counter semantics
 * are compatible with what darlingserver needs (write-to-wake, read-to-drain).
 * We implement it with a socket pair: the "eventfd" is the read socket; the
 * write socket is stored in a small lookup table keyed by the read-socket fd.
 * Accumulation semantics differ from real eventfd (each write is one message),
 * but darlingserver only writes 1 and reads once, so this is fine. */
#pragma once

#ifndef _BSDOS_EVENTFD_H_
#define _BSDOS_EVENTFD_H_

#include <stdint.h>
#include <sys/types.h>

typedef uint64_t eventfd_t;

#define EFD_CLOEXEC 0x80000
#define EFD_NONBLOCK 0x800
#define EFD_SEMAPHORE 0x1

#ifdef __cplusplus
extern "C" {
#endif

int eventfd(unsigned int initval, int flags);
int eventfd_read(int fd, eventfd_t *value);
int eventfd_write(int fd, eventfd_t value);

#ifdef __cplusplus
}
#endif

#endif /* _BSDOS_EVENTFD_H_ */
