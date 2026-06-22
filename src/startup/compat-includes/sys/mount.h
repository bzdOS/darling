/* FreeBSD shim: sys/mount.h — Linux MS_* flags + umount → FreeBSD equivalents */
#pragma once
#include_next <sys/mount.h>

/* Linux MS_* flags → FreeBSD MNT_* */
#ifndef MS_NOSUID
#  define MS_NOSUID    MNT_NOSUID
#endif
#ifndef MS_NODEV
#  define MS_NODEV     MNT_NODEV
#endif
#ifndef MS_NOEXEC
#  define MS_NOEXEC    MNT_NOEXEC
#endif
#ifndef MS_RDONLY
#  define MS_RDONLY    MNT_RDONLY
#endif
#ifndef MS_REMOUNT
#  define MS_REMOUNT   MNT_UPDATE
#endif
/* Propagation flags FreeBSD doesn't support — zero so flag ORing compiles */
#ifndef MS_REC
#  define MS_REC       0
#endif
#ifndef MS_SLAVE
#  define MS_SLAVE     0
#endif
#ifndef MS_SHARED
#  define MS_SHARED    0
#endif
#ifndef MS_PRIVATE
#  define MS_PRIVATE   0
#endif
#ifndef MS_BIND
#  define MS_BIND      0
#endif

/* Linux umount(target) → FreeBSD unmount(target, flags)
 * NOTE: no mount() macro here — the Linux 5-arg vs FreeBSD 4-arg signature
 * difference is handled with #ifdef DARLING_FREEBSD guards in the caller. */
#ifndef umount
#  define umount(tgt)  unmount((tgt), 0)
#endif
