/*
 * freebsd_mig_compat.h — Compatibility shim for building darling host tools
 * (migcom, ld64) on FreeBSD.
 *
 * Provides Darwin-specific types/macros that FreeBSD doesn't have,
 * and blocks Darwin type headers that conflict with FreeBSD's.
 *
 * This header is force-included (-include) before any source files.
 */
#ifndef _FREEBSD_MIG_COMPAT_H
#define _FREEBSD_MIG_COMPAT_H

/* Block Darwin's i386/_types.h which redefines __int64_t as long long
   (FreeBSD defines it as long in sys/_types.h) */
#ifndef _BSD_I386__TYPES_H_
#define _BSD_I386__TYPES_H_
#endif

/* Darwin-specific types that mach headers need */
#ifndef __darwin_natural_t
typedef unsigned int        __darwin_natural_t;
#endif

#ifndef __darwin_intptr_t
#ifdef __LP64__
typedef long                __darwin_intptr_t;
#else
typedef int                 __darwin_intptr_t;
#endif
#endif

#ifndef __darwin_mach_port_t
typedef unsigned int        __darwin_mach_port_t;
#endif

/* UUID type for mach headers */
#ifndef __darwin_uuid_t
typedef unsigned char       __darwin_uuid_t[16];
#endif

/* Darwin size/ssize types (used by flat SDK sys/_types headers) */
#ifndef __darwin_ssize_t
#ifdef __LP64__
typedef long                __darwin_ssize_t;
#else
typedef int                 __darwin_ssize_t;
#endif
#endif

#ifndef __darwin_size_t
#ifdef __LP64__
typedef unsigned long       __darwin_size_t;
#else
typedef unsigned int        __darwin_size_t;
#endif
#endif

/* fsid_t — Darwin mach headers need it as a full struct definition.
   FreeBSD sys/mount.h also defines it; block that with _FSID_T guard. */
#ifndef _FSID_T
#define _FSID_T
typedef struct fsid { long val[2]; } fsid_t;
#endif

/* user_addr_t — used by mach/shared_region.h, not in flat SDK */
#ifndef _USER_ADDR_T
#define _USER_ADDR_T
typedef unsigned long user_addr_t;
#endif

/* proc_regionfilename and abort_with_payload — Darwin syscalls not in flat SDK */
#ifndef __DARLING_SYSCALL_DECLS
#define __DARLING_SYSCALL_DECLS
int proc_regionfilename(int pid, unsigned long long address, char *buffer, unsigned int buffersize);
void abort_with_payload(void *reason, unsigned int reason_size, const char *payload, unsigned int payload_size, const char *reason_str, unsigned int reason_str_code);
#endif

/* CrashReporterClient definitions — inlined to avoid -include <stdint.h> 
   from the flat SDK which pulls in unresolvable Darwin type chains */
#ifndef CRASHREPORTERCLIENT_H_
#define CRASHREPORTERCLIENT_H_

#define CRASHREPORTER_ANNOTATIONS_SECTION "__crash_info"
#define CRASHREPORTER_ANNOTATIONS_VERSION 5
#define CRASH_REPORTER_CLIENT_HIDDEN __attribute__((visibility("hidden")))

#ifdef __cplusplus
extern "C" {
#endif

inline void CRSetCrashLogMessage(const char* text) { (void)text; }
inline void CRSetCrashLogMessage2(const char* path) { (void)path; }
inline const char* CRGetCrashLogMessage(void) { return 0; }

struct crashreporter_annotations_t {
    unsigned long long version;
    unsigned long long message;
    unsigned long long signature_string;
    unsigned long long backtrace;
    unsigned long long message2;
    unsigned long long thread;
    unsigned long long dialog_mode;
    unsigned long long abort_cause;
};

CRASH_REPORTER_CLIENT_HIDDEN
extern struct crashreporter_annotations_t gCRAnnotations;

#ifdef __cplusplus
}
#endif
#endif /* CRASHREPORTERCLIENT_H_ */

/* mach_port_t is typically unsigned int on x86_64 */
#ifndef _SYS__TYPES_MACH_PORT_T
#define _SYS__TYPES_MACH_PORT_T
#endif

#endif /* _FREEBSD_MIG_COMPAT_H */
