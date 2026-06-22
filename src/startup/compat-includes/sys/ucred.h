/* FreeBSD shim: sys/ucred.h
 *
 * Pull in FreeBSD's real sys/ucred.h (provides struct ucred for kernel use,
 * struct xucred used by sys/mount.h, etc.), then add bsdos_ucred which is the
 * Linux-compatible socket credential struct {pid_t pid; uid_t uid; gid_t gid}.
 *
 * We do NOT redefine struct ucred (FreeBSD's kernel ucred). Instead darlingserver
 * code uses struct bsdos_ucred for socket credential passing. */
#pragma once
#include_next <sys/ucred.h>   /* FreeBSD's real ucred — defines struct xucred etc. */

#ifndef _BSDOS_UCRED_DEFINED
#define _BSDOS_UCRED_DEFINED
struct bsdos_ucred {
	pid_t pid;
	uid_t uid;
	gid_t gid;
};
#endif
