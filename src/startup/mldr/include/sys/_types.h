#ifndef _MLDR_SYS__TYPES_SHIM_H_
#define _MLDR_SYS__TYPES_SHIM_H_

#ifdef DARLING_FREEBSD
/* Pass through to the real sys/_types.h for __int8_t, __int16_t, etc. */
#include_next <sys/_types.h>
#endif

#endif // _MLDR_SYS__TYPES_SHIM_H_
