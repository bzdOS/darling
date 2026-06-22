/* FreeBSD shim: linux/sched.h — provides Linux clone flags */
#pragma once
#include <sched.h>  /* our compat-includes/sched.h with CLONE_* */
/* Additional CLONE flags used by darlingserver process spawning */
#ifndef CLONE_VM
#  define CLONE_VM         0x00000100
#endif
#ifndef CLONE_FILES
#  define CLONE_FILES      0x00000400
#endif
#ifndef CLONE_SIGHAND
#  define CLONE_SIGHAND    0x00000800
#endif
#ifndef CLONE_THREAD
#  define CLONE_THREAD     0x00010000
#endif
#ifndef CLONE_SYSVSEM
#  define CLONE_SYSVSEM    0x00040000
#endif
#ifndef CLONE_SETTLS
#  define CLONE_SETTLS     0x00080000
#endif
