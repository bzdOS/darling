/* FreeBSD shim: sys/mman.h — adds MAP_FIXED_NOREPLACE.
 *
 * Linux 4.17+ MAP_FIXED_NOREPLACE: like MAP_FIXED but returns EEXIST instead
 * of silently unmapping an existing mapping.  FreeBSD 11+ has MAP_EXCL which
 * gives the same guarantee when combined with MAP_FIXED. */
#pragma once
#include_next <sys/mman.h>

#ifdef DARLING_FREEBSD
#  ifndef MAP_FIXED_NOREPLACE
#    define MAP_FIXED_NOREPLACE (MAP_FIXED | MAP_EXCL)
#  endif
#endif
