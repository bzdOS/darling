/* FreeBSD shim: sys/timerfd.h — implements Linux timerfd using a background
 * thread + pipe.
 *
 * timerfd_create() opens a pipe pair; the read end is the returned fd.
 * timerfd_settime() records the deadline; a per-fd pthread sleeps until the
 * deadline then writes an expiry count to the pipe, making it readable.
 * Reading from the fd drains the count (returns a uint64_t). */
#pragma once

#ifndef _BSDOS_TIMERFD_H_
#define _BSDOS_TIMERFD_H_

#include <time.h>
#include <stdint.h>

#define TFD_CLOEXEC  0x80000
#define TFD_NONBLOCK 0x800
#define TFD_TIMER_ABSTIME (1 << 0)

#ifdef __cplusplus
extern "C" {
#endif

int timerfd_create(int clockid, int flags);
int timerfd_settime(int fd, int flags,
                    const struct itimerspec *new_value,
                    struct itimerspec *old_value);

#ifdef __cplusplus
}
#endif

#endif /* _BSDOS_TIMERFD_H_ */
