#include <string.h>
#include <sys/sysctl.h>
#include <sys/errno.h>
#include <mach-o/dyld-interposing.h>

static int
my_sysctlbyname(const char *name, void *oldp, size_t *oldlenp, void *newp, size_t newlen)
{
    if (name && strcmp(name, "kern.secure_kernel") == 0) {
        if (oldp && oldlenp && *oldlenp >= sizeof(int)) {
            *(int *)oldp = 1;
            *oldlenp = sizeof(int);
        }
        return 0;
    }

    return sysctlbyname(name, oldp, oldlenp, newp, newlen);
}

DYLD_INTERPOSE(my_sysctlbyname, sysctlbyname)
