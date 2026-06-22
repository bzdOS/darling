/* FreeBSD shim: sys/uio.h — adds process_vm_readv/writev via ptrace(PT_IO).
 *
 * Linux provides cross-process memory access as syscalls.  FreeBSD exposes
 * the same capability through ptrace(PT_IO) using struct ptrace_io_desc.
 * darlingserver always calls with liovcnt=riovcnt=1 and takes function
 * pointers to these, so they must be real symbols not macros. */
#pragma once
#include_next <sys/uio.h>

#ifdef DARLING_FREEBSD
#include <sys/ptrace.h>
#include <sys/types.h>

static inline ssize_t process_vm_readv(pid_t pid,
    const struct iovec *local_iov, unsigned long liovcnt,
    const struct iovec *remote_iov, unsigned long riovcnt, unsigned long flags)
{
    (void)liovcnt; (void)riovcnt; (void)flags;
    struct ptrace_io_desc iodesc;
    iodesc.piod_op   = PIOD_READ_D;
    iodesc.piod_offs = remote_iov[0].iov_base;
    iodesc.piod_addr = local_iov[0].iov_base;
    iodesc.piod_len  = local_iov[0].iov_len;
    if (ptrace(PT_IO, pid, (caddr_t)&iodesc, 0) < 0)
        return -1;
    return (ssize_t)iodesc.piod_len;
}

static inline ssize_t process_vm_writev(pid_t pid,
    const struct iovec *local_iov, unsigned long liovcnt,
    const struct iovec *remote_iov, unsigned long riovcnt, unsigned long flags)
{
    (void)liovcnt; (void)riovcnt; (void)flags;
    struct ptrace_io_desc iodesc;
    iodesc.piod_op   = PIOD_WRITE_D;
    iodesc.piod_offs = remote_iov[0].iov_base;
    iodesc.piod_addr = local_iov[0].iov_base;
    iodesc.piod_len  = local_iov[0].iov_len;
    if (ptrace(PT_IO, pid, (caddr_t)&iodesc, 0) < 0)
        return -1;
    return (ssize_t)iodesc.piod_len;
}
#endif /* DARLING_FREEBSD */
