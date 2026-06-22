/* FreeBSD shim: sys/socket.h — maps Linux socket credential constants.
 *
 * Linux sends credential control messages with SCM_CREDENTIALS + struct ucred.
 * FreeBSD uses SCM_CREDS + struct cmsgcred (the kernel fills in the receiver's
 * copy regardless of what the sender wrote).
 *
 * SO_PASSCRED (Linux): tells socket to pass credentials in SCM_CREDENTIALS.
 * LOCAL_CREDS (FreeBSD): equivalent — tells the socket to pass cmsgcred.
 *
 * DARLING_CRED_CMSG_SIZE: the buffer size that must be allocated for receiving
 * a credential control message.  On FreeBSD the kernel writes struct cmsgcred
 * (44+ bytes); on Linux it writes struct ucred (12 bytes). */
#pragma once
#include_next <sys/socket.h>
#include <sys/un.h>   /* LOCAL_CREDS, LOCAL_PEERCRED */

#ifdef DARLING_FREEBSD
#  include <sys/ucred.h>  /* struct bsdos_ucred (our shim), struct cmsgcred */
#  ifndef SCM_CREDENTIALS
#    define SCM_CREDENTIALS SCM_CREDS
#  endif
/* struct cmsgcred is defined in <sys/socket.h> on FreeBSD */
#  define DARLING_CRED_CMSG_SIZE sizeof(struct cmsgcred)
/* SO_PASSCRED → LOCAL_CREDS; level 0 = SOL_LOCAL on FreeBSD Unix sockets */
#  ifndef SO_PASSCRED
#    define SO_PASSCRED LOCAL_CREDS
#    ifndef SOL_SOCKET_CREDS_LEVEL
#      define SOL_SOCKET_CREDS_LEVEL 0
#    endif
#  endif
#else
#  define DARLING_CRED_CMSG_SIZE sizeof(struct ucred)
#endif
