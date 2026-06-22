/* FreeBSD shim: sys/user.h — provides struct user_regs_struct for thread.cpp.
 *
 * Linux <sys/user.h> defines struct user_regs_struct for ptrace register access.
 * FreeBSD uses struct reg (from <machine/reg.h>), with r_-prefixed field names.
 * We expose user_regs_struct as a C++ subclass of struct reg so code that accesses
 * r_rsp, r_rip, etc. works unchanged; code expecting Linux-named fields needs its
 * own #ifdef DARLING_FREEBSD guards (thread.cpp already has them). */
#pragma once
#include_next <sys/user.h>

#ifdef DARLING_FREEBSD
#include <machine/reg.h>

#ifdef __cplusplus
struct user_regs_struct : public reg {
    user_regs_struct() = default;
    user_regs_struct(const struct reg& r) : reg(r) {}
};
#else
typedef struct reg user_regs_struct;
#endif
#endif /* DARLING_FREEBSD */
