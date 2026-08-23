/* FreeBSD shim: sys/socket.h — maps Linux socket credential constants.
 *
 * Linux sends credential control messages with SCM_CREDENTIALS + struct ucred.
 *
 * FreeBSD has TWO distinct kernel-verified credential mechanisms, delivered
 * via different cmsg types and structs, and this codebase needs the one that
 * actually matches which socket option is set (#198, live-diagnosed via a
 * standalone recvmsg/recvmmsg test and debug prints in receiveMany()):
 *   - LOCAL_CREDS            → SCM_CREDS  + struct cmsgcred,  but ONLY on the
 *     FIRST datagram ever received on a given listening socket for its whole
 *     lifetime (unix(4)) — useless for a server fielding many clients over
 *     its run; every later checkin gets no credentials cmsg at all.
 *   - LOCAL_CREDS_PERSISTENT → SCM_CREDS2 + struct sockcred2, attached to
 *     EVERY datagram — this is the one darlingserver actually sets
 *     (server.cpp). Confirmed live: with LOCAL_CREDS_PERSISTENT set, every
 *     received message's cmsg has cmsg_type == SCM_CREDS2 (8), never
 *     SCM_CREDS (3) — so code that searches for SCM_CREDS/cmsgcred here
 *     always comes up empty (Process::id() read back as 0/-1 for every
 *     process, not just some, which is exactly this: cmsgcred is simply never
 *     delivered when LOCAL_CREDS_PERSISTENT is the option in effect).
 *
 * IMPORTANT asymmetry: SCM_CREDS2 is receive-only. A userspace sendmsg() that
 * attaches its own SCM_CREDS2 cmsg (as message.cpp's self-fabricated
 * placeholder — used when locally forwarding/overriding pid/uid/gid via
 * setPID()/setUID(), or on any freshly-constructed outgoing Message before a
 * real one has been received into it — does for every outgoing message) gets
 * the whole sendmsg() rejected outright with EINVAL. Confirmed live: this
 * silently broke the mldr<->darlingserver worker-thread notification path
 * (a fork()'d child's registration message, sent by darlingserver back to
 * mldr's own per-process socket) the first time this file mapped
 * SCM_CREDENTIALS to SCM_CREDS2 — the sendmsg() failed, mldr never got
 * notified, and it hung forever in recvmsg(). SCM_CREDS/cmsgcred has no such
 * restriction (the kernel silently ignores/overwrites whatever a sender
 * attaches), which is why it stays the type for SELF-fabricated/outgoing
 * cmsgs below, while SCM_CREDS2/sockcred2 is only ever the type actually
 * searched for and decoded on RECEIVE (see message.cpp's _credentialsHeader()
 * and copyCredentialsOut(), which check for either type explicitly rather
 * than through this single macro).
 *
 * SO_PASSCRED (Linux): tells socket to pass credentials in SCM_CREDENTIALS.
 *
 * DARLING_CRED_CMSG_SIZE: the buffer size that must be allocated for a
 * credential control message, sized for the larger of the two possible
 * shapes (struct sockcred2 with headroom for CMGROUP_MAX supplemental
 * groups, to match cmsgcred's own group-array capacity — pid/uid/gid sit
 * before the variable-length group array either way, so even if a sender has
 * more groups than fit, the fields this code actually reads are never
 * truncated). On Linux it's sized for struct ucred (12 bytes). */
#pragma once
#include_next <sys/socket.h>
#include <sys/un.h>   /* LOCAL_CREDS, LOCAL_CREDS_PERSISTENT, LOCAL_PEERCRED */

#ifdef DARLING_FREEBSD
#  include <sys/ucred.h>  /* struct bsdos_ucred (our shim), struct cmsgcred */
/* struct sockcred2 / SCM_CREDS2 / SOCKCRED2SIZE are defined in <sys/socket.h>
 * on FreeBSD. SCM_CREDENTIALS keeps mapping to the classic, sendable
 * SCM_CREDS/cmsgcred — see this file's header comment on why SCM_CREDS2 must
 * NOT be used here. */
#  ifndef SCM_CREDENTIALS
#    define SCM_CREDENTIALS SCM_CREDS
#  endif
#  define DARLING_CRED_CMSG_SIZE (sizeof(struct cmsgcred) > SOCKCRED2SIZE(CMGROUP_MAX) \
                                    ? sizeof(struct cmsgcred) : SOCKCRED2SIZE(CMGROUP_MAX))
/* SO_PASSCRED → LOCAL_CREDS_PERSISTENT; level 0 = SOL_LOCAL on FreeBSD Unix sockets */
#  ifndef SO_PASSCRED
#    define SO_PASSCRED LOCAL_CREDS_PERSISTENT
#    ifndef SOL_SOCKET_CREDS_LEVEL
#      define SOL_SOCKET_CREDS_LEVEL 0
#    endif
#  endif
#else
#  define DARLING_CRED_CMSG_SIZE sizeof(struct ucred)
#endif
