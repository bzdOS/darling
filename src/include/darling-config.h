#ifndef CONFIG_H
#define CONFIG_H

#define LIB_PATH "/usr/local/libexec/darling/lib/darling"
#define LIBEXEC_PATH "/usr/local/libexec/darling"
#define LIB_DIR_NAME "lib"
/* M0 test prefix — override with -DINSTALL_PREFIX=... if needed */
#ifndef INSTALL_PREFIX
#define INSTALL_PREFIX "/usr/local/libexec/darling"
#endif
#define SHARE_PATH "/usr/local/libexec/darling/share/darling"
#define DYLD_PATH "/usr/local/libexec/darling/bin/dyld"
#define ETC_DARLING_PATH "/etc/darling"
/* Path where the system root gets "mounted" inside the prefix */
#define SYSTEM_ROOT "/Volumes/SystemRoot"
#define GIT_BRANCH "bsdos-freebsd"
#define GIT_COMMIT_HASH "0000000"

#ifndef __APPLE__
#include <stdint.h>
#define __uint64_t uint64_t
#endif

#endif /* CONFIG_H */
