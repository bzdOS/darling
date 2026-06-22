/* FreeBSD shim: sys/prctl.h — Linux process control, mapped to no-ops */
#pragma once
#define PR_SET_DUMPABLE  4
#define PR_GET_DUMPABLE  3
#define PR_SET_NAME      15
#define PR_GET_NAME      16
static inline int prctl(int option, ...) { (void)option; return 0; }
