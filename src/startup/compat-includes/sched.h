/* FreeBSD shim: sched.h + CLONE_* namespace constants for Darling startup */
#pragma once
#include_next <sched.h>

/* Linux namespace clone flags — Darling startup uses these to select namespace types.
 * On FreeBSD these are re-mapped to jail(2) parameters in freebsd_jail.c. */
#ifndef CLONE_NEWNS
#define CLONE_NEWNS   0x00020000
#endif
#ifndef CLONE_NEWUTS
#define CLONE_NEWUTS  0x04000000
#endif
#ifndef CLONE_NEWIPC
#define CLONE_NEWIPC  0x08000000
#endif
#ifndef CLONE_NEWPID
#define CLONE_NEWPID  0x20000000
#endif
#ifndef CLONE_NEWUSER
#define CLONE_NEWUSER 0x10000000
#endif
#ifndef CLONE_NEWNET
#define CLONE_NEWNET  0x40000000
#endif
