/* FreeBSD port: inline Mach boolean type (avoids 9p symlink issues) */
#ifndef _MACH_MACHINE_BOOLEAN_H_
#define _MACH_MACHINE_BOOLEAN_H_
#if defined(__x86_64__) && !defined(KERNEL)
typedef unsigned int    boolean_t;
#elif defined(__aarch64__)
typedef unsigned int    boolean_t;
#else
typedef int             boolean_t;
#endif
#endif /* _MACH_MACHINE_BOOLEAN_H_ */
